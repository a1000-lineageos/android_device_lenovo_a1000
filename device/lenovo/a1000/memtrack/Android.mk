LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# memtrack поверх Mali-400. Модуль называется memtrack.sc8830, а находит его
# hw_get_module по свойству ro.hardware.memtrack (см. device.mk): ro.hardware
# на этом устройстве = "unknown", поэтому на имя платформы полагаться нельзя.
LOCAL_MODULE := memtrack.sc8830
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_SRC_FILES := memtrack_sc8830.c
LOCAL_SHARED_LIBRARIES := liblog libcutils
LOCAL_HEADER_LIBRARIES := libhardware_headers
LOCAL_CFLAGS := -Wall -Werror
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
