/*
 * libsprdshim C++ part — bridges legacy (Android 5) libui/libgui C++ ABI
 * symbols that gralloc.sc8830.so / hwcomposer.sc8830.so link against but
 * Android 9's libui/libgui export under different mangled names / signatures.
 *
 * Technique: for each missing member symbol we emit a free function carrying
 * the *exact legacy mangled name* via asm(), taking the implicit `this` as the
 * first argument, and forward to the real Android-9 method (compiled against
 * the A9 ui/ headers). dlopen is all-or-nothing, so every legacy UND symbol
 * must be provided before the blobs load.
 *
 * Build (against AOSP/NDK arm32, Android 9):
 *   arm-linux-androideabi-g++ -shared -fPIC -O2 -std=c++14 \
 *       libsprdshim.c libsprdshim_cpp.cpp -o libsprdshim.so \
 *       -lui -lutils -lcutils -lion -llog
 * Inject:  patchelf --add-needed libsprdshim.so gralloc.sc8830.so
 *          patchelf --add-needed libsprdshim.so hwcomposer.sc8830.so
 * Ship libsprdshim.so in /system/lib and /vendor/lib.
 *
 * A9 reference signatures (from frameworks/native/libs/ui, los16):
 *   GraphicBufferAllocator::allocate(uint32 w,uint32 h,PixelFormat,uint32 layerCount,
 *       uint64 usage,buffer_handle_t* h,uint32* stride,uint64 id,std::string name)
 *   GraphicBufferMapper::lock(buffer_handle_t,uint32 usage,const Rect&,void**)
 *   GraphicBuffer(const native_handle_t*,HandleWrapMethod,uint32 w,uint32 h,
 *       PixelFormat,uint32 layerCount,uint64 usage,uint32 stride)
 *   Fence(int), status_t Fence::wait(int)   [dtor is inline/default -> not exported]
 */
#include <stdint.h>
#include <string>
#include <new>
#include <ui/Fence.h>
#include <ui/Rect.h>
#include <ui/PixelFormat.h>
#include <ui/GraphicBuffer.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/GraphicBufferAllocator.h>
#include <utils/Errors.h>

using android::status_t;
using android::Fence;
using android::Rect;
using android::PixelFormat;
using android::GraphicBuffer;
using android::GraphicBufferMapper;
using android::GraphicBufferAllocator;

/* ===== android::Fence::wait(unsigned int)  ->  wait(int) =====
 * legacy: _ZN7android5Fence4waitEj   (A9 has waitEi). */
extern "C" status_t _sprdshim_fence_wait_u(Fence* self, unsigned int timeout)
    asm("_ZN7android5Fence4waitEj");
extern "C" status_t _sprdshim_fence_wait_u(Fence* self, unsigned int timeout) {
    return self->wait(static_cast<int>(timeout));
}

/* ===== android::Fence::~Fence()  (legacy: _ZN7android5FenceD1Ev) =====
 * DELIBERATELY NOT PROVIDED on Android 8.1.
 *
 * Android 9 exports no Fence dtor, so this file used to supply an empty stub just to let
 * dlopen succeed, accepting a leaked fence fd as "minor". Android 8.1 is different:
 * libui.so exports the real _ZN7android5FenceD1Ev. Because libui_shim.so is DT_NEEDED[0]
 * of hwcomposer.sc8830.so it is searched before libui, so a stub here HIJACKS the real
 * destructor for the blob.
 *
 * That is not minor. OverlayNativeWindow::queueBuffer() runs, once per composed frame:
 *      operator new(8); Fence::Fence(fenceFd); Fence::wait(-1);
 *      SprdPrimaryPlane::display(); Fence::~Fence(); operator delete
 * so an empty destructor leaks one sync fence fd per frame and blows through
 * RLIMIT_NOFILE within about half a minute of composited output.
 *
 * The 8.1 object is LightRefBase<Fence>(4) + base::unique_fd(4) = 8 bytes, exactly the
 * size the blob allocates, so binding to libui's real destructor is ABI-correct.
 */

/* ===== GraphicBufferMapper::lock(native_handle const*,int,Rect const&,void**) =====
 * legacy: _ZN7android19GraphicBufferMapper4lockEPK13native_handleiRKNS_4RectEPPv
 * (references are pointers in the ABI; usage int -> uint32). */
extern "C" status_t _sprdshim_gbm_lock(GraphicBufferMapper* self,
        const native_handle_t* handle, int usage, const Rect* bounds, void** vaddr)
    asm("_ZN7android19GraphicBufferMapper4lockEPK13native_handleiRKNS_4RectEPPv");
extern "C" status_t _sprdshim_gbm_lock(GraphicBufferMapper* self,
        const native_handle_t* handle, int usage, const Rect* bounds, void** vaddr) {
    return self->lock(handle, static_cast<uint32_t>(usage), *bounds, vaddr);
}

/* ===== GraphicBufferAllocator::alloc(uint32,uint32,int,int,native_handle const**,int*) =====
 * legacy: _ZN7android22GraphicBufferAllocator5allocEjjiiPPK13native_handlePi
 * A9 renamed alloc->allocate and added layerCount/uint64 usage/id/name. */
extern "C" status_t _sprdshim_gba_alloc(GraphicBufferAllocator* self,
        uint32_t w, uint32_t h, int format, int usage,
        const native_handle_t** handle, int* stride)
    asm("_ZN7android22GraphicBufferAllocator5allocEjjiiPPK13native_handlePi");
extern "C" status_t _sprdshim_gba_alloc(GraphicBufferAllocator* self,
        uint32_t w, uint32_t h, int format, int usage,
        const native_handle_t** handle, int* stride) {
    uint32_t s = 0;
    status_t r = self->allocate(w, h, static_cast<PixelFormat>(format),
            1u /*layerCount*/, static_cast<uint64_t>(static_cast<uint32_t>(usage)),
            handle, &s, 0ull /*graphicBufferId*/, "sprdshim");
    if (stride) *stride = static_cast<int>(s);
    return r;
}

/* ===== GraphicBuffer::GraphicBuffer(uint32,uint32,int,uint32,uint32,native_handle*,bool) =====
 * legacy: _ZN7android13GraphicBufferC1EjjijjP13native_handleb
 * Wrap the existing handle with the A9 handle-wrapping ctor (placement-new in
 * the storage `self` the caller already allocated). keepOwnership chooses
 * whether GraphicBuffer takes/owns the native_handle. */
extern "C" void _sprdshim_gb_ctor(GraphicBuffer* self,
        uint32_t w, uint32_t h, int format, uint32_t usage, uint32_t stride,
        native_handle_t* handle, bool keepOwnership)
    asm("_ZN7android13GraphicBufferC1EjjijjP13native_handleb");
extern "C" void _sprdshim_gb_ctor(GraphicBuffer* self,
        uint32_t w, uint32_t h, int format, uint32_t usage, uint32_t stride,
        native_handle_t* handle, bool keepOwnership) {
    new (self) GraphicBuffer(handle,
            keepOwnership ? GraphicBuffer::TAKE_HANDLE : GraphicBuffer::WRAP_HANDLE,
            w, h, static_cast<PixelFormat>(format), 1u /*layerCount*/,
            static_cast<uint64_t>(usage), stride);
}

/* ===== DONE: SPRD MemoryHeapIon::Get_phy_addr_from_ion =====
 * Implemented (pure C, no A9 headers) in libsprdshim.c via ION_IOC_CUSTOM /
 * ION_SPRD_CUSTOM_PHYS ioctl. It is a static member, so no `this`. */
