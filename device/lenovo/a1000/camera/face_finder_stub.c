/*
 * A1000: не заглушка, а переходник к НАСТОЯЩЕЙ стоковой libface_finder.
 *
 * Оригинал лежит рядом под именем libface_finder_real.so и грузится нашим
 * мини-загрузчиком: bionic его не примет (не-PIC), а нам он нужен целиком.
 *
 * Про сигнатуры. Настоящих прототипов у нас нет. Объявляем переходники с
 * восемью целочисленными аргументами и передаём их дальше как есть: в ARM EABI
 * первые четыре идут в регистрах, остальные через стек, и лишние аргументы
 * вызываемая сторона просто игнорирует. Так вызов проходит при любой реальной
 * арности до восьми, не требуя знать её точно.
 */
#define LOG_TAG "face_finder_shim"

#include <stddef.h>
#include <log/log.h>

#include "elfload.h"

#define REAL_PATH "/system/lib/libface_finder_real.so"

typedef int (*fn8_t)(int, int, int, int, int, int, int, int);

static fn8_t g_init, g_func, g_fini;
static int g_ready;

__attribute__((constructor)) static void load_real(void)
{
    static const char *const want[] = {
        "FaceFinder_Init", "FaceFinder_Function", "FaceFinder_Finalize"
    };
    void *addr[3];

    if (elfload_open(REAL_PATH, want, addr, 3) == NULL) {
        ALOGE("распознавание лиц недоступно: не загрузился " REAL_PATH);
        return;
    }
    g_init = (fn8_t)addr[0];
    g_func = (fn8_t)addr[1];
    g_fini = (fn8_t)addr[2];
    g_ready = 1;
    ALOGI("libface_finder загружена, распознавание лиц доступно");
}

int FaceFinder_Init(int a, int b, int c, int d, int e, int f, int g, int h)
{
    if (!g_ready) return 0;
    return g_init(a, b, c, d, e, f, g, h);
}

int FaceFinder_Function(int a, int b, int c, int d, int e, int f, int g, int h)
{
    if (!g_ready) return 0;
    return g_func(a, b, c, d, e, f, g, h);
}

int FaceFinder_Finalize(int a, int b, int c, int d, int e, int f, int g, int h)
{
    if (!g_ready) return 0;
    return g_fini(a, b, c, d, e, f, g, h);
}
