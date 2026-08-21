LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# power HAL поверх общей политики cpufreq (губернатор sprdemand).
# Ищется по ro.hardware.power: ro.hardware на этом устройстве = "unknown",
# поэтому на имя платформы полагаться нельзя.
LOCAL_MODULE := power.sc8830
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_SRC_FILES := power_sc8830.c
LOCAL_SHARED_LIBRARIES := liblog libcutils
LOCAL_HEADER_LIBRARIES := libhardware_headers
LOCAL_CFLAGS := -Wall -Werror
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
