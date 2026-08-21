/*
 * libsprdshim — compatibility shim for SPRD/Mali Android-5 graphics blobs
 * (gralloc.sc8830.so, hwcomposer.sc8830.so) running on Android 9.
 *
 * Provides the symbols that the legacy blobs need (UND) but Android 9's
 * system libraries no longer export. Inject with:
 *     patchelf --add-needed libsprdshim.so gralloc.sc8830.so
 *     patchelf --add-needed libsprdshim.so hwcomposer.sc8830.so
 * (and ship libsprdshim.so in /system/lib/ + /vendor/lib/).
 *
 * Build (arm32, Android 9 NDK or AOSP):
 *   arm-linux-androideabi-gcc -shared -fPIC -O2 libsprdshim.c -o libsprdshim.so \
 *       -lion -llog
 *
 * ===== EASY (C symbols) — implemented below =====
 *   ion_invalidate_fd        (libion API changed)
 *   android_atomic_inc/dec/add (libcutils legacy atomics removed)
 *
 * ===== HARD (C++ libui/libgui ABI A5->A9) — see libsprdshim_cpp.cpp =====
 *   _ZN7android5FenceD1Ev            android::Fence::~Fence()
 *   _ZN7android5Fence4waitEj         android::Fence::wait(uint)
 *   _ZN7android13GraphicBufferC1EjjijjP13native_handleb
 *   _ZN7android19GraphicBufferMapper4lockEPK13native_handleiRKNS_4RectEPPv
 *   _ZN7android22GraphicBufferAllocator5allocEjjiiPPK13native_handlePi
 *   _ZN7android13MemoryHeapIon21Get_phy_addr_from_ionEiPmPj  (SPRD-specific)
 */
/* self-contained: avoid libc headers so a bare arm-eabi toolchain can build it */
typedef int int32_t;

/* feed the color interposer's phys->fd table (libsprdshim_ioctl.c) */
extern void _sprdcolor_record(unsigned long phys, int fd, unsigned int len);

/* ---- ion: bridge legacy ion_invalidate_fd onto modern ion_sync_fd ---- */
/* Modern libion exports ion_sync_fd(int ion_fd, int dma_buf_fd). The old
 * SPRD gralloc called ion_invalidate_fd(int ion_fd, int handle_fd) to flush
 * caches; a cache sync is the safe equivalent.
 *
 * DO NOT "optimise" this into a no-op. It was tried on 2026-07-26 because the A1000
 * kernel never implements ION_IOC_INVALIDATE (its ioctl switch has no case 8, so the
 * original call returned -ENOTTY) and because logcat showed a stream of
 * "ion: ioctl c0084907 failed: Bad file descriptor". Stubbing it out brought the colour
 * snow straight back: gralloc's calls here mostly SUCCEED and are performing real cache
 * maintenance before the DISPC DMA reads the buffer — removing it leaves the controller
 * scanning out stale cache lines. The EBADF noise came from a different caller, our own
 * handle_osd() using a recycled fd, which is fixed in libsprdshim_ioctl.c by dup()ing it.
 */
extern int ion_sync_fd(int fd, int handle_fd);
extern int ion_share(int fd, int handle, int *share_fd);
extern int close(int fd);
extern int __android_log_print(int prio, const char *tag, const char *fmt, ...);

/* A1000: хэндл, а не дескриптор.
 *
 * Замерено: за 1869 мс скролла ядро печатает 250 раз
 *   ion_sync_for_device: the dmabuf is err dmabuf is fffffff7   (-9 = EBADF)
 * то есть ~4-5 промахов на кадр, и обслуживание кэша для этих буферов не
 * выполняется — DISPC читает устаревшие строки. Причина в том, что старый
 * SPRD-ный ion_invalidate_fd вторым аргументом получает ION-хэндл, а
 * ion_sync_fd ждёт dma-buf fd; хэндл не открытый дескриптор, отсюда EBADF.
 *
 * Порядок специально такой: сперва пробуем как раньше, поэтому вызовы, которые
 * уже работают, ведут себя абсолютно так же. Только на неудаче достаём из
 * хэндла настоящий fd через ion_share() и синхронизируем его. */
int ion_invalidate_fd(int fd, int handle_fd)
{
    static unsigned n_direct, n_shared, n_failed;
    static unsigned long last_report;
    int r, share_fd;

    r = ion_sync_fd(fd, handle_fd);
    if (r == 0) {
        n_direct++;
    } else {
        share_fd = -1;
        if (ion_share(fd, handle_fd, &share_fd) == 0 && share_fd >= 0) {
            r = ion_sync_fd(fd, share_fd);
            close(share_fd);
            if (r == 0)
                n_shared++;
            else
                n_failed++;
        } else {
            n_failed++;
        }
    }

    /* отчёт примерно раз на 300 вызовов, чтобы видеть, что реально срабатывает */
    if (++last_report >= 300) {
        last_report = 0;
        __android_log_print(4, "sprdshim",
            "ion_invalidate: прямой=%u через_share=%u неудач=%u",
            n_direct, n_shared, n_failed);
    }
    return r;
}

/* ---- legacy libcutils atomics (removed in Android 9) ---- */
/* android_atomic_* return the *previous* value. */
int32_t android_atomic_inc(volatile int32_t *addr)
{
    return __sync_fetch_and_add(addr, 1);
}

int32_t android_atomic_dec(volatile int32_t *addr)
{
    return __sync_fetch_and_sub(addr, 1);
}

int32_t android_atomic_add(int32_t value, volatile int32_t *addr)
{
    return __sync_fetch_and_add(addr, value);
}

/* ---- SPRD android::MemoryHeapIon::Get_phy_addr_from_ion (static) ----
 * legacy mangled: _ZN7android13MemoryHeapIon21Get_phy_addr_from_ionEiPmPj
 *   static int Get_phy_addr_from_ion(int fd, unsigned long* phy, unsigned int* len)
 * Issues the SPRD ION_IOC_CUSTOM / ION_SPRD_CUSTOM_PHYS ioctl to fetch the
 * physical address of an ION dma-buf fd (needed by the sprdfb overlay path).
 * Kernel: include/video/ion_sprd.h struct ion_phys_data {int fd_buffer; unsigned long phys; size_t size;}
 *         enum ION_SPRD_CUSTOM_PHYS = 0; ION_IOC_CUSTOM = _IOWR('I',6,ion_custom_data{u32 cmd; ulong arg;}). */
struct sprdshim_ion_phys_data { int fd_buffer; unsigned long phys; unsigned int size; };
struct sprdshim_ion_custom_data { unsigned int cmd; unsigned long arg; };
extern int open(const char *path, int flags, ...);
extern int close(int fd);
extern int ioctl(int fd, unsigned long request, ...);
#define SPRDSHIM_O_RDWR            2
#define SPRDSHIM_ION_SPRD_PHYS     0u            /* ION_SPRD_CUSTOM_PHYS */
#define SPRDSHIM_ION_IOC_CUSTOM    0xC0084906u   /* _IOWR('I',6,struct ion_custom_data) 32-bit */

int _sprdshim_mhi_get_phys(int fd, unsigned long *phy, unsigned int *len)
    asm("_ZN7android13MemoryHeapIon21Get_phy_addr_from_ionEiPmPj");
int _sprdshim_mhi_get_phys(int fd, unsigned long *phy, unsigned int *len)
{
    struct sprdshim_ion_phys_data data;
    struct sprdshim_ion_custom_data custom;
    int ion_fd, ret;

    data.fd_buffer = fd;
    data.phys = 0;
    data.size = 0;
    custom.cmd = SPRDSHIM_ION_SPRD_PHYS;
    custom.arg = (unsigned long)&data;

    ion_fd = open("/dev/ion", SPRDSHIM_O_RDWR);
    if (ion_fd < 0)
        return -1;
    ret = ioctl(ion_fd, SPRDSHIM_ION_IOC_CUSTOM, &custom);
    close(ion_fd);
    if (ret)
        return ret;
    if (phy)
        *phy = data.phys;
    if (len)
        *len = data.size;
    _sprdcolor_record(data.phys, fd, data.size);
    return 0;
}
