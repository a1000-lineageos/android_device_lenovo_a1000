LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# Шим для стокового camera.sc8830.so: класс MemoryHeapIon (Spreadtrum держала
# его в патченном libbinder.so, в AOSP его нет) плюс пара символов, выпавших
# из 8.1. Подключается к блобу через patchelf, см. build_camera.sh.
LOCAL_MODULE := libsprd_camera_shim
LOCAL_SRC_FILES := sprd_camera_shim.cpp
LOCAL_SHARED_LIBRARIES := libbinder libui libutils libcutils liblog
LOCAL_CFLAGS := -Wall -Werror -Wno-unused-parameter
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

# ---- Заглушки вместо стоковых библиотек с текстовыми релокациями ----
# libface_finder.so и libmorpho_easy_hdr.so из стока bionic 8.1 не грузит
# (TEXTREL запрещены с API 23), и из-за этого не поднимался ВЕСЬ HAL камеры.
# Отдаём только те символы, которые реально нужны camera.sc8830.so.
# Цена: нет распознавания лиц и HDR.

include $(CLEAR_VARS)
LOCAL_MODULE := libface_finder
LOCAL_MODULE_SUFFIX := .so
LOCAL_SRC_FILES := face_finder_stub.c elfload.c
LOCAL_SHARED_LIBRARIES := liblog libdl
LOCAL_CFLAGS := -Wall -Werror
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libmorpho_easy_hdr
LOCAL_MODULE_SUFFIX := .so
LOCAL_SRC_FILES := morpho_hdr_stub.c elfload.c
LOCAL_SHARED_LIBRARIES := liblog libdl
LOCAL_CFLAGS := -Wall -Werror
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
