/*
 * A1000 (sc8830): power HAL.
 *
 * Управляем только границами частоты общей политики cpufreq — этого достаточно
 * и это единственное, что доступно процессу system без правки прав:
 *
 *   scaling_min_freq / scaling_max_freq  ->  system:system
 *   scaling_governor, ручки sprdemand    ->  root (не трогаем)
 *
 * Логика:
 *   экран включён      max = 1300000            (полная скорость доступна)
 *   экран выключен     max = 1000000            (фон не разгоняет камень)
 *   касание            min = 1000000 на 800 мс  (нет паузы на разгон)
 *   запуск приложения  min = 1200000 на 2000 мс (самый тяжёлый момент)
 *   энергосбережение   max = 1000000
 *
 * Буст снимается отдельным потоком по таймеру: он ждёт на условной переменной
 * до дедлайна, и если новых подсказок не пришло — возвращает min на место.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <hardware/hardware.h>
#include <hardware/power.h>

#define LOG_TAG "power_sc8830"
#include <log/log.h>

#define CPUFREQ_MIN "/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq"
#define CPUFREQ_MAX "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"

/* Реальные точки политики этого SoC. */
#define FREQ_MIN         768000
#define FREQ_INTERACTIVE 1000000
#define FREQ_LAUNCH      1200000
#define FREQ_MAX         1300000

/* Экран выключен и режим энергосбережения одинаково ограничивают потолок. */
#define FREQ_MAX_SCREEN_OFF 1000000
#define FREQ_MAX_LOW_POWER  1000000

#define BOOST_MS_INTERACTION 800
#define BOOST_MS_LAUNCH      2000

struct a1000_power {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t boost_thread;

    int screen_on;      /* setInteractive */
    int low_power;      /* POWER_HINT_LOW_POWER */

    int boost_freq;     /* 0 = буста нет */
    long long boost_until_ms;

    int cur_min;
    int cur_max;

    int inited;
};

/*
 * cond инициализируется в init() через pthread_condattr_setclock(CLOCK_MONOTONIC):
 * дедлайн буста считается по монотонным часам, а PTHREAD_COND_INITIALIZER даёт
 * условную переменную на CLOCK_REALTIME. При таком несовпадении
 * pthread_cond_timedwait() истекает мгновенно, и поток крутится вхолостую всю
 * длительность буста — на четырёхъядерном A7 это сожжённое ядро на каждое
 * касание.
 */
static struct a1000_power g_pwr = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .screen_on = 1,
    .low_power = 0,
    .boost_freq = 0,
    .boost_until_ms = 0,
    .cur_min = -1,
    .cur_max = -1,
    .inited = 0,
};

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void sysfs_write_int(const char *path, int value)
{
    char buf[32];
    int len;
    int fd = open(path, O_WRONLY);

    if (fd < 0) {
        ALOGE("не открыть %s: %s", path, strerror(errno));
        return;
    }
    len = snprintf(buf, sizeof(buf), "%d", value);
    if (write(fd, buf, len) < 0) {
        ALOGE("не записать %d в %s: %s", value, path, strerror(errno));
    }
    close(fd);
}

/* Считает желаемые границы из текущего состояния и пишет только изменившееся. */
static void apply_locked(void)
{
    int want_max = FREQ_MAX;
    int want_min = FREQ_MIN;

    if (!g_pwr.screen_on) {
        want_max = FREQ_MAX_SCREEN_OFF;
    }
    if (g_pwr.low_power && want_max > FREQ_MAX_LOW_POWER) {
        want_max = FREQ_MAX_LOW_POWER;
    }
    if (g_pwr.boost_freq > 0) {
        want_min = g_pwr.boost_freq;
    }
    /* Нижняя граница не может оказаться выше верхней. */
    if (want_min > want_max) {
        want_min = want_max;
    }

    /*
     * Порядок важен: сначала расширяем окно, потом сужаем, иначе ядро отвергнет
     * промежуточное состояние, где min > max.
     */
    if (want_max > g_pwr.cur_max) {
        sysfs_write_int(CPUFREQ_MAX, want_max);
        g_pwr.cur_max = want_max;
    }
    if (want_min != g_pwr.cur_min) {
        sysfs_write_int(CPUFREQ_MIN, want_min);
        g_pwr.cur_min = want_min;
    }
    if (want_max < g_pwr.cur_max) {
        sysfs_write_int(CPUFREQ_MAX, want_max);
        g_pwr.cur_max = want_max;
    }
}

static void *boost_thread_fn(void *arg)
{
    (void)arg;

    pthread_mutex_lock(&g_pwr.lock);
    for (;;) {
        if (g_pwr.boost_freq == 0) {
            /* Буста нет — спим до следующей подсказки. */
            pthread_cond_wait(&g_pwr.cond, &g_pwr.lock);
            continue;
        }

        long long now = now_ms();
        if (now >= g_pwr.boost_until_ms) {
            g_pwr.boost_freq = 0;
            apply_locked();
            continue;
        }

        {
            long long left = g_pwr.boost_until_ms - now;
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ts.tv_sec += left / 1000;
            ts.tv_nsec += (left % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000L;
            }
            /*
             * Ждём до дедлайна. Новая подсказка разбудит раньше и сдвинет
             * дедлайн — тогда просто пойдём на следующий круг.
             */
            pthread_cond_timedwait(&g_pwr.cond, &g_pwr.lock, &ts);
        }
    }
    /* сюда не приходим */
    pthread_mutex_unlock(&g_pwr.lock);
    return NULL;
}

static void boost(int freq, int duration_ms)
{
    long long until = now_ms() + duration_ms;

    pthread_mutex_lock(&g_pwr.lock);
    /* Более сильный или более долгий буст побеждает, слабый его не отменяет. */
    if (freq > g_pwr.boost_freq) {
        g_pwr.boost_freq = freq;
    }
    if (until > g_pwr.boost_until_ms) {
        g_pwr.boost_until_ms = until;
    }
    apply_locked();
    pthread_cond_signal(&g_pwr.cond);
    pthread_mutex_unlock(&g_pwr.lock);
}

static void a1000_power_init(struct power_module *module)
{
    (void)module;

    pthread_mutex_lock(&g_pwr.lock);
    if (!g_pwr.inited) {
        pthread_condattr_t attr;

        pthread_condattr_init(&attr);
        pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        pthread_cond_init(&g_pwr.cond, &attr);
        pthread_condattr_destroy(&attr);

        /*
         * Текущие значения не читаем: приводим политику к известному состоянию,
         * иначе первое же apply_locked() может решить, что писать нечего.
         */
        sysfs_write_int(CPUFREQ_MAX, FREQ_MAX);
        sysfs_write_int(CPUFREQ_MIN, FREQ_MIN);
        g_pwr.cur_max = FREQ_MAX;
        g_pwr.cur_min = FREQ_MIN;

        if (pthread_create(&g_pwr.boost_thread, NULL, boost_thread_fn, NULL) != 0) {
            ALOGE("не создать поток снятия буста: %s", strerror(errno));
        }
        g_pwr.inited = 1;
        ALOGI("power HAL инициализирован: %d..%d кГц", FREQ_MIN, FREQ_MAX);
    }
    pthread_mutex_unlock(&g_pwr.lock);
}

static void a1000_set_interactive(struct power_module *module, int on)
{
    (void)module;

    pthread_mutex_lock(&g_pwr.lock);
    g_pwr.screen_on = on ? 1 : 0;
    if (!on) {
        /* Экран погас — держать буст незачем. */
        g_pwr.boost_freq = 0;
        g_pwr.boost_until_ms = 0;
    }
    apply_locked();
    pthread_cond_signal(&g_pwr.cond);
    pthread_mutex_unlock(&g_pwr.lock);
}

static void a1000_power_hint(struct power_module *module, power_hint_t hint,
                             void *data)
{
    (void)module;

    switch (hint) {
    case POWER_HINT_INTERACTION:
        /*
         * Касание. data (если есть) — ожидаемая длительность в мс, но полагаться
         * на неё нельзя: разные вызывающие передают то NULL, то длительность.
         */
        boost(FREQ_INTERACTIVE, BOOST_MS_INTERACTION);
        break;

    case POWER_HINT_LAUNCH:
        /* Запуск приложения — самый тяжёлый момент, даём максимум надолго. */
        boost(FREQ_LAUNCH, BOOST_MS_LAUNCH);
        break;

    case POWER_HINT_LOW_POWER:
        pthread_mutex_lock(&g_pwr.lock);
        g_pwr.low_power = (data != NULL) ? 1 : 0;
        apply_locked();
        pthread_mutex_unlock(&g_pwr.lock);
        break;

    /*
     * VSYNC приходит на каждый кадр — реагировать на него частотой нельзя,
     * иначе камень не опустится никогда. Остальные подсказки этому железу
     * нечего предложить.
     */
    case POWER_HINT_VSYNC:
    default:
        break;
    }
}

static struct hw_module_methods_t power_module_methods = {
    .open = NULL,
};

struct power_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = POWER_MODULE_API_VERSION_0_2,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = POWER_HARDWARE_MODULE_ID,
        .name = "A1000 (sc8830) power HAL",
        .author = "A1000 port",
        .methods = &power_module_methods,
    },
    .init = a1000_power_init,
    .setInteractive = a1000_set_interactive,
    .powerHint = a1000_power_hint,
    /*
     * Остальные поля (setFeature, getFeature, get_platform_low_power_stats,
     * get_number_of_platform_modes, get_voter_list) обнуляются designated
     * initializers. Явно их не перечисляем: набор полей отличается между
     * версиями power.h, и жёсткий список ломает сборку.
     */
};
