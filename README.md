# LineageOS 15.1 для телефона Lenovo A1000 (SC8830)

Дерево устройства и правки исходников AOSP для Android 8.1 на Lenovo A1000.
Исходники ядра — отдельный репозиторий:
<https://github.com/Breead1337/lenovoa1000kernel3.10sourceENHANCED>
Пребилт ядра здесь: `device/lenovo/a1000/prebuilt/kernel`, сборка **#89**.

## Что внутри

    device/lenovo/a1000/          дерево устройства целиком
      prebuilt/                   ядро #89, mali.ko, sprd.dtb, ramdisk.img
      rootdir/bin/                a1000_selabel.sh (метки SELinux), batcap, logs
      rootdir/etc/                init-скрипты: SELinux, губернатор, gsensor,
                                  rbswitch, батарея, BT, камера, GPS, Wi-Fi
      sepolicy/                   правила политики для устройства
      camera/                     шим камеры и загрузчик стоковых блобов
      configs/ overlay/ idc/ keylayout/ audio/ power/ memtrack/ gps/ charger/

    external/libui_shim/          шим вокруг framebuffer HAL: вывод кадра без
                                  копирования, page flip, честная частота панели
    external/tinyalsa/            звук
    external/wpa_supplicant_8/    MAC-адрес Wi-Fi
    external/noto-fonts/          урезание шрифтов
    external/chromium-webview/    урезание webview

    frameworks/native/            SurfaceFlinger: HWComposer_hwc1,
                                  SurfaceFlinger_hwc1, FramebufferSurface,
                                  Layer, ProgramCache (перестановка каналов),
                                  Gralloc2, HWC2On1Adapter
    frameworks/base/              SystemUI: PanelView

    hardware/ril/                 libril (SIM, уровень сигнала, IMEI), rild
    hardware/interfaces/          bluetooth, sensors, audio

    system/bt/                    стек Bluetooth
    system/core/                  init, healthd
    system/connectivity/wificond/ netlink

    bootable/recovery/            minui для этого экрана
    vendor/lenovo/a1000/          a1000-vendor.mk (сами блобы не выкладываются)

    tools/                        a1000_sepolicy_inject.c — дописать правила в
                                    готовую двоичную политику
                                  a1000_bootimg_tool.py — разбор и пересборка
                                    boot.img (info / kernel / replace)
                                  sepolicy_rules.txt — 93 правила

## Как класть в дерево

Каталоги повторяют раскладку AOSP: содержимое кладётся поверх дерева
LineageOS 15.1 по тем же путям.

## Инструменты

    gcc -O2 -w -I external/selinux/libsepol/include -I external/selinux/libsepol/src \
        tools/a1000_sepolicy_inject.c external/selinux/libsepol/src/*.c -o sepinject
    ./sepinject <политика> tools/sepolicy_rules.txt <результат>

    python3 tools/a1000_bootimg_tool.py info    boot.img
    python3 tools/a1000_bootimg_tool.py kernel  boot.img Image out.img
    python3 tools/a1000_bootimg_tool.py replace boot.img sepolicy новый out.img
