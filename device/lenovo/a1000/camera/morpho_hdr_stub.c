/*
 * A1000: переходник к настоящей стоковой libmorpho_easy_hdr (см. соседний
 * face_finder_stub.c — там же пояснение про сигнатуры и мини-загрузчик).
 */
#define LOG_TAG "morpho_hdr_shim"

#include <stddef.h>
#include <log/log.h>

#include "elfload.h"

#define REAL_PATH "/system/lib/libmorpho_easy_hdr_real.so"

typedef int (*fn8_t)(int, int, int, int, int, int, int, int);

static fn8_t g_hdr;

__attribute__((constructor)) static void load_real(void)
{
    static const char *const want[] = { "HDR_Function" };
    void *addr[1];

    if (elfload_open(REAL_PATH, want, addr, 1) == NULL) {
        ALOGE("HDR недоступен: не загрузился " REAL_PATH);
        return;
    }
    g_hdr = (fn8_t)addr[0];
    ALOGI("libmorpho_easy_hdr загружена, HDR доступен");
}

int HDR_Function(int a, int b, int c, int d, int e, int f, int g, int h)
{
    if (g_hdr == NULL) return -1;
    return g_hdr(a, b, c, d, e, f, g, h);
}
