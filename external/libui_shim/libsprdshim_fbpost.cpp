/*
 * libsprdshim_fbpost.cpp — A1000: перехват framebuffer HAL вокруг вывода кадра.
 *
 * ПОЧЕМУ ПЕРЕХВАТ ИМЕННО compositionComplete (разобрано дизассемблером 2026-07-27):
 * в framebuffer_device_open видно, чем заполняются указатели:
 *   2988  str.w r3,[r7,#144] ; 0x90 post                r3 = add r3,pc -> ЛОКАЛЬНЫЙ
 *   298c  ldr   r6,[r6,r4]   ;                          <- ЗАГРУЗКА ИЗ GOT
 *   298e  str.w r6,[r7,#148] ; 0x94 compositionComplete
 * post статический -> LD_PRELOAD его не перебивает (проверено: 0 срабатываний).
 * А compositionComplete идёт через GOT, значит это настоящий экспортируемый символ
 * и перехват работает. SF зовёт его каждый кадр И ПЕРЕДАЁТ НАМ САМ dev — а через
 * dev до статического post всё-таки можно дотянуться: перезаписать указатель в
 * структуре. Этим и пользуется «ноль копирования» ниже.
 *
 * ЧТО ДЕЛАЕТ ШТАТНЫЙ post. В gralloc.sc8830.so fb_post проверяет флаг «буфер
 * принадлежит fb-устройству»:
 *   20a0  ands.w r7, r0, #1
 *   20a4  beq 2136                 -> НЕ наш буфер
 *   20f4  blx ioctl (0x4601 FBIOPUT_VSCREENINFO)   <- pan, ТОЛЬКО для «своих»
 * Наш FB-таргет — обычный ION (HW_FB в Android 8.1 не поддерживается gralloc1on0),
 * поэтому идёт запасная ветка 0x2136:
 *   lock(src); lock(fb); memcpy; unlock; unlock; return    <- НИ ОДНОГО ioctl
 * Этот memcpy и есть главный тормоз: 2026-08-20 simpleperf насчитал 80.65 % всех
 * тактов surfaceflinger именно в нём. Читается кадр 1.5 МБ из НЕкешированной
 * памяти overlay-heap'а ION — 63 МБ/с, 24 мс на кадр (fbbench). Запись в fb при
 * этом идёт 1104 МБ/с: платим целиком за чтение.
 *
 * ВНИМАНИЕ: pan, ожидание vsync и двойная буферизация, которые лежали здесь
 * раньше, в установленной libui_shim.so ОТСУТСТВУЮТ (проверено strings) — это
 * были неразвёрнутые эксперименты. Живой pan делает сам SurfaceFlinger по
 * debug.sf.a1000_pan / debug.sf.a1000_pan_vsync. Поэтому отсюда они убраны:
 * иначе пересборка шима молча меняет поведение устройства.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <time.h>
#include <cutils/native_handle.h>
#include <cutils/properties.h>
#include <hardware/hardware.h>
#include <hardware/fb.h>
#include <hardware/gralloc.h>
#include <sys/mman.h>
/* Без LOG_TAG тег уходит пустым и строки до logcat не доезжают — именно поэтому
 * анонс перехвата никогда не было видно. */
#define LOG_TAG "A1000zc"
#include <log/log.h>

typedef int (*compcomplete_t)(struct framebuffer_device_t *);
typedef int (*post_t)(struct framebuffer_device_t *, buffer_handle_t);

static int g_fbfd = -2;

static void a1000_fbopen(void)
{
    if (g_fbfd == -2)
        g_fbfd = open("/dev/graphics/fb0", O_RDWR);
}

/* ===== ноль копирования: DISPC сканирует FB-таргет напрямую =================
 *
 * FB-таргет SurfaceFlinger выделяется из ion_heap_carveout_overlay, то есть он
 * физически непрерывен и DISPC умеет сканировать его сам — ровно для этого heap
 * и существует. Значит копировать кадр в /dev/graphics/fb0 не нужно вовсе:
 * достаточно отдать ядру физический адрес буфера.
 *
 * Последовательность у sprdfb та же, какой пользуется стоковый HWC:
 *   SPRD_FB_SET_OVERLAY (overlay_info)        -> overlay_open + overlay_osd_configure,
 *                                                кладёт адрес в DISPC_OSD_BASE_ADDR
 *   SPRD_FB_DISPLAY_OVERLAY (overlay_display) -> overlay_start + dispc_run
 *
 * rb_switch ядро переопределяет параметром модуля osd_rb_ov (=1, см.
 * a1000_rbswitch.rc), поэтому что передавать здесь — неважно. Перестановку
 * каналов панели чинит шейдер RenderEngine по debug.sf.swz — как и в любом
 * другом оверлейном пути.
 *
 * Включение: debug.sf.a1000_zerocopy=1. При ЛЮБОЙ осечке молча откатываемся на
 * штатный post, так что худший случай — прежняя скорость, а не чёрный экран. */
#define SPRD_FB_IOCTL_MAGIC        'm'
#define SPRD_FB_SET_OVERLAY        _IOW(SPRD_FB_IOCTL_MAGIC, 1, unsigned int)
#define SPRD_FB_DISPLAY_OVERLAY    _IOW(SPRD_FB_IOCTL_MAGIC, 2, unsigned int)
#define SPRD_LAYER_OSD             0x2
#define SPRD_DATA_FORMAT_RGB888    3
#define SPRD_OVERLAY_DISPLAY_ASYNC 0
#define SPRD_OVERLAY_DISPLAY_SYNC  1
/* A1000, ядро #80: сменить ТОЛЬКО адрес сканирования и дождаться
 * защёлкивания. См. sprdfb_dispc_pageflip() в sprdfb_dispc.c. */
#define SPRD_FB_PAGEFLIP           _IOW(SPRD_FB_IOCTL_MAGIC, 20, unsigned int)

typedef struct { uint16_t x, y, w, h; } ov_rect_t;
typedef struct {                      /* kernel struct overlay_info */
    int32_t   layer_index;
    int32_t   data_type;
    int32_t   y_endian;
    int32_t   uv_endian;
    uint8_t   rb_switch;              /* kernel bool */
    ov_rect_t rect;
    uint32_t  buffer;                 /* ФИЗИЧЕСКИЙ адрес */
} ov_info_t;
typedef struct {                      /* kernel struct overlay_display */
    int32_t   layer_index;
    ov_rect_t rect;
    int32_t   display_mode;
} ov_display_t;

/* SPRD ION: физический адрес по dma-buf fd.
 * include/video/ion_sprd.h: struct ion_phys_data {int fd_buffer; unsigned long phys; size_t size;}
 * ION_SPRD_CUSTOM_PHYS = 0, ION_IOC_CUSTOM = _IOWR('I',6,ion_custom_data). */
struct a1000_ion_phys   { int fd_buffer; unsigned long phys; unsigned int size; };
struct a1000_ion_custom { unsigned int cmd; unsigned long arg; };
#define A1000_ION_IOC_CUSTOM 0xC0084906u

static int g_ionfd = -2;

static unsigned long a1000_phys_of(buffer_handle_t h, unsigned int want)
{
    const native_handle_t *nh = (const native_handle_t *)h;
    int i;

    if (!nh || nh->numFds <= 0)
        return 0;
    if (g_ionfd == -2)
        g_ionfd = open("/dev/ion", O_RDWR);
    if (g_ionfd < 0)
        return 0;

    /* Раскладка private_handle_t вендорная и от версии к версии плавает, поэтому
     * не гадаем со смещением поля, а пробуем все fd дескриптора: настоящий ION
     * ответит на запрос, остальные вернут ошибку. */
    for (i = 0; i < nh->numFds; i++) {
        struct a1000_ion_phys   d;
        struct a1000_ion_custom c;
        d.fd_buffer = nh->data[i];
        d.phys = 0;
        d.size = 0;
        c.cmd = 0;                     /* ION_SPRD_CUSTOM_PHYS */
        c.arg = (unsigned long)&d;
        if (ioctl(g_ionfd, A1000_ION_IOC_CUSTOM, &c) == 0 && d.phys && d.size >= want)
            return d.phys;
    }
    return 0;
}

static post_t g_real_post = NULL;
static unsigned g_zc_frame = 0;        /* байт в кадре */
static int g_zc_w = 0, g_zc_h = 0;
static int g_zc_off = 0;               /* осечка -> больше не пытаемся */
static int g_zc_logged = 0;
static int g_zc_sync = 1;              /* debug.sf.a1000_zcsync */
static int g_zc_fence = 1;             /* debug.sf.a1000_zcfence */
static int g_zc_flip = 0;              /* слой настроен -> короткий путь */
static int g_zc_noflip = 0;            /* ядро без SPRD_FB_PAGEFLIP */
static const gralloc_module_t *g_gralloc = NULL;

/* Ждём кадровый импульс панели — это и есть момент, когда DISPC защёлкивает
 * записанный нами DISPC_OSD_BASE_ADDR (BIT(4) DISPC_DPI_CTRL сброшен -> «update
 * with SW & VSync», см. sprdfb_dispc.c).
 *
 * Предохранитель: если импульсов нет (ранняя загрузка, панель в переходе),
 * ядерное ожидание упирается в таймаут 100 мс. Пять таких подряд — выключаемся,
 * чтобы не тормозить каждый кадр. */
static void a1000_wait_vsync(void)
{
    __u32 crt = 0;
    struct timespec t0, t1;
    static int slow_in_row = 0;
    long ms;

    if (!g_zc_sync || g_fbfd < 0)
        return;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (ioctl(g_fbfd, FBIO_WAITFORVSYNC, &crt) < 0) {
        ALOGE("FBIO_WAITFORVSYNC не поддержан (%s), дальше без ожидания", strerror(errno));
        g_zc_sync = 0;
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    if (ms >= 50) {
        if (++slow_in_row >= 5) {
            ALOGE("vsync не идёт (%ld мс x5), ожидание выключено", ms);
            g_zc_sync = 0;
        }
    } else {
        slow_in_row = 0;
    }
}

/* Одна строка на каждые 600 осечек: иначе погашенный экран зальёт logcat. */
static void a1000_log_rare(const char *fmt, const char *arg)
{
    static unsigned n = 0;
    if ((n++ % 600) == 0)
        ALOGE(fmt, arg);
}

/* Дождаться, пока GPU допишет кадр.
 *
 * Штатный fb_post начинался с lock(src) — и именно этот вызов синхронизировал
 * нас с отрисовкой. «Ноль копирования» его выбросил вместе с memcpy, и DISPC
 * принимался сканировать наполовину нарисованный буфер: картинка рвалась ПО
 * СОДЕРЖИМОМУ (верх нового кадра, низ предыдущего), а не по строкам развёртки.
 *
 * Копировать по-прежнему ничего не надо — нужна только синхронизация, поэтому
 * запираем область 1x1: цена вызова, а не цена кадра.
 * Выключается debug.sf.a1000_zcfence=0. */
static void a1000_sync_gpu(buffer_handle_t buffer)
{
    void *va = NULL;

    if (!g_zc_fence || !g_gralloc || !g_gralloc->lock)
        return;
    if (g_gralloc->lock(g_gralloc, buffer, GRALLOC_USAGE_SW_READ_RARELY,
                        0, 0, 1, 1, &va) == 0)
        g_gralloc->unlock(g_gralloc, buffer);
}

static int a1000_overlay_post(struct framebuffer_device_t *dev, buffer_handle_t buffer)
{
    ov_info_t     oi;
    ov_display_t  od;
    unsigned long phys;

    if (g_zc_off || g_fbfd < 0)
        goto fallback;

    /* Отсутствие физического адреса — свойство постоянное: буфер лежит не
     * в том heap'е, из которого DISPC умеет сканировать. Тут выключаемся совсем. */
    phys = a1000_phys_of(buffer, g_zc_frame);
    if (!phys) {
        ALOGE("у буфера нет физического адреса, режим выключен");
        g_zc_off = 1;
        goto fallback;
    }

    a1000_sync_gpu(buffer);

    /* Короткий путь: слой уже настроен, меняем только адрес.
     *
     * Длинный путь (SET_OVERLAY + DISPLAY_OVERLAY) заново открывает и
     * настраивает слой на КАЖДЫЙ кадр, а в ядре DISPLAY_OVERLAY вдобавок
     * гасит слой OSD и включает обратно уже внутри overlay_start(). Между
     * этими записями идёт живая развёртка — отсюда рвань. Нужен он только
     * один раз, чтобы задать размер и формат. */
    if (g_zc_flip && !g_zc_noflip) {
        uint32_t p32 = (uint32_t)phys;
        int rc = ioctl(g_fbfd, SPRD_FB_PAGEFLIP, &p32);
        if (rc == 0)
            return 0;              /* в ядре уже дождались защёлкивания */
        if (errno == ENOTTY) {
            ALOGE("ядро без SPRD_FB_PAGEFLIP, работаем длинным путём");
            g_zc_noflip = 1;
        }
        g_zc_flip = 0;             /* панель гасла или слой сброшен -> настроим заново */
    }

    memset(&oi, 0, sizeof oi);
    oi.layer_index = SPRD_LAYER_OSD;
    oi.data_type   = SPRD_DATA_FORMAT_RGB888;
    oi.rb_switch   = 0;                /* ядро переопределит через osd_rb_ov */
    oi.rect.w      = (uint16_t)g_zc_w;
    oi.rect.h      = (uint16_t)g_zc_h;
    oi.buffer      = (uint32_t)phys;
    /* Осечка бывает штатной и ВРЕМЕННОЙ: пока панель погашена, ядро отвечает
     * -EPERM (dev->enable == 0). Поэтому на этот кадр откатываемся на
     * копирование, но режим НЕ выключаем — иначе один перезапуск при спящем
     * экране гасит ускорение до перезагрузки. */
    if (ioctl(g_fbfd, SPRD_FB_SET_OVERLAY, &oi) != 0) {
        a1000_log_rare("SET_OVERLAY не прошёл (%s), кадр через копирование", strerror(errno));
        goto fallback;
    }

    memset(&od, 0, sizeof od);
    od.layer_index  = SPRD_LAYER_OSD;
    od.rect.w       = (uint16_t)g_zc_w;
    od.rect.h       = (uint16_t)g_zc_h;
    /* Режим SYNC для DPI-панели — пустышка: и sprdfb_dispc_display_overlay, и
     * dispc_run пропускают dispc_sync, когда panel_if_type == DPI. Ставим ASYNC
     * честно и ждём сами. */
    od.display_mode = SPRD_OVERLAY_DISPLAY_ASYNC;
    if (ioctl(g_fbfd, SPRD_FB_DISPLAY_OVERLAY, &od) != 0) {
        a1000_log_rare("DISPLAY_OVERLAY не прошёл (%s), кадр через копирование", strerror(errno));
        goto fallback;
    }

    /* ЗДЕСЬ, А НЕ ДО. Это классический page flip, и без него кадр рвётся.
     *
     * DISPLAY_OVERLAY только ВЗВОДИТ обновление регистров (BIT(5) DISPC_DPI_CTRL),
     * а защёлкнется оно на ближайшем кадровом импульсе. Возврат из post означает
     * для SurfaceFlinger «кадр отдан»: FramebufferSurface::onFrameCommitted тут же
     * возвращает предыдущий буфер в очередь. Буферов у FB-таргета три
     * (NUM_FRAMEBUFFER_SURFACE_BUFFERS=3), из них один показывается и один
     * отрисовывается, так что освобождённый берут в работу СЛЕДУЮЩИМ же кадром —
     * а DISPC до сих пор выдаёт на панель именно его. GPU рисует поверх того, что
     * сканируется. При этом счётчики темпа чистые: приложение-то успевает.
     *
     * Ожидание здесь даёт заодно обратную связь всему конвейеру: ровно один post
     * на импульс, без биения между темпом SurfaceFlinger и 60.001 Гц панели.
     *
     * debug.sf.a1000_zcsync=0 выключает — только для сравнения глазом. */
    if (g_zc_sync)
        a1000_wait_vsync();

    g_zc_flip = 1;                 /* слой настроен: дальше короткий путь */

    if (!g_zc_logged) {
        ALOGE("кадр %dx%d выведен из ION напрямую (phys=%08lx), memcpy больше нет",
              g_zc_w, g_zc_h, phys);
        g_zc_logged = 1;
    }
    return 0;                          /* настоящий post не зовём: копирования нет */

fallback:
    return g_real_post ? g_real_post(dev, buffer) : 0;
}

/* ===== честный период кадра вместо выдуманного ядром =======================
 *
 * sprdfb_main.c:450 считает fb_var.pixclock из ЗАПРОШЕННЫХ panel->fps (60), а не
 * из того, что реально получилось. А получается другое: dpi_clk квантуется целым
 * делителем (dispc_check_new_clk: divider = ROUND(pclk_src, pclk)), и для 480x800
 * с порчами 620x826 вместо нужных 30.73 МГц выходит 32 МГц — панель идёт
 * 62.49 Гц. Замерено по кадровым импульсам DISPC: 3772 импульса за 60.36 с.
 *
 * Дальше gralloc считает частоту из pixclock стоковой формулой
 *   refresh = 1e15 / (yres * xres * pixclock)      (поля margin ядро держит в 0)
 * и отдаёт SurfaceFlinger 16666389 нс = 60.001 Гц. Эти 60.001 уезжают в
 * DisplayInfo, оттуда в Choreographer КАЖДОГО приложения — и вся анимация
 * считает шаг по времени, которого у панели нет. Кадры при этом идут ровно
 * (287 flips за свайп и ровно столько же update-done, ни одного потерянного),
 * поэтому приборы чистые, а глаз видит рывки.
 *
 * Чиним на выходе из FBIOGET_VSCREENINFO: меряем настоящий период по импульсам
 * и пересчитываем pixclock обратно. Именно МЕРЯЕМ, а не зашиваем число: панелей
 * у A1000 две (ILI9805 и ILI9806E) с разными порчами.
 *
 * Выключается debug.sf.a1000_truefps=0. */
static long a1000_measure_period(int fd)
{
    struct timespec t0, t1;
    __u32 crt = 0;
    long ns;
    int i;

    if (ioctl(fd, FBIO_WAITFORVSYNC, &crt) < 0)   /* выровняться по импульсу */
        return 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 8; i++)
        if (ioctl(fd, FBIO_WAITFORVSYNC, &crt) < 0)
            return 0;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ns = ((t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec)) / 8;
    /* 8..30 мс. Всё, что вне, — панель ещё не идёт (ядерное ожидание отвалилось
     * по таймауту 100 мс). Тогда лучше НЕ трогать: пусть остаётся как было. */
    return (ns > 8000000L && ns < 30000000L) ? ns : 0;
}

extern "C" void a1000_fix_vsync_period(int fd, int request, void *arg)
{
    static long period = 0;
    static int on = -1;
    struct fb_var_screeninfo *vi = (struct fb_var_screeninfo *)arg;
    unsigned long px;

    if (request != (int)FBIOGET_VSCREENINFO || !arg)
        return;
    if (on == -1) {
        char v[PROPERTY_VALUE_MAX];
        property_get("debug.sf.a1000_truefps", v, "1");
        on = atoi(v) ? 1 : 0;
    }
    if (!on)
        return;
    if (period <= 0)                   /* не вышло — пробуем на следующем вызове */
        period = a1000_measure_period(fd);
    if (period <= 0 || !vi->xres || !vi->yres)
        return;

    /* 64 бита обязательны: период в наносекундах, умноженный на 1000, в
     * unsigned long ARM32 не влезает — переполнение давало «320 Гц». */
    px = (unsigned long)((unsigned long long)period * 1000ULL /
                         ((unsigned long long)vi->xres * vi->yres));
    if (px > 1000 && px < 1000000 && px != vi->pixclock) {
        ALOGE("период кадра %ld нс (%lld мГц), pixclock %u -> %lu",
              period, 1000000000000LL / period, vi->pixclock, px);
        vi->pixclock = (uint32_t)px;
    }
}

/* Подменяем указатель post в самой структуре устройства: статическую функцию
 * gralloc'а LD_PRELOAD не перебивает, а поле структуры — обычная память.
 *
 * Зовёт это SurfaceFlinger сразу после framebuffer_open (HWComposer_hwc1.cpp)
 * через dlsym(RTLD_DEFAULT): compositionComplete на этом устройстве не годится —
 * при HWC 1.1 HWComposer::fbCompositionComplete() выходит раньше, чем дойдёт до
 * fb-устройства, и перехват не срабатывает ни разу. */
static void a1000_patch_post(struct framebuffer_device_t *dev)
{
    struct fb_var_screeninfo vi;

    a1000_fbopen();
    if (!dev || dev->post == a1000_overlay_post || g_fbfd < 0)
        return;
    if (ioctl(g_fbfd, FBIOGET_VSCREENINFO, &vi) != 0)
        return;

    g_zc_w     = (int)vi.xres;
    g_zc_h     = (int)vi.yres;
    g_zc_frame = vi.xres * vi.yres * 4;
    g_real_post = dev->post;
    dev->post   = a1000_overlay_post;
    ALOGE("post перехвачен (было %p), кадр %dx%d", (void *)g_real_post, g_zc_w, g_zc_h);
}

/* Своего fb-устройства SurfaceFlinger не сохраняет: при HWC 1.1 он его тут же
 * закрывает (framebuffer_close, HWComposer_hwc1.cpp), а вендорный HWC открывает
 * СВОЁ — и кадр в память копирует именно оно. Поэтому подменяем не экземпляр, а
 * фабрику: gralloc-модуль один на процесс, и его methods->open отдаёт каждое
 * fb-устройство. Указатель лежит в .data.rel.ro, после RELRO он только на
 * чтение — снимаем защиту со страницы. */
typedef int (*hw_open_t)(const struct hw_module_t *, const char *, struct hw_device_t **);
static hw_open_t g_real_hw_open = NULL;

static int a1000_gralloc_open(const struct hw_module_t *m, const char *id,
                              struct hw_device_t **device)
{
    int rc = g_real_hw_open ? g_real_hw_open(m, id, device) : -1;
    if (rc == 0 && device && *device && id && strcmp(id, GRALLOC_HARDWARE_FB0) == 0)
        a1000_patch_post((struct framebuffer_device_t *)*device);
    return rc;
}

static void a1000_hook_gralloc_factory(void)
{
    typedef int (*hw_get_module_t)(const char *, const struct hw_module_t **);
    hw_get_module_t get_mod;
    const struct hw_module_t *m = NULL;
    hw_open_t *slot;
    long page = sysconf(_SC_PAGESIZE);
    void *pg;

    if (g_real_hw_open)
        return;
    /* через dlsym, чтобы шим не тянул libhardware в процессы, где её нет */
    get_mod = (hw_get_module_t)dlsym(RTLD_DEFAULT, "hw_get_module");
    if (!get_mod || get_mod(GRALLOC_HARDWARE_MODULE_ID, &m) != 0 || !m || !m->methods)
        return;

    slot = (hw_open_t *)&m->methods->open;
    pg = (void *)((unsigned long)slot & ~(unsigned long)(page - 1));
    if (mprotect(pg, (size_t)page, PROT_READ | PROT_WRITE) != 0) {
        ALOGE("mprotect на methods->open не прошёл: %s", strerror(errno));
        return;
    }
    g_gralloc = (const gralloc_module_t *)m;   /* нужен для a1000_sync_gpu */
    g_real_hw_open = *slot;
    *slot = a1000_gralloc_open;
    ALOGE("фабрика gralloc перехвачена (open было %p)", (void *)g_real_hw_open);
}

/* Зовётся из перехвата ioctl на КАЖДЫЙ ioctl процесса, поэтому первым делом
 * дешёвый выход: свойство читаем один раз, фабрику цепляем один раз. */
extern "C" void a1000_zc_hook_fb(struct framebuffer_device_t *dev)
{
    static int zc = -1;

    if (zc == 0 || (zc == 1 && g_real_hw_open && !dev))
        return;
    if (zc == -1) {
        char v[PROPERTY_VALUE_MAX];
        property_get("debug.sf.a1000_zerocopy", v, "0");
        zc = atoi(v) ? 1 : 0;
        if (!zc)
            return;
        property_get("debug.sf.a1000_zcsync", v, "1");
        g_zc_sync = atoi(v) ? 1 : 0;
        property_get("debug.sf.a1000_zcfence", v, "1");
        g_zc_fence = atoi(v) ? 1 : 0;
    }

    a1000_patch_post(dev);          /* экземпляр самого SF, если он ещё жив */
    a1000_hook_gralloc_factory();   /* и все будущие, включая вендорный HWC */
}

/* gralloc собран как C++: _Z19compositionCompleteP20framebuffer_device_t */
extern "C" int shim_compositionComplete(struct framebuffer_device_t *dev)
    asm("_Z19compositionCompleteP20framebuffer_device_t");

extern "C" int shim_compositionComplete(struct framebuffer_device_t *dev)
{
    static compcomplete_t orig = NULL;
    static int announced = 0;

    if (!announced) {
        ALOGE("compositionComplete перехвачен, dev=%p", (void *)dev);
        announced = 1;
    }

    /* Про запас: если этот путь всё-таки живой (другой процесс, другой HWC),
     * подцепимся и отсюда — a1000_zc_hook_fb идемпотентна. */
    a1000_zc_hook_fb(dev);

    if (!orig)
        orig = (compcomplete_t)dlsym(RTLD_NEXT,
                   "_Z19compositionCompleteP20framebuffer_device_t");
    return orig ? orig(dev) : 0;
}
