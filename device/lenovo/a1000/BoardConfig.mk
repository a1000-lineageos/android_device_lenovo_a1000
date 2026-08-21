# BoardConfig.mk — Lenovo A1000 (a1000) — sc8830/sc7731c
# LineageOS 15.1 (Android 8.1). Ядро 3.10.64 переиспользуем (prebuilt), non-Treble, low-RAM (Go).

DEVICE_PATH := device/lenovo/a1000

# ---- Архитектура (Cortex-A7 / ARMv7-a-neon, 32-bit) ----
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a7
TARGET_CPU_SMP := true
ARCH_ARM_HAVE_TLS_REGISTER := true
TARGET_BOARD_PLATFORM := sc8830
TARGET_BOOTLOADER_BOARD_NAME := sc8830
TARGET_NO_BOOTLOADER := true

# ---- Ядро (prebuilt, собранное под 3.10.64 — НЕ собираем из исходников) ----
# Стоковое ядро вытащено из boot.img: firmware/out_kernel.bin
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilt/kernel
BOARD_KERNEL_IMAGE_NAME := zImage
BOARD_KERNEL_BASE := 0x00000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_OFFSET := 0x00008000
BOARD_RAMDISK_OFFSET := 0x01000000
BOARD_KERNEL_TAGS_OFFSET := 0x00000100
BOARD_KERNEL_CMDLINE := console=ttyS1,115200n8
BOARD_MKBOOTIMG_ARGS := --base $(BOARD_KERNEL_BASE) \
    --pagesize $(BOARD_KERNEL_PAGESIZE) \
    --kernel_offset $(BOARD_KERNEL_OFFSET) \
    --ramdisk_offset $(BOARD_RAMDISK_OFFSET) \
    --tags_offset $(BOARD_KERNEL_TAGS_OFFSET) \
    --dt $(DEVICE_PATH)/prebuilt/sprd.dtb \
    --ramdisk $(DEVICE_PATH)/prebuilt/ramdisk.img

# ---- Размеры разделов (ТОЧНЫЕ, сняты с устройства по adb: /proc/partitions + by-name) ----
# boot=mmcblk0p16, recovery=p19 (по 15360 KB); system=p17 (1228800 KB);
# cache=p18 (153600 KB); userdata=p21 (6182912 KB).
BOARD_BOOTIMAGE_PARTITION_SIZE := 15728640        # 15 МБ (p16)
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 26214400
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 943718400    # 900 МБ (A1000_SLIM; физический p17 = 1.17 ГБ)
BOARD_USERDATAIMAGE_PARTITION_SIZE := 6331301888  # 5.9 ГБ (p21)
BOARD_CACHEIMAGE_PARTITION_SIZE := 157286400      # 150 МБ (p18)
BOARD_FLASH_BLOCK_SIZE := 131072
TARGET_USERIMAGES_USE_EXT4 := true
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4

# ---- Partition by-name path (из fstab.sc8830) ----
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/etc/fstab.sc8830

# ---- low-RAM / Android Go (1 ГБ ОЗУ) ----
MALLOC_SVELTE := true
TARGET_USES_MKE2FS := true

# ---- GPU: Mali-400 (Utgard), userspace blob r5p0 ----
BOARD_EGL_CFG := $(DEVICE_PATH)/configs/egl.cfg
USE_OPENGL_RENDERER := true
# gralloc0 + HWC1 из стока → нужны адаптеры/shim (см. vendor README)
TARGET_USES_HWC2 := false
BOARD_USES_GRALLOC1on0_ADAPTER := true

# ---- Шим-библиотеки для блобов Android 5 (libui/libutils/libbinder C++ ABI) ----
# Конкретные символы добавим после первой сборки по linker-ошибкам.
TARGET_LD_SHIM_LIBS := \
    /system/lib/hw/hwcomposer.sc8830.so|libui_shim.so \
    /system/lib/egl/libGLES_mali.so|libui_shim.so

# ---- non-Treble ----
# Устройство запускалось на Android 5, Treble нет. Собираем монолит.
TARGET_USES_64BIT_BINDER := false
PRODUCT_FULL_TREBLE_OVERRIDE := false
# BOARD_VNDK_VERSION отключён: на non-Treble он плодил vndk-sp-28 дубль libhidltransport → hwservicemanager SIGABRT (destroyed mutex) → фастбут
# BOARD_VNDK_VERSION := current

# ---- SELinux (стартуем в permissive ради первого буста, потом ужесточим) ----
# TODO: после успешного буста перевести в enforcing и допилить sepolicy.
BOARD_KERNEL_CMDLINE += androidboot.selinux=permissive
BOARD_KERNEL_CMDLINE += androidboot.hardware=sc8830

# Свои правила: без них enforcing валит Wi-Fi целиком и рвёт звук.
# Всё, что лечится МЕТКОЙ, метится живьём в /system/bin/a1000_selabel.sh.
BOARD_PLAT_PUBLIC_SEPOLICY_DIR  += device/lenovo/a1000/sepolicy/public
BOARD_PLAT_PRIVATE_SEPOLICY_DIR += device/lenovo/a1000/sepolicy/private
BOARD_SEPOLICY_DIRS             += device/lenovo/a1000/sepolicy/vendor

# ---- Bootanimation под low-res экран ----
TARGET_BOOTANIMATION_HALF_RES := true
TARGET_SCREEN_HEIGHT := 800
TARGET_SCREEN_WIDTH := 480

# Допустимые имена устройства для updater-script (телефон рапортует lenovo_a1000)
TARGET_OTA_ASSERT_DEVICE := a1000,lenovo_a1000,Lenovo_A1000,Lenovo A1000

POLICYVERS := 28

# ---- VINTF device manifest (объявляет keymaster/composer/allocator/health HIDL-сервисы) ----
DEVICE_MANIFEST_FILE := device/lenovo/a1000/manifest.xml

# kernel 3.10: CC GC read-barrier corrupts refs -> disable, use CMS GC
ART_USE_READ_BARRIER := false

# ---- WiFi (WCN-чип, драйвер sprdwl) ----
# Драйвер даёт стандартный cfg80211-интерфейс, поэтому берём nl80211-supplicant
# из external/wpa_supplicant_8. Приватной вендорской driver_cmd-библиотеки для
# sprdwl в дереве нет, поэтому NONE — базовый функционал от этого не страдает.
BOARD_WLAN_DEVICE                := sprdwl
WPA_SUPPLICANT_VERSION           := VER_0_8_X
BOARD_WPA_SUPPLICANT_DRIVER      := NL80211
BOARD_HOSTAPD_DRIVER             := NL80211
WIFI_DRIVER_MODULE_PATH          := "/system/lib/modules/sprdwl.ko"
WIFI_DRIVER_MODULE_NAME          := "sprdwl"

# ---- Bluetooth (WCN-чип, канал /dev/sttybt0) ----
BOARD_HAVE_BLUETOOTH := true
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := device/lenovo/a1000/bluetooth


# ---- Две SIM ----
# У планшета два лотка, и modemd поднимает два демона RIL — по процессу на слот.
#
# SIM_COUNT здесь ДОЛЖЕН ОСТАВАТЬСЯ 1: это макрос нативного RIL, и при значении
# 2 libril собирается с -DANDROID_MULTI_SIM, отчего у RIL_onUnsolicitedResponse
# появляется четвёртый параметр socket_id. Стоковая libreference-ril_sp.so
# собрана БЕЗ него и передаёт три аргумента — в slotId прилетал мусор, и rild
# падал с SIGSEGV в radioStateChangedInd (проверено дизассемблером места вызова
# в блобе, см. firmware/build_ril_abifix.sh). Менять ABI вендорного блоба нам
# нечем, поэтому подстраиваемся под него.
#
# Разные слоты разводятся не этим макросом, а именем HIDL-сервиса: rild
# выставляет slot<clientId+1>, так что процессы -c 0 и -c 1 дают slot1 и slot2.
# Для фреймворка две SIM включены свойствами в device.mk
# (persist.radio.multisim.config=dsds, ro.multisim.simslotcount=2).
SIM_COUNT := 1

# Свойства, которые нельзя задать через PRODUCT_PROPERTY_OVERRIDES:
# их уже выставляет vendor/lineage, а побеждает первое присваивание.
TARGET_SYSTEM_PROP := device/lenovo/a1000/system.prop

# ================== A1000_SLIM: облегчение /system ==================
# ВАЖНО: PRODUCT_* переменные здесь имеют смысл только потому, что
# build/core/envsetup.mk включает BoardConfig.mk ПОСЛЕ product_config.mk —
# к этому моменту граф продуктов развёрнут и PRODUCT_PACKAGES/PRODUCT_LOCALES
# уже плоские списки. В самих продуктовых .mk на их месте стоят маркеры
# |inherit|<файл>, поэтому там filter-out молча не срабатывает.

# ---- Шрифты: без экзотических письменностей и без CJK (18 МБ) ----
# Emoji остаются: они под отдельным флагом MINIMAL_FONT_FOOTPRINT.
SMALLER_FONT_FOOTPRINT := true

# ---- Локали: было ~120 (ресурсы каждого apk), стало две ----
PRODUCT_LOCALES := en_US ru_RU
PRODUCT_AAPT_CONFIG := en_US ru_RU normal hdpi
PRODUCT_AAPT_PREF_CONFIG := hdpi

# ---- Словари LatinIME от LineageOS (28 языков, ~37 МБ; один main_uk.dict — 10 МБ) ----
# Остаются штатные словари AOSP: de, en, es, fr, it, pt_br, ru.
PRODUCT_PACKAGE_OVERLAYS := $(filter-out vendor/lineage/overlay/dictionaries,$(PRODUCT_PACKAGE_OVERLAYS))

# ---- Приложения, которые не нужны на 1 ГБ ОЗУ / 4" экране ----
# Печать (принтеров нет), заставки, почтовые клиенты, эквалайзер, плеер,
# живые обои, пасхалка, отладочные утилиты, TTS-движок, виджет часов, диктофон.
# PicoTts убирает синтез речи целиком (TalkBack останется без голоса).
A1000_REMOVE_PACKAGES := \
    BasicDreams PhotoTable \
    BuiltInPrintService PrintSpooler PrintRecommendationService \
    Email Exchange2 \
    LiveWallpapersPicker \
    Traceur EasterEgg Development Terminal \
    AudioFX Eleven \
    PicoTts \
    LockClock Recorder

# main.mk:798 берёт список устанавливаемого из карты продукта, а не из плоской
# переменной, поэтому фильтровать надо именно её.
PRODUCTS.$(INTERNAL_PRODUCT).PRODUCT_PACKAGES := $(filter-out \
    $(A1000_REMOVE_PACKAGES),$(PRODUCTS.$(INTERNAL_PRODUCT).PRODUCT_PACKAGES))
# Голоса Pico (7 МБ) приезжают отдельными PRODUCT_COPY_FILES из full_base.mk и
# без пакета PicoTts бесполезны.
# filter-out понимает только ОДИН % в шаблоне, поэтому фильтруем по источнику.
PRODUCT_COPY_FILES := $(filter-out external/svox/pico/lang/%,$(PRODUCT_COPY_FILES))

# ================== /A1000_SLIM ==================

# minui зовёт FBIOPAN_DISPLAY только под этим флагом, а без него на sprdfb кадр
# вообще не доезжает до панели: FBIOPUT_VSCREENINFO падает с -EINVAL, потому что
# minui просит yres_virtual = height*2, а драйвер держит height*FRAMEBUFFER_NR(=3),
# и sprdfb_check_var такое отвергает. Без pan не зовётся sprdfb_pan_display ->
# ctrl->refresh(), DISPC продолжает показывать буфер u-boot (логотип).
BOARD_RECOVERY_NEEDS_FBIOPAN_DISPLAY := true
