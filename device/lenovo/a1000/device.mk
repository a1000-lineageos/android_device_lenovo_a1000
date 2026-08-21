# device.mk — Lenovo A1000 (a1000) — sc8830
# Android Go (low_ram) конфигурация для 1 ГБ ОЗУ.

# ---- Подключаем vendor-блобы (45 файлов из стока) ----
$(call inherit-product-if-exists, vendor/lenovo/a1000/a1000-vendor.mk)

# ---- Android Go: low-RAM профиль ----
$(call inherit-product, build/target/product/go_defaults.mk)

PRODUCT_PROPERTY_OVERRIDES += \
    ro.config.low_ram=true \
    ro.lmk.use_minfree_levels=true \
    dalvik.vm.heapgrowthlimit=96m \
    dalvik.vm.heapsize=174m \
    dalvik.vm.heapstartsize=8m \
    dalvik.vm.heaptargetutilization=0.75 \
    persist.sys.force_highendgfx=false

# ---- zram (своп в ОЗУ) для разгрузки 1 ГБ ----
PRODUCT_PROPERTY_OVERRIDES += \
    ro.config.zram=true

# ro.config.zram=true сам по себе своп в A9 НЕ поднимает -> активатор:
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/bin/zram_setup.sh:system/bin/zram_setup.sh \
    $(LOCAL_PATH)/rootdir/etc/init/zram.rc:system/etc/init/zram.rc

# Ускорение первого бута: лёгкий dexopt-фильтр
PRODUCT_PROPERTY_OVERRIDES += \
    pm.dexopt.first-boot=verify \
    pm.dexopt.boot=verify

# ---- Дисплей / графика ----
PRODUCT_AAPT_CONFIG := normal hdpi
PRODUCT_AAPT_PREF_CONFIG := hdpi
PRODUCT_CHARACTERISTICS := default

PRODUCT_PROPERTY_OVERRIDES += \
    ro.opengles.version=131072 \
    ro.hwui.use_vulkan=false \
    ro.sf.lcd_density=220

# Из стокового system/build.prop (A1000_S30666_161121_ROW), см. firmware/stockfw/system/build.prop.
# render_dirty_regions=false — сток явно ГАСИТ частичную перерисовку: с sprd overlay-путём
# partial update даёт артефакты. lcd_width/height — физический размер экрана в мм (4", 480x800).
PRODUCT_PROPERTY_OVERRIDES += \
    debug.hwui.render_dirty_regions=false \
    ro.sf.hwrotation=0 \
    ro.sf.lcd_width=54 \
    ro.sf.lcd_height=96 \

# Панель: lcd_id=ID0/lcd_adc=ADC1202/lcd_flag=FLAG1 подставляет загрузчик (uboot) в cmdline.
# fb0 = 480x800, тройной буфер (virtual 480x2400). Density 220.

# ---- HALs (минимум для буста; HIDL-обёртки поверх legacy) ----
PRODUCT_PACKAGES += \
    android.hardware.graphics.allocator@2.0-impl \
    android.hardware.graphics.allocator@2.0-service \
    android.hardware.graphics.mapper@2.0-impl \
    android.hardware.graphics.composer@2.1-impl \
    android.hardware.graphics.composer@2.1-service \
    gralloc.sc8830 \
    hwcomposer.sc8830 \
    libGLES_mali \
    libhwc2on1adapter \
    libgralloc1on0adapter

# ---- Sensors HAL: акселерометр (автоповорот). Блоб sensors.sc8830.so уже
# копируется, не хватало только HIDL-обёртки и записи в манифесте.
PRODUCT_PACKAGES += \
    android.hardware.sensors@1.0-impl \
    android.hardware.sensors@1.0-service

# ---- Lights HAL: без него Android не управляет подсветкой вообще,
# и при блокировке экрана панель гаснет, а подсветка остаётся гореть (белый экран).
PRODUCT_PACKAGES += \
    android.hardware.light@2.0-impl \
    android.hardware.light@2.0-service

# health HAL (нужен для буста — иначе виснет на проверке батареи)
PRODUCT_PACKAGES += \
    android.hardware.health@1.0-service \
    android.hardware.health@1.0-impl

# keystore (нужен для буста)
PRODUCT_PACKAGES += \
    android.hardware.keymaster@3.0-impl \
    android.hardware.keymaster@3.0-service

# ---- Audio HIDL HAL (service+impl+effect + legacy primary stub).
#      Без HAL+VINTF-манифеста чистый mka bacon -> audioserver SIGSEGV (#4). ----
PRODUCT_PACKAGES += \
    android.hardware.audio@2.0-service \
    android.hardware.audio@2.0-impl \
    android.hardware.audio.effect@2.0-impl \
    audio.primary.default

# stub audio_policy config (нет реального audio HW): top-level + include из frameworks/av
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/audio/audio_policy_configuration.xml:system/etc/audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/stub_audio_policy_configuration.xml:system/etc/stub_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:system/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:system/etc/default_volume_tables.xml

# ---- Shim-либы (заглушки, заполним символы после первой сборки) ----
PRODUCT_PACKAGES += \
    libui_shim

# ---- init / fstab / rc файлы ----
PRODUCT_PACKAGES += \
    fstab.sc8830

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/etc/fstab.sc8830:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.sc8830 \
    $(LOCAL_PATH)/rootdir/etc/fstab.sc8830:root/fstab.sc8830 \
    $(LOCAL_PATH)/rootdir/etc/init.sc8830.rc:root/init.sc8830.rc \
    $(LOCAL_PATH)/rootdir/etc/init.unknown.rc:root/init.unknown.rc \
    $(LOCAL_PATH)/rootdir/etc/fstab.unknown:root/fstab.unknown \
    $(LOCAL_PATH)/rootdir/sbin/toybox_static:root/sbin/toybox_static

# ---- Разрешения/функции устройства (минимум) ----
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:system/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:system/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml

# ---- Go-приложения вместо полных GMS (лёгкие) ----
# (GApps Go ставятся отдельно; здесь только базовый AOSP набор для буста)

# ==== A1000 Android9 boot-fix integration (mali autoload, /dev perms, ART) ====
# NB: mali.ko binary is shipped by vendor/lenovo/a1000 (a1000-vendor.mk), replaced with ABI-matched build.
PRODUCT_COPY_FILES += $(LOCAL_PATH)/rootdir/etc/init/mali_load.rc:system/etc/init/mali_load.rc
PRODUCT_COPY_FILES += $(LOCAL_PATH)/rootdir/ueventd.sc8830.rc:root/ueventd.sc8830.rc
PRODUCT_PROPERTY_OVERRIDES += dalvik.vm.relocate=true

# ==== A1000 debug/diagnostics props (persistent logcat survives reboot, read from recovery) ====
PRODUCT_PROPERTY_OVERRIDES +=     persist.logd.logpersistd.size=4096     persist.sys.usb.config=adb

# ---- Bluetooth ----
PRODUCT_PACKAGES += \
    android.hardware.bluetooth@1.0-service \
    android.hardware.bluetooth@1.0-impl \
    libbt-vendor \
    bluetooth.default \
    libbluetooth_jni \
    Bluetooth

# ---- Демоны RIL и Wi-Fi ----
# Объявлены явно: без этого сборка их не устанавливает, а init-скрипты
# a1000_ril.rc и a1000_wpa.rc запускают именно /vendor/bin/hw/rild и
# /vendor/bin/hw/wpa_supplicant. На устройстве они лежали с давних сборок как
# устаревшие артефакты out/ и пропали после первого же m installclean.
# rild здесь AOSP-шный: только он регистрирует HIDL android.hardware.radio@1.0,
# библиотека остаётся стоковой (rild.libpath ниже).
PRODUCT_PACKAGES += \
    rild \
    wpa_supplicant \
    hostapd

# ---- Звонилка ----
PRODUCT_PACKAGES += \
    Dialer

# ---- Фичи, без которых не поднимаются сервисы ----
# usb.accessory: иначе SystemServer не стартует UsbService, и Settings падает
# в «Подключенных устройствах» (UsbBackend -> null UsbManager -> getPorts()).
# bluetooth/bluetooth_le: иначе не регистрируется bluetooth_manager, а apk
# Bluetooth в манифесте требует android.hardware.bluetooth_le.
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:system/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:system/etc/permissions/android.hardware.bluetooth_le.xml

# ---- FM-радио ----
# Приложение + JNI под драйвер sr2351 (drivers/misc/fm_2351 -> /dev/Trout_FM).
PRODUCT_PACKAGES += \
    FMRadio \
    libfmjni

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/etc/a1000_fm.rc:system/etc/init/a1000_fm.rc


# ---- Звук ----
# ro.hardware = "unknown" (в cmdline нет androidboot.hardware), поэтому
# hw_get_module подхватывал /vendor/lib/hw/audio.primary.default.so — заглушку.
# hw_get_module_by_class перед этим смотрит на ro.hardware.<class>, так что
# одного свойства достаточно, чтобы попасть в стоковый HAL.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.hardware.audio.primary=sc8830

# Стоковый HAL Spreadtrum, его проприетарные зависимости и конфиги.
# Из 139 внешних символов HAL'а в нашей 8.1 не хватало только
# pcm_set_samplerate — он добавлен в external/tinyalsa.
PRODUCT_COPY_FILES += \
    vendor/lenovo/a1000/proprietary/lib/hw/audio.primary.sc8830.so:system/lib/hw/audio.primary.sc8830.so \
    vendor/lenovo/a1000/proprietary/lib/libatchannel.so:system/lib/libatchannel.so \
    vendor/lenovo/a1000/proprietary/lib/libdumpdata.so:system/lib/libdumpdata.so \
    vendor/lenovo/a1000/proprietary/lib/libnvexchange.so:system/lib/libnvexchange.so \
    vendor/lenovo/a1000/proprietary/lib/libvbeffect.so:system/lib/libvbeffect.so \
    vendor/lenovo/a1000/proprietary/lib/libvbpga.so:system/lib/libvbpga.so \
    vendor/lenovo/a1000/proprietary/etc/audio_hw.xml:system/etc/audio_hw.xml \
    vendor/lenovo/a1000/proprietary/etc/tiny_hw.xml:system/etc/tiny_hw.xml \
    vendor/lenovo/a1000/proprietary/etc/codec_pga.xml:system/etc/codec_pga.xml \
    vendor/lenovo/a1000/proprietary/etc/audio_para:system/etc/audio_para \
    vendor/lenovo/a1000/proprietary/etc/audio_policy.conf:system/etc/audio_policy.conf

# Дополнительные аудио-модули: без них AudioPolicyManager ругается
# "could not open HW module a2dp/usb/r_submix". a2dp нужен для звука по
# Bluetooth, r_submix — для записи экрана и трансляции.
PRODUCT_PACKAGES += \
    audio.a2dp.default \
    audio.usb.default \
    audio.r_submix.default


# ---- Кодеки ----
# Без media_codecs.xml MediaCodecList пуст: не создаётся ни один декодер,
# ACodec падает с OMX_ErrorUndefined и не играет вообще ничего.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/audio_media/media_codecs.xml:system/etc/media_codecs.xml \
    $(LOCAL_PATH)/audio_media/media_codecs_google_audio.xml:system/etc/media_codecs_google_audio.xml \
    $(LOCAL_PATH)/audio_media/media_codecs_google_video.xml:system/etc/media_codecs_google_video.xml \
    $(LOCAL_PATH)/audio_media/media_codecs_google_telephony.xml:system/etc/media_codecs_google_telephony.xml \
    $(LOCAL_PATH)/audio_media/media_profiles.xml:system/etc/media_profiles.xml


# ---- Вибромотор ----
# Мотор и драйвер живы (/sys/class/timed_output/vibrator/enable, узел
# system:system), vibrator.default.so приезжает с общей конфигурацией. Не было
# только HIDL-обёртки, а в 8.1 VibratorService дёргает железо исключительно
# через android.hardware.vibrator@1.0::IVibrator — поэтому вибрации не было
# вообще, и dumpsys vibrator оставался пустым.
PRODUCT_PACKAGES += \
    android.hardware.vibrator@1.0-impl \
    android.hardware.vibrator@1.0-service


# ---- Оверлей ресурсов устройства ----
# Пока в нём только длительности вибро: штатные паттерны AOSP (1-40 мс)
# рассчитаны на LRA, а здешний ERM ниже ~130 мс не ощущается вообще.
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay


# ---- GPS ----
# Вендорный HAL (gps.default.so — так Spreadtrum называет свой, это не
# заглушка AOSP) уже едет в образ из вендорных блобов, и все 70 его внешних
# символов в 8.1 находятся. Не было только HIDL-обёртки: в 8.1 LocationManager
# работает с железом исключительно через android.hardware.gnss@1.0::IGnss,
# поэтому провайдеров в dumpsys location не было вообще.
PRODUCT_PACKAGES += \
    android.hardware.gnss@1.0-impl \
    android.hardware.gnss@1.0-service

# Стоковые конфиги + права на /dev/ttyV1 и каталоги /data/cg (см. a1000_gps.rc)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/etc/a1000_gps.rc:system/etc/init/a1000_gps.rc \
    $(LOCAL_PATH)/gps/supl.xml:system/etc/supl.xml \
    $(LOCAL_PATH)/gps/GPSenseEngine.xml:system/etc/GPSenseEngine.xml


# ---- memtrack ----
# Не было ни в порте, ни в стоке: в логе "Couldn't load memtrack module", а в
# dumpsys meminfo пустая строка Graphics — память Mali и dma-buf'ы не попадают
# в smaps, и вес приложений был занижен. Модуль читает
# /sys/kernel/debug/mali0/gpu_memory (доступ даёт a1000_memtrack.rc).
# ro.hardware здесь = "unknown", поэтому модуль ищется по ro.hardware.memtrack.
PRODUCT_PACKAGES += \
    memtrack.sc8830 \
    android.hardware.memtrack@1.0-impl \
    android.hardware.memtrack@1.0-service

PRODUCT_PROPERTY_OVERRIDES += \
    ro.hardware.memtrack=sc8830

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/etc/a1000_memtrack.rc:system/etc/init/a1000_memtrack.rc


# ---- power HAL ----
# В стоке был только power.default.so — заглушка AOSP на 5320 байт, то есть
# система никак не сообщала железу о своих намерениях. Свой HAL двигает
# границы общей политики cpufreq: потолок вниз при погашенном экране, пол вверх
# на касание и запуск приложения. Hotplug не трогаем — он уже настроен отдельно.
PRODUCT_PACKAGES += \
    power.sc8830 \
    android.hardware.power@1.0-impl \
    android.hardware.power@1.0-service

PRODUCT_PROPERTY_OVERRIDES += \
    ro.hardware.power=sc8830


# ---- Камера ----
# Вендорный camera.sc8830.so — полноценный HAL3 (SprdCamera3HWI), поэтому
# camera2 получается настоящим, а не LEGACY-прослойкой. Блоб собран под
# Android 5, и 12 его символов в 8.1 отсутствуют (главное — класс
# android::MemoryHeapIon, который Spreadtrum держала в своём libbinder):
# их закрывает libsprd_camera_shim, подключённый к блобу через patchelf.
# HIDL-сервиса камеры в сборке не было вовсе — отсюда "Number of camera
# devices: 0".
PRODUCT_PACKAGES += \
    libsprd_camera_shim \
    libface_finder \
    libmorpho_easy_hdr \
    android.hardware.camera.provider@2.4-impl \
    android.hardware.camera.provider@2.4-service \
    android.hardware.camera.device@1.0-impl \
    android.hardware.camera.device@3.2-impl

PRODUCT_PROPERTY_OVERRIDES += \
    ro.hardware.camera=sc8830

# Блоб (уже с добавленной зависимостью на шим) и его проприетарное окружение.
PRODUCT_COPY_FILES += \
    vendor/lenovo/a1000/proprietary/lib/hw/camera.sc8830.so:system/lib/hw/camera.sc8830.so \
    vendor/lenovo/a1000/proprietary/lib/libae.so:system/lib/libae.so \
    vendor/lenovo/a1000/proprietary/lib/libaf.so:system/lib/libaf.so \
    vendor/lenovo/a1000/proprietary/lib/libawb.so:system/lib/libawb.so \
    vendor/lenovo/a1000/proprietary/lib/liblsc.so:system/lib/liblsc.so \
    vendor/lenovo/a1000/proprietary/lib/libuvdenoise.so:system/lib/libuvdenoise.so \
    $(LOCAL_PATH)/rootdir/etc/a1000_camera.rc:system/etc/init/a1000_camera.rc


# ---- Подпись сборки ----
# ВАЖНО: значение свойства Android ограничено 91 символом - более длинное
# читается только через __system_property_read_callback(), и Настройки
# показывают вместо него ошибку. Строка ниже укладывается в предел.
# Адрес для донатов показывается в Настройки -> О планшете -> «Номер сборки»
# (ro.build.display.id — свободная строка) и отдельным свойством, чтобы его
# можно было достать через getprop, не разбирая строку сборки.
PRODUCT_BUILD_PROP_OVERRIDES += \
    BUILD_DISPLAY_ID="a1000 by baton4iks | TON UQAPC9J9UY8oaYV4AwjEAYIIJMswo7qVzJDkf4pzY8kVtzJ-"

PRODUCT_PROPERTY_OVERRIDES += \
    ro.baton4iks.usdt.ton=UQAPC9J9UY8oaYV4AwjEAYIIJMswo7qVzJDkf4pzY8kVtzJ-


# ---- Свойства двух SIM ----
# dsds = две SIM, одна активна в момент времени (обычный режим для этого
# железа: один приёмник на два слота). default_network задаём для обоих
# слотов, иначе телефония берёт умолчание только для первого.
PRODUCT_PROPERTY_OVERRIDES += \
    persist.radio.multisim.config=dsds \
    ro.telephony.default_network=0,0 \
    ro.multisim.simslotcount=2


# ---- Имя железа для fs_mgr ----
# Загрузчик этого планшета НЕ передаёт androidboot.hardware в командной строке
# ядра, поэтому ro.hardware = "unknown". В Android 8.1 vold ищет таблицу
# монтирования строго как /fstab.<ro.boot.hardware>, имя получалось пустым — и
# vold не находил fstab ВООБЩЕ ("Failed to open default fstab"). Отсюда и
# невидимая SD-карта: без fstab у vold нет ни одного источника дисков.
# fs_mgr_get_boot_config первым делом смотрит свойство ro.boot.<ключ>, поэтому
# достаточно задать его здесь. ro.hardware при этом НЕ меняется (init выставил
# его раньше, в первой стадии), так что поиск HAL и импорты init не затронуты.
PRODUCT_PROPERTY_OVERRIDES +=     ro.boot.hardware=sc8830


# ---- Уровень заряда между перезагрузками ----
# У устройства нет кулонометра, заряд при загрузке оценивается по напряжению,
# а на зарядке оно завышено — процент прыгал на десяток. В драйвере есть
# приём сохранённого значения (save_capacity), но писать туда было некому.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/bin/a1000_batcap.sh:system/bin/a1000_batcap.sh \
    $(LOCAL_PATH)/rootdir/etc/a1000_battery.rc:system/etc/init/a1000_battery.rc


# ---- Номинальная ёмкость аккумулятора ----
# 2000 мА·ч — паспортная ёмкость аккумулятора устройства (BL253).
# В ядре (sprd-battery.dtsi) стоит cnom = <1730> — это внутреннее значение
# драйвера питания, оно занижено; трогать его не стали, чтобы не менять расчёт
# заряда. Реальную ёмкость взять неоткуда —
# кулонометра у устройства нет, поэтому её измеряет сервис a1000_batcap.
PRODUCT_PROPERTY_OVERRIDES += \
    ro.baton4iks.batt.design_mah=2000

# ---- Свойства, правленные вручную в build.prop на устройстве ----
# Эти свойства дописывались прямо в /system/build.prop на самом планшете,
# поэтому сборка из дерева их не содержала: прошивка выходила без модема
# и без настроек графики.

# Модем и две SIM (без них не работают ни звонки, ни SIM)
PRODUCT_PROPERTY_OVERRIDES += \
    persist.msms.phone_count=2 \
    rild.libpath=/vendor/lib/libreference-ril_sp.so \
    ro.modem.w.assert=/dev/spipe_w2 \
    ro.modem.w.count=2 \
    ro.modem.w.dev=/proc/cpw/ \
    ro.modem.w.diag=/dev/slog_w \
    ro.modem.w.eth=seth_w \
    ro.modem.w.fixnv_size=0x40000 \
    ro.modem.w.id=0 \
    ro.modem.w.loop=/dev/spipe_w0 \
    ro.modem.w.nv=/dev/spipe_w1 \
    ro.modem.w.runnv_size=0x60000 \
    ro.modem.w.snd=1 \
    ro.modem.w.tty=/dev/stty_w \
    ro.modem.w.vbc=/dev/spipe_w6 \
    ro.modem.wcn.assert=/dev/spipe_wcn2 \
    ro.modem.wcn.count=1 \
    ro.modem.wcn.dev=/dev/cpwcn \
    ro.modem.wcn.diag=/dev/slog_wcn \
    ro.modem.wcn.enable=1 \
    ro.modem.wcn.id=1 \
    ro.msms.phone_count=2 \
    ro.telephony.ril_class=SprdRIL

# Графика: подобрано опытным путём, менять только с проверкой на устройстве
PRODUCT_PROPERTY_OVERRIDES += \
    debug.hwc.disable=0 \
    debug.hwc.info=0 \
    debug.sf.a1000_dblbuf=0 \
    debug.sf.a1000_flip=0 \
    debug.sf.a1000_pan=1 \
    debug.sf.disable_hwc=1 \
    debug.sf.force_gles=0 \
    debug.sf.swz=rgb \
    ro.sf.disable_triple_buffer=1

# Отладка и поведение среды выполнения
# JIT включён обратно. dalvik.vm.usejit=false остался с ранней отладки и стоил
# дорого: профили для speed-profile собирает сам JIT (ProfileSaver стартует из
# jit::Jit), поэтому без него pm.dexopt.install=speed-profile вырождался в
# verify — код приложений исполнялся интерпретатором и после прогрева.
# Замер прокрутки Настроек (dumpsys gfxinfo, 12 свайпов туда-обратно):
# «Slow UI thread» 494/873 кадров -> 322/821, «Slow bitmap uploads» 34 -> 5,
# рваных кадров 85.7% -> 80.0%. Остаток упирается уже не в CPU, а в путь
# вывода кадра: dequeueBuffer ~26 мс на кадр (dumpsys gfxinfo framestats).
PRODUCT_PROPERTY_OVERRIDES += \
    dalvik.vm.usejit=true \
    ro.secure=0


# ---- Файлы, которые раньше ставились только вручную ----
# До этого они попадали на планшет через adb push и в прошивку из
# исходников не входили: без них не поднимались Bluetooth, Wi-Fi, RIL
# и не выставлялись права на узлы sysfs.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/etc/a1000_bt.rc:system/etc/init/a1000_bt.rc \
    $(LOCAL_PATH)/rootdir/etc/a1000_gpu.rc:system/etc/init/a1000_gpu.rc \
    $(LOCAL_PATH)/rootdir/etc/a1000_logs.rc:system/etc/init/a1000_logs.rc \
    $(LOCAL_PATH)/rootdir/etc/a1000_perm.rc:system/etc/init/a1000_perm.rc \
    $(LOCAL_PATH)/rootdir/etc/a1000_rbswitch.rc:system/etc/init/a1000_rbswitch.rc \
    $(LOCAL_PATH)/rootdir/etc/a1000_ril.rc:system/etc/init/a1000_ril.rc \
    $(LOCAL_PATH)/rootdir/etc/a1000_ril_start.rc:system/etc/init/a1000_ril_start.rc \
    $(LOCAL_PATH)/rootdir/etc/a1000_usb.rc:system/etc/init/a1000_usb.rc \
    $(LOCAL_PATH)/rootdir/etc/a1000_wcn.rc:system/etc/init/a1000_wcn.rc \
    $(LOCAL_PATH)/rootdir/etc/a1000_wcnperm.rc:system/etc/init/a1000_wcnperm.rc \
    $(LOCAL_PATH)/rootdir/etc/a1000_wifi.rc:system/etc/init/a1000_wifi.rc \
    $(LOCAL_PATH)/rootdir/etc/a1000_wpa.rc:system/etc/init/a1000_wpa.rc \
    $(LOCAL_PATH)/rootdir/etc/healthd.rc:system/etc/init/healthd.rc \
    $(LOCAL_PATH)/rootdir/bin/a1000_logs.sh:system/bin/a1000_logs.sh \
    $(LOCAL_PATH)/rootdir/bin/kcap.sh:system/bin/kcap.sh \
    $(LOCAL_PATH)/rootdir/bin/bufcmp_run.sh:system/bin/bufcmp_run.sh \
    $(LOCAL_PATH)/rootdir/bin/gspdump.sh:system/bin/gspdump.sh \
    $(LOCAL_PATH)/keylayout/gpio-keys.kl:system/usr/keylayout/gpio-keys.kl \
    $(LOCAL_PATH)/keylayout/sci-keypad.kl:system/usr/keylayout/sci-keypad.kl \
    $(LOCAL_PATH)/keylayout/headset-keyboard.kl:system/usr/keylayout/headset-keyboard.kl \
    $(LOCAL_PATH)/idc/focaltech_ts.idc:system/usr/idc/focaltech_ts.idc \
    $(LOCAL_PATH)/configs/wifi/wpa_supplicant.conf:system/etc/wifi/wpa_supplicant.conf \
    $(LOCAL_PATH)/configs/wifi/wpa_supplicant_overlay.conf:system/etc/wifi/wpa_supplicant_overlay.conf \
    $(LOCAL_PATH)/configs/etc/blkid.tab:system/etc/blkid.tab \
    $(LOCAL_PATH)/configs/etc/connectivity_configure.ini:system/etc/connectivity_configure.ini \
    $(LOCAL_PATH)/configs/etc/audio_para_india:system/etc/audio_para_india \
    $(LOCAL_PATH)/configs/permissions/android.software.app_widgets.xml:system/etc/permissions/android.software.app_widgets.xml

# A1000: SELinux, губернатор и ориентация акселерометра.
#
# a1000_selabel.sh расставляет метки вендорным узлам (/dev/mali0, батарея,
# подсветка, cpufreq, вибромотор, трубы WCN) — без них enforcing валит
# графику, Wi-Fi, камеру и виброотклик. Он же по свойству
# persist.a1000.selinux переводит систему в enforcing.
#
# ВАЖНО: init в 8.1 разбирает ВСЕ файлы в /system/etc/init, а не только *.rc.
# Бэкапы туда класть нельзя — отработают следом и перебьют значения.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/bin/a1000_selabel.sh:system/bin/a1000_selabel.sh \
    $(LOCAL_PATH)/rootdir/etc/a1000_selinux.rc:system/etc/init/a1000_selinux.rc \
    $(LOCAL_PATH)/rootdir/etc/a1000_cpu.rc:system/etc/init/a1000_cpu.rc \
    $(LOCAL_PATH)/rootdir/etc/a1000_gsensor.rc:system/etc/init/a1000_gsensor.rc

# Enforcing включается сам после загрузки. Ранняя загрузка всегда permissive,
# поэтому неудачная политика не запирает устройство: достаточно
# перезагрузиться и снять свойство.
PRODUCT_PROPERTY_OVERRIDES += \
    persist.a1000.selinux=1


# ---- Оффлайн-зарядка: штатный вид LineageOS ----
# Картинка (бирюзовый круг, 22 кадра) и шрифт процентов — из
# vendor/lineage/charger ветки lineage-20.0, hdpi. Лежат тут копиями и с
# префиксом lineage_, чтобы не столкнуться с модулем charger_res_images;
# рисует их AOSP-овый healthd (хуков healthd_board_mode_charger_* в этом
# system/core нет), имена берутся из animation.txt.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/charger/animation.txt:root/res/values/charger/animation.txt \
    $(LOCAL_PATH)/charger/lineage_battery_scale.png:root/res/images/charger/lineage_battery_scale.png \
    $(LOCAL_PATH)/charger/lineage_battery_fail.png:root/res/images/charger/lineage_battery_fail.png \
    $(LOCAL_PATH)/charger/lineage_percent_font.png:root/res/images/charger/lineage_percent_font.png

# ---- Модем: NV для CP ----
# Без persist.modem.w.nvp демон nvitemd (и modem_control) завершается сразу же,
# CP остаётся без NV-данных: AT+CGSN -> ERROR (IMEI пустой), AT+CFUN=1 -> ERROR
# (радио не включается вообще). В стоковом Android 5 обе строки лежали в
# build.prop; при портировании потерялись, а ro.modem.w.* переехали.
PRODUCT_PROPERTY_OVERRIDES += \
    persist.modem.w.enable=1 \
    persist.modem.w.nvp=w \
    persist.sys.sprd.modemreset=1
