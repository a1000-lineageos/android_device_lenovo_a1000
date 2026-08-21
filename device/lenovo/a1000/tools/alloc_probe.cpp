/*
 * A1000: перебор сочетаний формат x usage через IAllocator@2.0.
 * Безопасно: блоб живёт в своём сервисе, мы только просим у него буферы.
 */
#define LOG_TAG "alloc_probe"

#include <stdio.h>

#include <android/hardware/graphics/allocator/2.0/IAllocator.h>
#include <android/hardware/graphics/mapper/2.0/IMapper.h>

using android::hardware::graphics::allocator::V2_0::IAllocator;
using android::hardware::graphics::mapper::V2_0::IMapper;
using android::hardware::graphics::mapper::V2_0::Error;
using android::hardware::graphics::mapper::V2_0::BufferDescriptor;
using android::hardware::hidl_vec;
using android::hardware::hidl_handle;

struct Fmt { uint32_t v; const char *name; };
struct Usg { uint64_t v; const char *name; };

/* Значения из system/core/include/system/graphics.h и gralloc.h */
static Fmt formats[] = {
    { 1,  "RGBA_8888" },
    { 2,  "RGBX_8888" },
    { 4,  "RGB_565" },
    { 17, "YCrCb_420_SP (NV21)" },
    { 35, "YCbCr_420_888" },
    { 0x32315659, "YV12" },
    { 0x22, "IMPLEMENTATION_DEFINED" },
    { 0x21, "BLOB" },
    { 0, nullptr }
};

static Usg usages[] = {
    { 0x100,       "HW_TEXTURE (0x100)" },
    { 0x200,       "HW_RENDER (0x200)" },
    { 0x300,       "HW_TEXTURE|HW_RENDER" },
    { 0x100 | 0x3, "HW_TEXTURE|SW_READ_RARELY|SW_WRITE_RARELY" },
    { 0x100 | 0x33,"HW_TEXTURE|SW_RW_OFTEN" },
    { 0x800,       "HW_COMPOSER (0x800)" },
    { 0x900,       "HW_COMPOSER|HW_TEXTURE" },
    { 0x20000,     "HW_CAMERA_WRITE" },
    { 0x20100,     "HW_CAMERA_WRITE|HW_TEXTURE" },
    { 0, nullptr }
};

int main()
{
    auto allocator = IAllocator::getService();
    auto mapper = IMapper::getService();
    if (allocator == nullptr || mapper == nullptr) {
        printf("нет сервиса: allocator=%p mapper=%p\n",
               allocator.get(), mapper.get());
        return 1;
    }

    printf("%-26s | %s\n", "usage", "форматы: отказавшие");
    printf("---------------------------------------------------------------\n");

    for (int j = 0; usages[j].name; j++) {
        printf("%-26s |", usages[j].name);
        for (int i = 0; formats[i].name; i++) {
            IMapper::BufferDescriptorInfo info{};
            info.width = 640;
            info.height = 480;
            info.layerCount = 1;
            info.format = static_cast<
                android::hardware::graphics::common::V1_0::PixelFormat>(
                    formats[i].v);
            info.usage = usages[j].v;

            BufferDescriptor desc;
            Error err = Error::NONE;
            mapper->createDescriptor(info, [&](Error e, BufferDescriptor d) {
                err = e; desc = d;
            });
            if (err != Error::NONE) {
                printf("  %s:описатель(%d)", formats[i].name, (int)err);
                continue;
            }

            Error aerr = Error::NONE;
            allocator->allocate(desc, 1,
                [&](Error e, uint32_t /*stride*/, const hidl_vec<hidl_handle>& bufs) {
                    aerr = e;
                    if (e == Error::NONE)
                        for (size_t k = 0; k < bufs.size(); k++)
                            mapper->freeBuffer(
                                const_cast<native_handle_t*>(bufs[k].getNativeHandle()));
                });
            if (aerr != Error::NONE)
                printf("  %s(код %d)", formats[i].name, (int)aerr);
        }
        printf("\n");
    }
    printf("\n(пусто справа = все форматы выделились)\n");

    /*
     * UDERZHANIE. Po odnomu vsyo vydelyaetsya dazhe kogda kamera v eto vremya
     * poluchaet otkaz. Znachit proveryaem drugoe: skol'ko buferov udayotsya
     * derzhat' ODNOVREMENNO - kamera derzhit ochered' prev'yu, a perebor vyshe
     * kazhdyy bufer srazu osvobozhdal.
     */
    printf("\n=== skolko NV21 640x480 (HW_TEXTURE) derzhitsya razom ===\n");
    {
        IMapper::BufferDescriptorInfo info2{};
        info2.width = 640; info2.height = 480; info2.layerCount = 1;
        info2.format = static_cast<
            android::hardware::graphics::common::V1_0::PixelFormat>(17);
        info2.usage = 0x100;

        BufferDescriptor desc2;
        Error err2 = Error::NONE;
        mapper->createDescriptor(info2, [&](Error e, BufferDescriptor d) {
            err2 = e; desc2 = d;
        });
        if (err2 != Error::NONE) { printf("descriptor fail: %d\n", (int)err2); return 1; }

        const int MAXN = 64;
        const native_handle_t *held[MAXN];
        int n = 0;
        for (; n < MAXN; n++) {
            Error aerr = Error::NONE;
            const native_handle_t *h = nullptr;
            allocator->allocate(desc2, 1,
                [&](Error e, uint32_t, const hidl_vec<hidl_handle>& bufs) {
                    aerr = e;
                    if (e == Error::NONE && bufs.size() > 0)
                        h = bufs[0].getNativeHandle();
                });
            if (aerr != Error::NONE) {
                printf("OTKAZ na bufere N%d (kod %d)\n", n + 1, (int)aerr);
                break;
            }
            held[n] = h;
        }
        if (n == MAXN) printf("uderzhano %d shtuk bez otkaza\n", n);
        for (int k = 0; k < n; k++)
            mapper->freeBuffer(const_cast<native_handle_t*>(held[k]));
        printf("osvobozhdeno %d\n", n);
    }


    /*
     * IMPORT. Vsyo vyshe prohodit, a kamera poluchaet otkaz - znachit delo ne
     * v samom vydelenii. GraphicBufferAllocator posle allocate() delaet ewyo
     * odin shag: mapper->importBuffer() UZHE V PROCESSE PRILOZHENIYA (mapper
     * zdes' passthrough, gralloc gruzitsya v sam process). Perebor vyshe etot
     * shag propuskal. Proveryaem ego otdel'no.
     */
    printf("\n=== import NV21 640x480 HW_TEXTURE ===\n");
    {
        IMapper::BufferDescriptorInfo i3{};
        i3.width = 640; i3.height = 480; i3.layerCount = 1;
        i3.format = static_cast<
            android::hardware::graphics::common::V1_0::PixelFormat>(17);
        i3.usage = 0x100;

        BufferDescriptor d3;
        Error e3 = Error::NONE;
        mapper->createDescriptor(i3, [&](Error e, BufferDescriptor d) { e3 = e; d3 = d; });
        if (e3 != Error::NONE) { printf("descriptor fail %d\n", (int)e3); return 1; }

        Error aerr = Error::NONE;
        const native_handle_t *raw = nullptr;
        allocator->allocate(d3, 1,
            [&](Error e, uint32_t, const hidl_vec<hidl_handle>& bufs) {
                aerr = e;
                if (e == Error::NONE && bufs.size() > 0) raw = bufs[0].getNativeHandle();
            });
        printf("allocate: %s (kod %d)\n", aerr == Error::NONE ? "OK" : "OTKAZ", (int)aerr);
        if (aerr == Error::NONE && raw) {
            Error ierr = Error::NONE;
            const native_handle_t *imported = nullptr;
            mapper->importBuffer(raw, [&](Error e, void *buf) {
                ierr = e; imported = static_cast<const native_handle_t *>(buf);
            });
            printf("importBuffer: %s (kod %d)\n",
                   ierr == Error::NONE ? "OK" : "OTKAZ", (int)ierr);
            if (ierr == Error::NONE && imported)
                mapper->freeBuffer(const_cast<native_handle_t *>(imported));
        }
    }

    return 0;
}
