/*
 * A1000: недостающие символы для стокового camera.sc8830.so на Android 8.1.
 *
 * Здесь три независимые вещи:
 *   1. android::MemoryHeapIon — класс, которого в AOSP нет (Spreadtrum держала
 *      его в патченном libbinder.so). Переписан поверх ION-ABI ядра.
 *   2. android_atomic_inc/dec — выкинуты из libcutils в 8.1.
 *   3. Fence::wait(unsigned) — в 8.1 остался только wait(int).
 */

/* LOG_TAG обязан быть определён ДО включения log.h, иначе он задаст свой NULL. */
#define LOG_TAG "sprd_camera_shim"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <binder/MemoryHeapBase.h>
#include <ui/Fence.h>

#include <log/log.h>

/* ------------------------------------------------------------------ */
/* ABI ядра. Копия из a1000-kernel: include/linux/ion.h и               */
/* include/video/ion_sprd.h — заголовки ядра в дерево не тянем.         */
/* ------------------------------------------------------------------ */

typedef int ion_user_handle_t;

struct ion_allocation_data {
    size_t len;
    size_t align;
    unsigned int heap_id_mask;
    unsigned int flags;
    ion_user_handle_t handle;
};

struct ion_fd_data {
    ion_user_handle_t handle;
    int fd;
};

struct ion_handle_data {
    ion_user_handle_t handle;
};

struct ion_custom_data {
    unsigned int cmd;
    unsigned long arg;
};

#define ION_IOC_MAGIC   'I'
#define ION_IOC_ALLOC   _IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_FREE    _IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data)
#define ION_IOC_SHARE   _IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)
#define ION_IOC_CUSTOM  _IOWR(ION_IOC_MAGIC, 6, struct ion_custom_data)

/* enum ION_SPRD_CUSTOM_CMD из include/video/ion_sprd.h — порядок важен */
enum {
    ION_SPRD_CUSTOM_PHYS = 0,
    ION_SPRD_CUSTOM_MSYNC,
    ION_SPRD_CUSTOM_GSP_MAP,
    ION_SPRD_CUSTOM_GSP_UNMAP,
    ION_SPRD_CUSTOM_MM_MAP,
    ION_SPRD_CUSTOM_MM_UNMAP,
    ION_SPRD_CUSTOM_FENCE_CREATE,
    ION_SPRD_CUSTOM_FENCE_SIGNAL,
    ION_SPRD_CUSTOM_FENCE_DUP,
    ION_SPRD_CUSTOM_MAP,
    ION_SPRD_CUSTOM_UNMAP,
};

struct ion_phys_data {
    int fd_buffer;
    unsigned long phys;
    size_t size;
};

struct ion_mmu_data {
    int master_id;
    int fd_buffer;
    unsigned long iova_addr;
    size_t iova_size;
};

struct ion_msync_data {
    int fd_buffer;
    void *vaddr;
    void *paddr;
    size_t size;
};

/* enum ION_MASTER_ID: ION_MM идёт вторым */
#define ION_MM 1

#define ION_DEVICE "/dev/ion"

namespace android {

/*
 * Объявление совпадает со стоковым по сигнатурам (иначе не сойдутся искажённые
 * имена) и НЕ содержит собственных полей — см. заметку про 64 байта.
 */
class MemoryHeapIon : public MemoryHeapBase
{
public:
    MemoryHeapIon(const char* device, size_t size, uint32_t flags,
                  unsigned long memory_types);
    virtual ~MemoryHeapIon();

    int get_phy_addr_from_ion(unsigned long *phys, size_t *size);
    int get_mm_iova(unsigned long *iova, size_t *size);
    int free_mm_iova(unsigned long iova, size_t size);

    static int Get_phy_addr_from_ion(int fd, unsigned long *phys, size_t *size);
    static int Get_mm_iova(int fd, unsigned long *iova, size_t *size);
    static int Free_mm_iova(int fd, unsigned long iova, size_t size);
    static int Mm_iommu_is_enabled(void);
    static int flush_ion_buffer(void *vaddr, void *paddr, size_t size);
};

/* Общая обёртка над ION_IOC_CUSTOM: открыть /dev/ion, дёрнуть, закрыть. */
static int ion_custom(unsigned int cmd, void *arg)
{
    struct ion_custom_data data;
    int fd = open(ION_DEVICE, O_RDWR);
    int ret;

    if (fd < 0) {
        ALOGE("не открыть %s: %s", ION_DEVICE, strerror(errno));
        return -errno;
    }
    data.cmd = cmd;
    data.arg = (unsigned long)arg;
    ret = ioctl(fd, ION_IOC_CUSTOM, &data);
    if (ret < 0) {
        ALOGE("ION_IOC_CUSTOM cmd=%u: %s", cmd, strerror(errno));
        ret = -errno;
    }
    close(fd);
    return ret;
}

MemoryHeapIon::MemoryHeapIon(const char* device, size_t size, uint32_t flags,
                             unsigned long memory_types)
    : MemoryHeapBase()
{
    struct ion_allocation_data alloc;
    struct ion_fd_data share;
    struct ion_handle_data hd;
    void *base;
    int ion_fd;

    ion_fd = open(device ? device : ION_DEVICE, O_RDWR);
    if (ion_fd < 0) {
        ALOGE("не открыть %s: %s", device ? device : ION_DEVICE, strerror(errno));
        return;
    }

    memset(&alloc, 0, sizeof(alloc));
    alloc.len = size;
    alloc.align = 4096;
    /*
     * У Spreadtrum memory_types — это прямо маска кучи ION. Если не задана,
     * берём системную: так ведёт себя и стоковая реализация.
     */
    alloc.heap_id_mask = memory_types ? (unsigned int)memory_types : (1u << 0);
    alloc.flags = 0;

    if (ioctl(ion_fd, ION_IOC_ALLOC, &alloc) < 0) {
        ALOGE("ION_IOC_ALLOC %zu байт, маска 0x%x: %s", size,
              alloc.heap_id_mask, strerror(errno));
        close(ion_fd);
        return;
    }

    memset(&share, 0, sizeof(share));
    share.handle = alloc.handle;
    if (ioctl(ion_fd, ION_IOC_SHARE, &share) < 0) {
        ALOGE("ION_IOC_SHARE: %s", strerror(errno));
        hd.handle = alloc.handle;
        ioctl(ion_fd, ION_IOC_FREE, &hd);
        close(ion_fd);
        return;
    }

    /* Дескриптор из SHARE держит буфер сам, handle больше не нужен. */
    hd.handle = alloc.handle;
    ioctl(ion_fd, ION_IOC_FREE, &hd);
    close(ion_fd);

    base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, share.fd, 0);
    if (base == MAP_FAILED) {
        ALOGE("mmap %zu байт: %s", size, strerror(errno));
        close(share.fd);
        return;
    }

    /* init() забирает владение дескриптором. */
    init(share.fd, base, (int)size, (int)flags, device);
}

MemoryHeapIon::~MemoryHeapIon()
{
    /* Всё освобождает ~MemoryHeapBase: munmap и close дескриптора. */
}

int MemoryHeapIon::Get_phy_addr_from_ion(int fd, unsigned long *phys, size_t *size)
{
    struct ion_phys_data data;
    int ret;

    if (fd < 0 || phys == NULL || size == NULL) return -EINVAL;

    memset(&data, 0, sizeof(data));
    data.fd_buffer = fd;
    ret = ion_custom(ION_SPRD_CUSTOM_PHYS, &data);
    if (ret < 0) return ret;

    *phys = data.phys;
    *size = data.size;
    return 0;
}

int MemoryHeapIon::Get_mm_iova(int fd, unsigned long *iova, size_t *size)
{
    struct ion_mmu_data data;
    int ret;

    if (fd < 0 || iova == NULL || size == NULL) return -EINVAL;

    memset(&data, 0, sizeof(data));
    data.master_id = ION_MM;
    data.fd_buffer = fd;
    ret = ion_custom(ION_SPRD_CUSTOM_MM_MAP, &data);
    if (ret < 0) return ret;

    /* Драйвер возвращает и адрес, и фактический размер буфера. */
    *iova = data.iova_addr;
    *size = data.iova_size;
    return 0;
}

int MemoryHeapIon::Free_mm_iova(int fd, unsigned long iova, size_t size)
{
    struct ion_mmu_data data;

    if (fd < 0) return -EINVAL;

    memset(&data, 0, sizeof(data));
    data.master_id = ION_MM;
    data.fd_buffer = fd;
    data.iova_addr = iova;
    data.iova_size = size;
    return ion_custom(ION_SPRD_CUSTOM_MM_UNMAP, &data);
}

int MemoryHeapIon::Mm_iommu_is_enabled(void)
{
    /*
     * IOMMU на этом железе есть (в ядре узел sprd_iommu_mm), и стоковый
     * тракт камеры рассчитан именно на него. Отдельного узла «включён ли он»
     * ядро не отдаёт, поэтому отвечаем утвердительно — как и стоковая
     * реализация на устройстве с IOMMU_MM.
     */
    return 1;
}

int MemoryHeapIon::flush_ion_buffer(void *vaddr, void *paddr, size_t size)
{
    struct ion_msync_data data;

    /*
     * Драйвер требует выровненный на страницу vaddr и иначе возвращает
     * -EFAULT (см. ION_SPRD_CUSTOM_MSYNC в sprd_ion.c).
     */
    if (vaddr == NULL) return -EINVAL;
    if ((unsigned long)vaddr & 0xFFF) {
        ALOGE("flush_ion_buffer: vaddr %p не выровнен на страницу", vaddr);
        return -EFAULT;
    }

    memset(&data, 0, sizeof(data));
    data.fd_buffer = -1;
    data.vaddr = vaddr;
    data.paddr = paddr;
    data.size = size;
    return ion_custom(ION_SPRD_CUSTOM_MSYNC, &data);
}

/* Методы экземпляра — тонкие обёртки: своего состояния у класса нет. */
int MemoryHeapIon::get_phy_addr_from_ion(unsigned long *phys, size_t *size)
{
    return Get_phy_addr_from_ion(getHeapID(), phys, size);
}

int MemoryHeapIon::get_mm_iova(unsigned long *iova, size_t *size)
{
    return Get_mm_iova(getHeapID(), iova, size);
}

int MemoryHeapIon::free_mm_iova(unsigned long iova, size_t size)
{
    return Free_mm_iova(getHeapID(), iova, size);
}

} /* namespace android */

/* ------------------------------------------------------------------ */
/* Мелочь: символы, выпавшие из 8.1                                     */
/* ------------------------------------------------------------------ */

extern "C" {

/* Были в libcutils до 8.1. */
int32_t android_atomic_inc(volatile int32_t *addr)
{
    return __sync_fetch_and_add(addr, 1);
}

int32_t android_atomic_dec(volatile int32_t *addr)
{
    return __sync_fetch_and_sub(addr, 1);
}

/*
 * Fence::wait(unsigned int) -> Fence::wait(int).
 * Имя задано искажённым напрямую: добавить метод в чужой класс libui иначе
 * нельзя, а подпись отличается только знаковостью аргумента.
 */
int _ZN7android5Fence4waitEj(void *self, unsigned int timeout)
{
    android::Fence *fence = reinterpret_cast<android::Fence *>(self);
    return fence->wait(static_cast<int>(timeout));
}

} /* extern "C" */
