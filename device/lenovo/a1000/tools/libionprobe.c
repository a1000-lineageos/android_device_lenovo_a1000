/*
 * A1000: диагностический перехват ioctl для сервиса аллокатора графики.
 * Только печатает; поведение не меняет.
 */
#define LOG_TAG "ionprobe"
#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <log/log.h>

/* Структуры ION — копия из ядра a1000 (include/linux/ion.h). */
struct ion_allocation_data {
    size_t len;
    size_t align;
    unsigned int heap_id_mask;
    unsigned int flags;
    int handle;
};
struct ion_fd_data { int handle; int fd; };
struct ion_handle_data { int handle; };
struct ion_custom_data { unsigned int cmd; unsigned long arg; };

#define ION_MAGIC 'I'

static int (*real_ioctl)(int, int, ...) = 0;

static const char *ion_name(unsigned nr)
{
    switch (nr) {
    case 0: return "ALLOC";
    case 1: return "FREE";
    case 2: return "MAP";
    case 3: return "IMPORT";
    case 4: return "SHARE";
    case 5: return "IMPORT/5";
    case 6: return "CUSTOM";
    case 7: return "SYNC";
    default: return "?";
    }
}

int ioctl(int fd, int request, ...)
{
    va_list ap;
    void *arg;
    int ret, err;
    unsigned type = (((unsigned)request) >> 8) & 0xff;
    unsigned nr = ((unsigned)request) & 0xff;

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    if (!real_ioctl)
        real_ioctl = (int (*)(int, int, ...))dlsym(RTLD_NEXT, "ioctl");
    if (!real_ioctl)
        return -1;

    if (type != ION_MAGIC)
        return real_ioctl(fd, request, arg);

    /* Аргументы ДО вызова: после неудачи ядро могло их не тронуть. */
    char before[160];
    before[0] = 0;
    if (nr == 0 && arg) {
        struct ion_allocation_data *a = arg;
        snprintf(before, sizeof(before),
                 "len=%zu align=%zu heap_mask=0x%x flags=0x%x",
                 a->len, a->align, a->heap_id_mask, a->flags);
    } else if ((nr == 4 || nr == 7 || nr == 2 || nr == 3) && arg) {
        struct ion_fd_data *f = arg;
        snprintf(before, sizeof(before), "handle=%d fd=%d", f->handle, f->fd);
    } else if (nr == 1 && arg) {
        struct ion_handle_data *h = arg;
        snprintf(before, sizeof(before), "handle=%d", h->handle);
    } else if (nr == 6 && arg) {
        struct ion_custom_data *c = arg;
        snprintf(before, sizeof(before), "cmd=%u", c->cmd);
    }

    ret = real_ioctl(fd, request, arg);
    err = errno;

    char after[128];
    after[0] = 0;
    if (ret == 0 && nr == 0 && arg) {
        struct ion_allocation_data *a = arg;
        snprintf(after, sizeof(after), " -> handle=%d", a->handle);
    } else if (ret == 0 && nr == 4 && arg) {
        struct ion_fd_data *f = arg;
        snprintf(after, sizeof(after), " -> fd=%d", f->fd);
    }

    if (ret != 0)
        ALOGE("ION_%s (nr=%u) %s => ОТКАЗ %d (%s)", ion_name(nr), nr, before,
              ret, strerror(err));
    else
        ALOGI("ION_%s (nr=%u) %s ок%s", ion_name(nr), nr, before, after);

    errno = err;
    return ret;
}

/*
 * ION отрабатывает успешно, а gralloc всё равно возвращает ошибку — значит
 * спотыкается уже ПОСЛЕ выделения. Первый подозреваемый — отображение
 * полученного дескриптора в память, поэтому перехватываем и mmap.
 */
static void *(*real_mmap)(void *, size_t, int, int, int, off_t) = 0;

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    void *r;
    int err;

    if (!real_mmap)
        real_mmap = (void *(*)(void *, size_t, int, int, int, off_t))
                        dlsym(RTLD_NEXT, "mmap");
    if (!real_mmap)
        return MAP_FAILED;

    r = real_mmap(addr, len, prot, flags, fd, off);
    err = errno;
    if (r == MAP_FAILED)
        ALOGE("mmap len=%zu prot=0x%x flags=0x%x fd=%d => ОТКАЗ (%s)",
              len, prot, flags, fd, strerror(err));
    else if (fd >= 0)
        ALOGI("mmap len=%zu fd=%d ок", len, fd);
    errno = err;
    return r;
}
