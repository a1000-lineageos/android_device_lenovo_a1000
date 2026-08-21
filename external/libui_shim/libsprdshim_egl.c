/*
 * libsprdshim_egl.c — teach the Android-5 vendor ANativeWindow to satisfy Android 8.1's EGL.
 *
 * WHY THIS EXISTS
 * ---------------
 * hwcomposer.sc8830.so hands every frame the GSP cannot handle in hardware — i.e. every
 * multi-layer frame: SetupWizard, the launcher, anything with a wallpaper under a window —
 * to its own in-process GLES compositor, OverlayComposer, which draws into an
 * OverlayNativeWindow backed by the DISPC OSD plane (SprdHWLayerList::revisitGeometry ->
 * revisitOverlayComposerLayer). Single-layer frames (the boot animation) bypass it, which
 * is why they always looked correct.
 *
 * OverlayNativeWindow::query() only answers `what` in [2..10] (the Android 5 attribute set);
 * anything else returns BAD_VALUE *without writing *value*. Android 8.1 opens
 * eglCreateWindowSurface() with:
 *
 *      int value = 0;
 *      window->query(window, NATIVE_WINDOW_IS_VALID, &value);   // NATIVE_WINDOW_IS_VALID == 17
 *      if (!value) return setError(EGL_BAD_NATIVE_WINDOW, EGL_NO_SURFACE);
 *      (frameworks/native/opengl/libs/EGL/eglApi.cpp:685)
 *
 * so the surface is never created ("eglCreateWindowSurface:685 error 300b", then
 * "SPRDHWComposer: Init EGL ENV failed"). OverlayComposer keeps running without a GLES
 * context: it dequeues an ION buffer, renders nothing into it, and presents it. What
 * reaches the panel is uninitialised memory — the colour "snow" and black rectangles.
 *
 * The fix is to answer the attributes that postdate Android 5. We interpose
 * eglCreateWindowSurface (this library is LD_PRELOAD'd into the composer@2.1 service) and,
 * for a window that fails the IS_VALID probe, swap in a ->query that knows them and
 * delegates everything else to the vendor implementation.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <log/log.h>

#undef LOG_TAG
#define LOG_TAG "sprdshim_egl"

/* <system/window.h> is not reachable from this module's include paths (its header library
 * has no vendor_available variant), so the two frozen public ABIs it would give us are
 * spelled out here. ANativeWindow's layout has not changed since API 9; the offset of
 * ->query is checked below against the value the vendor blob itself compiles in. */
struct shim_native_base {
    int   magic;
    int   version;
    void *reserved[4];
    void (*incRef)(struct shim_native_base *base);
    void (*decRef)(struct shim_native_base *base);
};

typedef struct shim_anw {
    struct shim_native_base common;
    const uint32_t flags;
    const int      minSwapInterval;
    const int      maxSwapInterval;
    const float    xdpi;
    const float    ydpi;
    intptr_t       oem[4];

    int (*setSwapInterval)(struct shim_anw *win, int interval);
    int (*dequeueBuffer_DEPRECATED)(struct shim_anw *win, void **buffer);
    int (*lockBuffer_DEPRECATED)(struct shim_anw *win, void *buffer);
    int (*queueBuffer_DEPRECATED)(struct shim_anw *win, void *buffer);
    int (*query)(const struct shim_anw *win, int what, int *value);
    int (*perform)(struct shim_anw *win, int operation, ...);
    int (*cancelBuffer_DEPRECATED)(struct shim_anw *win, void *buffer);
    int (*dequeueBuffer)(struct shim_anw *win, void **buffer, int *fenceFd);
    int (*queueBuffer)(struct shim_anw *win, void *buffer, int fenceFd);
    int (*cancelBuffer)(struct shim_anw *win, void *buffer, int fenceFd);
} shim_anw;

/* OverlayComposer::initEGL() calls the window's query through `ldr r3, [r4, #84]`. */
typedef char shim_assert_query_offset[(offsetof(shim_anw, query) == 84) ? 1 : -1];

/* Kept local so this file does not need the EGL headers; every one of these is
 * pointer- or int-sized, so the interposed symbol is ABI-identical to libEGL's. */
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLSurface;
typedef int   EGLint;
#define SHIM_EGL_NO_SURFACE ((EGLSurface)0)

#define SHIM_NATIVE_WINDOW_WIDTH             0
#define SHIM_NATIVE_WINDOW_HEIGHT            1
#define SHIM_NATIVE_WINDOW_DEFAULT_WIDTH     6
#define SHIM_NATIVE_WINDOW_DEFAULT_HEIGHT    7
#define SHIM_NATIVE_WINDOW_DEFAULT_DATASPACE 12
#define SHIM_NATIVE_WINDOW_BUFFER_AGE        13
#define SHIM_NATIVE_WINDOW_LAYER_COUNT       16
#define SHIM_NATIVE_WINDOW_IS_VALID          17
#define SHIM_HAL_DATASPACE_UNKNOWN           0

/* One entry per legacy window we adopt; in practice OverlayComposer creates exactly one. */
#define SHIM_MAX_WINDOWS 4
static struct {
    const shim_anw *win;
    int (*query)(const shim_anw *, int, int *);
} g_adopted[SHIM_MAX_WINDOWS];
static int g_adopted_count;
static int g_qlog;   /* bounded query trace */

static int shim_query(const shim_anw *win, int what, int *value)
{
    int (*orig)(const shim_anw *, int, int *) = 0;
    int i, rc;

    for (i = 0; i < g_adopted_count; i++)
        if (g_adopted[i].win == win) { orig = g_adopted[i].query; break; }

    if (g_qlog < 400 && (what == SHIM_NATIVE_WINDOW_WIDTH || what == SHIM_NATIVE_WINDOW_HEIGHT))
        ALOGI("query win=%p what=%d (WIDTH/HEIGHT) intercepted", win, what), g_qlog++;

    /* Attributes added after Android 5, which the vendor window rejects. */
    switch (what) {
    case SHIM_NATIVE_WINDOW_IS_VALID:
        *value = 1;
        return 0;
    case SHIM_NATIVE_WINDOW_LAYER_COUNT:
        *value = 1;
        return 0;
    case SHIM_NATIVE_WINDOW_BUFFER_AGE:
        *value = 0;              /* 0 == contents unknown, caller must fully redraw */
        return 0;
    case SHIM_NATIVE_WINDOW_DEFAULT_DATASPACE:
        *value = SHIM_HAL_DATASPACE_UNKNOWN;
        return 0;

    /* WIDTH(0) and HEIGHT(1) are rejected too, and that is what produced the sliver.
     * OverlayNativeWindow::query decodes only what in [2..10] (`subs r1,r7,#2; cmp r1,#8;
     * bhi -> mvn r4,#21`), returning BAD_VALUE *without writing *value*. Every EGL
     * implementation asks for exactly these two when it sizes a window surface, so the
     * driver sized the surface from an uninitialised stack slot: the scanout thumbnail
     * showed the composed frame written only into x=270..480 of a 480-wide buffer, the
     * rest left at zero, which is the black screen. DEFAULT_WIDTH(6)/DEFAULT_HEIGHT(7) sit
     * inside the range the vendor does decode and hold the same numbers, so ask for those. */
    case SHIM_NATIVE_WINDOW_WIDTH:
        if (orig && orig(win, SHIM_NATIVE_WINDOW_DEFAULT_WIDTH, value) == 0)
            return 0;
        break;
    case SHIM_NATIVE_WINDOW_HEIGHT:
        if (orig && orig(win, SHIM_NATIVE_WINDOW_DEFAULT_HEIGHT, value) == 0)
            return 0;
        break;
    default:
        break;
    }

    if (orig) {
        rc = orig(win, what, value);
        /* The composed frame lands in the buffer translated right by exactly 392 px on every
         * row (no shear, so the stride is right - only the origin is wrong), and that number
         * moved from 270 to 392 when this function started answering WIDTH/HEIGHT. So the
         * offset is computed from what this conversation returns. Record it verbatim rather
         * than keep guessing which attribute it is. */
        if (g_qlog < 400)
            ALOGI("query win=%p what=%d -> rc=%d value=%d", win, what, rc,
                  (rc == 0) ? *value : -1), g_qlog++;
        /* Never hand back an untouched *value: a caller that ignores the return code then
         * runs on stack garbage, which is the failure mode this whole file exists to fix. */
        if (rc != 0)
            *value = 0;
        return rc;
    }

    *value = 0;
    return -EINVAL;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  shim_anw *win, const EGLint *attrib_list)
{
    static EGLSurface (*real)(EGLDisplay, EGLConfig, shim_anw *, const EGLint *);
    EGLSurface surface;

    if (!real) {
        real = (EGLSurface (*)(EGLDisplay, EGLConfig, shim_anw *, const EGLint *))
                   dlsym(RTLD_NEXT, "eglCreateWindowSurface");
        /* RTLD_NEXT only searches objects loaded after this one in the same lookup scope.
         * Inside surfaceflinger that search comes up empty ("undefined symbol:
         * eglCreateWindowSurface"): the shim is LD_PRELOAD'd, so it is ahead of everything,
         * and libEGL ends up outside the scope RTLD_NEXT walks. Returning EGL_NO_SURFACE
         * there made this wrapper strictly worse than not existing - OverlayComposer went on
         * to call eglMakeCurrent with EGL_NO_SURFACE, which is EGL_BAD_MATCH by spec, logged
         * "Init EGL ENV failed", and then presented ION buffers it had never rendered into.
         * That uninitialised memory is the snow. Ask the linker for libEGL by name instead;
         * it is already mapped, so this just takes a reference to the existing object. */
        if (!real) {
            void *h = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
            if (h)
                real = (EGLSurface (*)(EGLDisplay, EGLConfig, shim_anw *, const EGLint *))
                           dlsym(h, "eglCreateWindowSurface");
            ALOGI("RTLD_NEXT missed eglCreateWindowSurface; libEGL.so fallback %s",
                  real ? "resolved it" : "failed too");
        }
        if (!real) {
            ALOGE("cannot resolve the real eglCreateWindowSurface: %s", dlerror());
            return SHIM_EGL_NO_SURFACE;
        }
    }

    if (win && win->query != shim_query) {
        int valid = 0;
        win->query(win, SHIM_NATIVE_WINDOW_IS_VALID, &valid);
        if (!valid) {
            if (g_adopted_count < SHIM_MAX_WINDOWS) {
                g_adopted[g_adopted_count].win = win;
                g_adopted[g_adopted_count].query = win->query;
                g_adopted_count++;
                win->query = shim_query;
                {
                    int w = -1, h = -1;
                    shim_query(win, SHIM_NATIVE_WINDOW_WIDTH, &w);
                    shim_query(win, SHIM_NATIVE_WINDOW_HEIGHT, &h);
                    ALOGI("adopted legacy ANativeWindow %p (pre-O query); size now %dx%d", win, w, h);
                }
            } else {
                ALOGE("legacy ANativeWindow %p not adopted: table full", win);
            }
        }
    }

    surface = real(dpy, config, win, attrib_list);
    if (surface == SHIM_EGL_NO_SURFACE)
        ALOGE("eglCreateWindowSurface still failed for window %p", win);
    return surface;
}
