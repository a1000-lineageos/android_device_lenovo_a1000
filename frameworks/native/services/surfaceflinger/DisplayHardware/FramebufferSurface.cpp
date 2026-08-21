/*
 **
 ** Copyright 2012 The Android Open Source Project
 **
 ** Licensed under the Apache License Version 2.0(the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 **
 **     http://www.apache.org/licenses/LICENSE-2.0
 **
 ** Unless required by applicable law or agreed to in writing software
 ** distributed under the License is distributed on an "AS IS" BASIS
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
 */

// #define LOG_NDEBUG 0
#undef LOG_TAG
#define LOG_TAG "FramebufferSurface"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <utils/String8.h>
#include <log/log.h>

#include <EGL/egl.h>

#include <hardware/hardware.h>
#include <gui/BufferItem.h>
#include <gui/BufferQueue.h>
#include <gui/Surface.h>

#include <ui/GraphicBuffer.h>
#include <ui/Rect.h>

#include <cutils/properties.h>
#include "FramebufferSurface.h"
#include "HWComposer.h"
#include "../SurfaceFlinger.h"

// ----------------------------------------------------------------------------
namespace android {
// ----------------------------------------------------------------------------

/*
 * This implements the (main) framebuffer management. This class is used
 * mostly by SurfaceFlinger, but also by command line GL application.
 *
 */

FramebufferSurface::FramebufferSurface(HWComposer& hwc, int disp,
        const sp<IGraphicBufferConsumer>& consumer) :
    ConsumerBase(consumer),
    mDisplayType(disp),
    mCurrentBufferSlot(-1),
    mCurrentBuffer(),
    mCurrentFence(Fence::NO_FENCE),
#ifdef USE_HWC2
    mHwc(hwc),
    mHasPendingRelease(false),
    mPreviousBufferSlot(BufferQueue::INVALID_BUFFER_SLOT),
    mPreviousBuffer()
#else
    mHwc(hwc)
#endif
{
#ifdef USE_HWC2
    ALOGV("Creating for display %d", disp);
#endif

    mName = "FramebufferSurface";
    mConsumer->setConsumerName(mName);
    /* A1000_FBCACHED: этот буфер пишет GPU, а читает его ЦЕЛИКОМ процессор —
     * gralloc fb_post копирует кадр в кадровый буфер (lock(src); lock(fb);
     * memcpy). Без единого SW-флага gralloc отображает буфер overlay-heap'а
     * НЕкешированным, и чтение идёт 60 МБ/с вместо 1066: 25.6 мс на кадр
     * (замер fbbench), 82 % всего процессорного времени SurfaceFlinger
     * (simpleperf: libc memcpy). Запрошенный SW_READ_OFTEN оставляет буфер в
     * том же heap, но отображает его кешированным; когерентность с GPU держит
     * сам gralloc через ion_sync_fd в lock()/unlock() (ION_FLAG_CACHED).
     * Если картинка поедет — debug.sf.a1000_fbcached=0 возвращает как было. */
    char _fc[PROPERTY_VALUE_MAX];
    /* ЗАМЕРЕНО 2026-08-20: SW_READ_OFTEN не сделал буфер кешированным (memcpy
     * как был 81 % профиля), а рваных кадров стало 83.8 % против 76 и медиана
     * 61 мс против 44. Поэтому по умолчанию ВЫКЛЮЧЕНО; =1 только для опытов. */
    property_get("debug.sf.a1000_fbcached", _fc, "0");
    const uint32_t _swread = (_fc[0] == '0') ? 0u
            : (uint32_t) GRALLOC_USAGE_SW_READ_OFTEN;
    /* A1000_FBHEAP: при =2 убираем и сам overlay-heap. Вендорный gralloc держит
     * этот heap некешированным независимо от SW-флагов (потому =1 и не дал
     * ничего), а в обычном system-heap'е ION_FLAG_CACHED по SW_READ_OFTEN
     * ставится честно. Непрерывность буферу здесь не нужна: DISPC из него не
     * сканирует, fb_post всё равно копирует кадр в fb0. Зато аппаратный оверлей
     * на одиночном слое с таким буфером работать перестанет — поэтому отдельное
     * значение свойства, а не поведение по умолчанию. */
    const uint32_t _heap = (_fc[0] == '2') ? 0u : 0x01000000u /*SPRD_OVERLAY_ion_heap*/;
    mConsumer->setConsumerUsageBits(_heap |
                                       GRALLOC_USAGE_HW_RENDER |
                                       GRALLOC_USAGE_HW_COMPOSER |
                                       _swread);
#ifdef USE_HWC2
    const auto& activeConfig = mHwc.getActiveConfig(disp);
    mConsumer->setDefaultBufferSize(activeConfig->getWidth(),
            activeConfig->getHeight());
#else
    mConsumer->setDefaultBufferFormat(mHwc.getFormat(disp));
    mConsumer->setDefaultBufferSize(mHwc.getWidth(disp), mHwc.getHeight(disp));
#endif
    mConsumer->setMaxAcquiredBufferCount(
            SurfaceFlinger::maxFrameBufferAcquiredBuffers - 1);
}

status_t FramebufferSurface::beginFrame(bool /*mustRecompose*/) {
    return NO_ERROR;
}

status_t FramebufferSurface::prepareFrame(CompositionType /*compositionType*/) {
    return NO_ERROR;
}

status_t FramebufferSurface::advanceFrame() {
#ifdef USE_HWC2
    uint32_t slot = 0;
    sp<GraphicBuffer> buf;
    sp<Fence> acquireFence(Fence::NO_FENCE);
    android_dataspace_t dataspace = HAL_DATASPACE_UNKNOWN;
    status_t result = nextBuffer(slot, buf, acquireFence, dataspace);
    if (result != NO_ERROR) {
        ALOGE("error latching next FramebufferSurface buffer: %s (%d)",
                strerror(-result), result);
    }
    return result;
#else
    // Once we remove FB HAL support, we can call nextBuffer() from here
    // instead of using onFrameAvailable(). No real benefit, except it'll be
    // more like VirtualDisplaySurface.
    return NO_ERROR;
#endif
}

#ifdef USE_HWC2
status_t FramebufferSurface::nextBuffer(uint32_t& outSlot,
        sp<GraphicBuffer>& outBuffer, sp<Fence>& outFence,
        android_dataspace_t& outDataspace) {
#else
status_t FramebufferSurface::nextBuffer(sp<GraphicBuffer>& outBuffer, sp<Fence>& outFence) {
#endif
    Mutex::Autolock lock(mMutex);

    BufferItem item;
    status_t err = acquireBufferLocked(&item, 0);
    if (err == BufferQueue::NO_BUFFER_AVAILABLE) {
#ifdef USE_HWC2
        mHwcBufferCache.getHwcBuffer(mCurrentBufferSlot, mCurrentBuffer,
                &outSlot, &outBuffer);
#else
        outBuffer = mCurrentBuffer;
#endif
        return NO_ERROR;
    } else if (err != NO_ERROR) {
        ALOGE("error acquiring buffer: %s (%d)", strerror(-err), err);
        return err;
    }

    // If the BufferQueue has freed and reallocated a buffer in mCurrentSlot
    // then we may have acquired the slot we already own.  If we had released
    // our current buffer before we call acquireBuffer then that release call
    // would have returned STALE_BUFFER_SLOT, and we would have called
    // freeBufferLocked on that slot.  Because the buffer slot has already
    // been overwritten with the new buffer all we have to do is skip the
    // releaseBuffer call and we should be in the same state we'd be in if we
    // had released the old buffer first.
    if (mCurrentBufferSlot != BufferQueue::INVALID_BUFFER_SLOT &&
        item.mSlot != mCurrentBufferSlot) {
#ifdef USE_HWC2
        mHasPendingRelease = true;
        mPreviousBufferSlot = mCurrentBufferSlot;
        mPreviousBuffer = mCurrentBuffer;
#else
        // Release the previous buffer.
        err = releaseBufferLocked(mCurrentBufferSlot, mCurrentBuffer,
                EGL_NO_DISPLAY, EGL_NO_SYNC_KHR);
        if (err < NO_ERROR) {
            ALOGE("error releasing buffer: %s (%d)", strerror(-err), err);
            return err;
        }
#endif
    }
    mCurrentBufferSlot = item.mSlot;
    mCurrentBuffer = mSlots[mCurrentBufferSlot].mGraphicBuffer;
    mCurrentFence = item.mFence;

    outFence = item.mFence;
#ifdef USE_HWC2
    mHwcBufferCache.getHwcBuffer(mCurrentBufferSlot, mCurrentBuffer,
            &outSlot, &outBuffer);
    outDataspace = item.mDataSpace;
    status_t result =
            mHwc.setClientTarget(mDisplayType, outSlot, outFence, outBuffer, outDataspace);
    if (result != NO_ERROR) {
        ALOGE("error posting framebuffer: %d", result);
        return result;
    }
#else
    outBuffer = mCurrentBuffer;
#endif

    return NO_ERROR;
}

#ifndef USE_HWC2
// Overrides ConsumerBase::onFrameAvailable(), does not call base class impl.
void FramebufferSurface::onFrameAvailable(const BufferItem& /* item */) {
    sp<GraphicBuffer> buf;
    sp<Fence> acquireFence;
    status_t err = nextBuffer(buf, acquireFence);
    if (err != NO_ERROR) {
        ALOGE("error latching nnext FramebufferSurface buffer: %s (%d)",
                strerror(-err), err);
        return;
    }
    err = mHwc.fbPost(mDisplayType, acquireFence, buf);
    if (err != NO_ERROR) {
        ALOGE("error posting framebuffer: %d", err);
    }
}
#endif

void FramebufferSurface::freeBufferLocked(int slotIndex) {
    ConsumerBase::freeBufferLocked(slotIndex);
    if (slotIndex == mCurrentBufferSlot) {
        mCurrentBufferSlot = BufferQueue::INVALID_BUFFER_SLOT;
    }
}

void FramebufferSurface::onFrameCommitted() {
#ifdef USE_HWC2
    if (mHasPendingRelease) {
        sp<Fence> fence = mHwc.getPresentFence(mDisplayType);
        if (fence->isValid()) {
            status_t result = addReleaseFence(mPreviousBufferSlot,
                    mPreviousBuffer, fence);
            ALOGE_IF(result != NO_ERROR, "onFrameCommitted: failed to add the"
                    " fence: %s (%d)", strerror(-result), result);
        }
        status_t result = releaseBufferLocked(mPreviousBufferSlot,
                mPreviousBuffer, EGL_NO_DISPLAY, EGL_NO_SYNC_KHR);
        ALOGE_IF(result != NO_ERROR, "onFrameCommitted: error releasing buffer:"
                " %s (%d)", strerror(-result), result);

        mPreviousBuffer.clear();
        mHasPendingRelease = false;
    }
#else
    sp<Fence> fence = mHwc.getAndResetReleaseFence(mDisplayType);
    if (fence->isValid() &&
            mCurrentBufferSlot != BufferQueue::INVALID_BUFFER_SLOT) {
        status_t err = addReleaseFence(mCurrentBufferSlot,
                mCurrentBuffer, fence);
        ALOGE_IF(err, "setReleaseFenceFd: failed to add the fence: %s (%d)",
                strerror(-err), err);
    }
#endif
}

#ifndef USE_HWC2
status_t FramebufferSurface::compositionComplete()
{
    return mHwc.fbCompositionComplete();
}
#endif

void FramebufferSurface::dumpAsString(String8& result) const {
    ConsumerBase::dumpState(result);
}

void FramebufferSurface::dumpLocked(String8& result, const char* prefix) const
{
#ifndef USE_HWC2
    mHwc.fbDump(result);
#endif
    ConsumerBase::dumpLocked(result, prefix);
}

const sp<Fence>& FramebufferSurface::getClientTargetAcquireFence() const {
    return mCurrentFence;
}

// ----------------------------------------------------------------------------
}; // namespace android
// ----------------------------------------------------------------------------
