# lineage_a1000.mk — продукт LineageOS 15.1 для Lenovo A1000 (Go)

# Чистая 32-битная база телефона (устройство только armeabi-v7a)
$(call inherit-product, $(SRC_TARGET_DIR)/product/aosp_base.mk)

# Конфиг устройства
$(call inherit-product, device/lenovo/a1000/device.mk)

# LineageOS базовый продукт (Go-вариант где возможно)
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

# Вырезаем мусорные приложения (нет PRODUCT_PACKAGES_REMOVE в A8.1).
# Фильтруем ПОСЛЕ всех inherit (common_full_phone последний).
PRODUCT_PACKAGES := $(filter-out \
    BasicDreams PhotoTable BuiltInPrintService PrintSpooler PrintRecommendationService \
    Email Exchange2 LiveWallpapersPicker Traceur EasterEgg AudioFX Eleven, \
    $(PRODUCT_PACKAGES))

PRODUCT_NAME := lineage_a1000
PRODUCT_DEVICE := a1000
PRODUCT_BRAND := Lenovo
PRODUCT_MODEL := Lenovo A1000
PRODUCT_MANUFACTURER := Lenovo

PRODUCT_GMS_CLIENTID_BASE := android-lenovo

# Build fingerprint (под стоковый, чтобы проходить проверки)
BUILD_FINGERPRINT := Lenovo/A1000/A1000:5.0/LRX21M/release-keys
PRODUCT_BUILD_PROP_OVERRIDES += \
    PRODUCT_NAME=A1000 \
    TARGET_DEVICE=a1000 \
    BUILD_FINGERPRINT="Lenovo/A1000/A1000:5.0/LRX21M/release-keys"

# ВНИМАНИЕ: core_64_bit наследование выше — для 32-бит устройства его НЕ нужно.
# TODO: заменить на чистый 32-битный base, если сборка ругнётся на 64-bit.
