LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# Диагностика: что именно готов выделять стоковый gralloc.
LOCAL_MODULE := gralloc_probe
LOCAL_SRC_FILES := gralloc_probe.c
LOCAL_SHARED_LIBRARIES := libhardware liblog libcutils
LOCAL_CFLAGS := -Wall
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
# Диагностический перехват ioctl для сервиса аллокатора графики (LD_PRELOAD).
LOCAL_MODULE := libionprobe
LOCAL_SRC_FILES := libionprobe.c
LOCAL_SHARED_LIBRARIES := liblog libdl
LOCAL_CFLAGS := -Wall
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
# Перебор формат x usage ЧЕРЕЗ HIDL-сервис аллокатора (безопасно: блоб живёт
# в своём процессе, мы только просим у него буферы).
LOCAL_MODULE := alloc_probe
LOCAL_SRC_FILES := alloc_probe.cpp
LOCAL_SHARED_LIBRARIES :=     libhidlbase libhidltransport libutils liblog libcutils     android.hardware.graphics.allocator@2.0     android.hardware.graphics.mapper@2.0
LOCAL_CFLAGS := -Wall
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
# Снятие содержимого всех буферов framebuffer через mmap: read() у sprdfb нет.
LOCAL_MODULE := fb_dump
LOCAL_SRC_FILES := fb_dump.c
LOCAL_SHARED_LIBRARIES := liblog libcutils
LOCAL_CFLAGS := -Wall
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)
