# Auto-generated vendor makefile for Lenovo A1000 (sc8830/sc7731c)
# Копирует проприетарные блобы в системный образ (non-Treble => в /system).

PRODUCT_SOONG_NAMESPACES += vendor/lenovo/a1000

PRODUCT_COPY_FILES += \
    vendor/lenovo/a1000/proprietary/bin/GPSenseEngine:system/bin/GPSenseEngine \
    vendor/lenovo/a1000/proprietary/bin/modem_control:system/bin/modem_control \
    vendor/lenovo/a1000/proprietary/bin/modemd:system/bin/modemd \
    vendor/lenovo/a1000/proprietary/bin/nvitemd:system/bin/nvitemd \
    vendor/lenovo/a1000/proprietary/bin/rild_sp:system/bin/rild_sp \
    vendor/lenovo/a1000/proprietary/bin/wcnd:system/bin/wcnd \
    vendor/lenovo/a1000/proprietary/lib/egl/egl.cfg:system/lib/egl/egl.cfg \
    vendor/lenovo/a1000/proprietary/lib/egl/libGLES_mali.so:system/lib/egl/libGLES_mali.so \
    vendor/lenovo/a1000/proprietary/lib/hw/audio.primary.sc8830.so:system/lib/hw/audio.primary.sc8830.so \
    vendor/lenovo/a1000/proprietary/lib/hw/audio_policy.sc8830.so:system/lib/hw/audio_policy.sc8830.so \
    vendor/lenovo/a1000/proprietary/lib/hw/camera.sc8830.so:system/lib/hw/camera.sc8830.so \
    vendor/lenovo/a1000/proprietary/lib/hw/fm.sc8830.so:system/lib/hw/fm.sc8830.so \
    vendor/lenovo/a1000/proprietary/lib/hw/gps.default.so:system/lib/hw/gps.default.so \
    vendor/lenovo/a1000/proprietary/lib/hw/gralloc.sc8830.so:system/lib/hw/gralloc.sc8830.so \
    vendor/lenovo/a1000/proprietary/lib/hw/hwcomposer.sc8830.so:system/lib/hw/hwcomposer.sc8830.so \
    vendor/lenovo/a1000/proprietary/lib/hw/lights.sc8830.so:system/lib/hw/lights.sc8830.so \
    vendor/lenovo/a1000/proprietary/lib/hw/power.default.so:system/lib/hw/power.default.so \
    vendor/lenovo/a1000/proprietary/lib/hw/sensors.sc8830.so:system/lib/hw/sensors.sc8830.so \
    vendor/lenovo/a1000/proprietary/lib/hw/sprd_gsp.sc8830.so:system/lib/hw/sprd_gsp.sc8830.so \
    vendor/lenovo/a1000/proprietary/lib/libomx_avcdec_hw_sprd.so:system/lib/libomx_avcdec_hw_sprd.so \
    vendor/lenovo/a1000/proprietary/lib/libomx_avcenc_hw_sprd.so:system/lib/libomx_avcenc_hw_sprd.so \
    vendor/lenovo/a1000/proprietary/lib/libomx_m4vh263dec_hw_sprd.so:system/lib/libomx_m4vh263dec_hw_sprd.so \
    vendor/lenovo/a1000/proprietary/lib/libomx_m4vh263enc_hw_sprd.so:system/lib/libomx_m4vh263enc_hw_sprd.so \
    vendor/lenovo/a1000/proprietary/lib/libomx_mp3dec_sprd.so:system/lib/libomx_mp3dec_sprd.so \
    vendor/lenovo/a1000/proprietary/lib/libomx_vpxdec_hw_sprd.so:system/lib/libomx_vpxdec_hw_sprd.so \
    vendor/lenovo/a1000/proprietary/lib/libsprd_agps_agent.so:system/lib/libsprd_agps_agent.so \
    vendor/lenovo/a1000/proprietary/lib/libstagefright_sprd_h264dec.so:system/lib/libstagefright_sprd_h264dec.so \
    vendor/lenovo/a1000/proprietary/lib/libstagefright_sprd_h264enc.so:system/lib/libstagefright_sprd_h264enc.so \
    vendor/lenovo/a1000/proprietary/lib/libstagefright_sprd_mp3dec.so:system/lib/libstagefright_sprd_mp3dec.so \
    vendor/lenovo/a1000/proprietary/lib/libstagefright_sprd_mpeg4dec.so:system/lib/libstagefright_sprd_mpeg4dec.so \
    vendor/lenovo/a1000/proprietary/lib/libstagefright_sprd_mpeg4enc.so:system/lib/libstagefright_sprd_mpeg4enc.so \
    vendor/lenovo/a1000/proprietary/lib/libstagefright_sprd_soft_h264dec.so:system/lib/libstagefright_sprd_soft_h264dec.so \
    vendor/lenovo/a1000/proprietary/lib/libstagefright_sprd_soft_mpeg4dec.so:system/lib/libstagefright_sprd_soft_mpeg4dec.so \
    vendor/lenovo/a1000/proprietary/lib/libstagefright_sprd_vpxdec.so:system/lib/libstagefright_sprd_vpxdec.so \
    vendor/lenovo/a1000/proprietary/lib/modules/mali.ko:system/lib/modules/mali.ko \
    vendor/lenovo/a1000/proprietary/lib/modules/sprdwl.ko:system/lib/modules/sprdwl.ko \
    vendor/lenovo/a1000/proprietary/lib/modules/trout_fm.ko:system/lib/modules/trout_fm.ko \
    vendor/lenovo/a1000/proprietary/vendor/lib/drm/libdrmwvmplugin.so:system/vendor/lib/drm/libdrmwvmplugin.so \
    vendor/lenovo/a1000/proprietary/vendor/lib/libWVStreamControlAPI_L3.so:system/vendor/lib/libWVStreamControlAPI_L3.so \
    vendor/lenovo/a1000/proprietary/vendor/lib/libdrmdecrypt.so:system/vendor/lib/libdrmdecrypt.so \
    vendor/lenovo/a1000/proprietary/vendor/lib/libwvdrm_L3.so:system/vendor/lib/libwvdrm_L3.so \
    vendor/lenovo/a1000/proprietary/vendor/lib/libwvm.so:system/vendor/lib/libwvm.so \
    vendor/lenovo/a1000/proprietary/vendor/lib/mediadrm/libdrmclearkeyplugin.so:system/vendor/lib/mediadrm/libdrmclearkeyplugin.so \
    vendor/lenovo/a1000/proprietary/vendor/lib/mediadrm/libwvdrmengine.so:system/vendor/lib/mediadrm/libwvdrmengine.so \


# ---- Блобы, которые доставлялись на устройство вручную ----
# Часть блобов на устройстве лежит в /vendor/lib — именно оттуда их
# ищут HIDL-сервисы, работающие в вендорном пространстве имён.
PRODUCT_COPY_FILES += \
    vendor/lenovo/a1000/proprietary/bin/phoneserver:system/bin/phoneserver \
    vendor/lenovo/a1000/proprietary/bin/refnotify:system/bin/refnotify \
    vendor/lenovo/a1000/proprietary/lib/libbt-vendor.so:system/lib/libbt-vendor.so \
    vendor/lenovo/a1000/proprietary/lib/libengbt.so:system/lib/libengbt.so \
    vendor/lenovo/a1000/proprietary/lib/libiwnpi.so:system/lib/libiwnpi.so \
    vendor/lenovo/a1000/proprietary/lib/libkeystore-engine-wifi-hidl.so:system/lib/libkeystore-engine-wifi-hidl.so \
    vendor/lenovo/a1000/proprietary/lib/libkeystore-wifi-hidl.so:system/lib/libkeystore-wifi-hidl.so \
    vendor/lenovo/a1000/proprietary/lib/hw/audio.r_submix.default.so:system/lib/hw/audio.r_submix.default.so \
    vendor/lenovo/a1000/proprietary/lib/hw/audio.usb.default.so:system/lib/hw/audio.usb.default.so \
    vendor/lenovo/a1000/proprietary/vendor/lib/hw/lights.sc8830.so:system/vendor/lib/hw/lights.sc8830.so \
    vendor/lenovo/a1000/proprietary/vendor/lib/hw/sensors.sc8830.so:system/vendor/lib/hw/sensors.sc8830.so \
    vendor/lenovo/a1000/proprietary/vendor/lib/libreference-ril_sp.so:system/vendor/lib/libreference-ril_sp.so \
    vendor/lenovo/a1000/proprietary/vendor/lib/libsprdsensors_shim.so:system/vendor/lib/libsprdsensors_shim.so \
    vendor/lenovo/a1000/proprietary/vendor/lib/libui_shim.so:system/vendor/lib/libui_shim.so


# libtinyalsa: на устройстве стоит СТОКОВАЯ библиотека, побайтово совпадающая
# со стоковой прошивкой, а не собранная из external/tinyalsa. Наш вариант с
# добавленным pcm_set_samplerate собирался и был заменён обратно на стоковый —
# значит, стоковый audio.primary.sc8830.so работает именно с ним.
PRODUCT_COPY_FILES += \
    vendor/lenovo/a1000/proprietary/lib/libtinyalsa.so:system/lib/libtinyalsa.so
