#!/system/bin/sh
# A1000: расставить SELinux-метки вендорным узлам.
#
# ЗАЧЕМ. В enforcing почти всё, обо что спотыкалась система, — проблема МЕТОК,
# а не отсутствующих правил: узлы остались с общими типами (device, sysfs),
# которых базовой политике никто не разрешал. Нужные типы (gpu_device,
# sysfs_batteryinfo, sysfs_leds, sysfs_devices_system_cpu, radio_device) в
# политике уже есть и уже разрешены нужным доменам.
#
# Немногое, что метками не лечится, дописано ПРЯМО В ДВОИЧНУЮ ПОЛИТИКУ
# инструментом a1000_sepolicy_inject (см. firmware/_sepolicy_rules.txt):
# пересобирать дерево не пришлось, рабочий ramdisk собран другим деревом.
#
# ПОЧЕМУ find, А НЕ ЦИКЛ ПО "$dir"/*
# Первая версия ходила по маске и звала chcon на всё подряд. В sysfs среди
# файлов лежат симлинки (device, subsystem), chcon идёт ПО ССЫЛКЕ — и каталог
# /sys/devices/sdio_emmc уехал в sysfs_leds. Вторая версия обернула это в
# функцию с проверками -f и -L, и переклейка перестала срабатывать вовсе:
# батарея осталась с типом sysfs, healthd не смог её читать и показывал 0 %.
# find -type f не ходит по ссылкам сам и не зависит от тонкостей оболочки.

set -x

# Графика: без этого у SurfaceFlinger и приложений нет ioctl к GPU.
chcon u:object_r:gpu_device:s0 /dev/mali0

# Трубы к сопроцессору WCN. Под init работает wcnd; в политику дописано
# allow init radio_device:chr_file. Без этого не поднимаются Wi-Fi и BT.
for f in /dev/spipe_wcn* /dev/slog_wcn /dev/cpwcn; do
    [ -e "$f" ] && chcon u:object_r:radio_device:s0 "$f" 2>/dev/null
done

# ВНИМАНИЕ: -path в здешнем toybox не работает (даёт 0 файлов), поэтому
# обходим каталоги классов. Слэш в конце важен: он заставляет find пойти ЧЕРЕЗ
# симлинк класса в реальный каталог, а -maxdepth 1 -type f не даёт зацепить
# вложенные ссылки device/subsystem.

# Батарея: healthd читает capacity/status/present/voltage_now/current_now.
for d in /sys/class/power_supply/*; do
    find "$d/" -maxdepth 1 -type f 2>/dev/null | while read f; do
        chcon u:object_r:sysfs_batteryinfo:s0 "$f" 2>/dev/null
    done
done

# Подсветка и светодиоды: hal_light.
for d in /sys/class/backlight/* /sys/class/leds/*; do
    find "$d/" -maxdepth 1 -type f 2>/dev/null | while read f; do
        chcon u:object_r:sysfs_leds:s0 "$f" 2>/dev/null
    done
done

# Частоты: hal_power пишет scaling_min_freq.
find /sys/devices/system/cpu -type f 2>/dev/null | while read f; do
    chcon u:object_r:sysfs_devices_system_cpu:s0 "$f" 2>/dev/null
done

# Вибромотор. Узел общего типа sysfs, а hal_vibrator_default писать туда не
# разрешено — из-за этого пропал виброотклик. Тип sysfs_vibrator в политике
# уже есть и уже разрешён этому домену.
# readlink здесь НЕ используем: он читает корень rootfs, а домену toolbox это
# не разрешено — в журнале появлялся лишний отказ. Слэш в конце пути и так
# ведёт find ЧЕРЕЗ симлинк класса, как в соседних местах выше.
for d in /sys/class/timed_output/*; do
    find "$d/" -maxdepth 1 -type f 2>/dev/null | while read f; do
        chcon u:object_r:sysfs_vibrator:s0 "$f" 2>/dev/null
    done
done

# Спящий режим.
chcon u:object_r:sysfs_power:s0 /sys/power/state 2>/dev/null

# Наш шим. Метку system_lib_file ставить НЕЛЬЗЯ: такого типа в здешней
# политике нет, ядро видит файл как unlabeled и запрещает его исполнять —
# из-за этого не поднималась камера. Соседние библиотеки имеют system_file.
# Клеим ТОЛЬКО если метка отличается: на /system метки хранятся в самой
# файловой системе и переживают перезагрузку, а лишний chcon упирается в
# запрет relabelfrom и сорит в журнале.
case "$(ls -Z /system/lib/libui_shim.so 2>/dev/null)" in
    *system_file*) ;;
    *) chcon u:object_r:system_file:s0 /system/lib/libui_shim.so 2>/dev/null ;;
esac

# Переход в enforcing — только по свойству, по умолчанию выключено:
#   setprop persist.a1000.selinux 1   (и перезагрузка)
# Ранняя загрузка ВСЕГДА permissive: скрипт срабатывает после
# sys.boot_completed, значит запереть себя нельзя.
if [ "$(getprop persist.a1000.selinux)" = "1" ]; then
    setenforce 1
fi
