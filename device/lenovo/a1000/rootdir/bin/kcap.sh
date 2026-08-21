#!/system/bin/sh
# Лёгкий вариант захвата: без screencap и без чтения /proc/dispc_osd.
# Они и грузили GPU (полный сброс экрана + PNG) и шину (4.6 МБ на тик).
# Зато цикл длиннее — доживает до блокировки экрана, чтобы поймать белый экран.
TB=/system/bin/toybox
mkdir -p /cache/cap 2>/dev/null
rm -f /cache/kmsg_stream.txt /cache/logcat_boot.txt /cache/progress.txt 2>/dev/null
rm -f /cache/dispc_trace.txt /cache/osd_*.raw /cache/osd_thumb.txt 2>/dev/null

echo 1 > /proc/sys/kernel/print-fatal-signals
echo 0 > /proc/sys/kernel/randomize_va_space
# 4 ядра онлайн, hotplug выключен (иначе плавает число ядер)
echo 1 > /sys/devices/system/cpu/cpufreq/sprdemand/cpu_hotplug_disable 2>/dev/null
for c in 1 2 3; do echo 1 > /sys/devices/system/cpu/cpu$c/online 2>/dev/null; done

echo "PERMCHECK backlight: $(ls -l /sys/class/backlight/sprd_backlight/brightness)" > /cache/permcheck.txt
echo "PERMCHECK usbcfgfs: $(getprop sys.usb.configfs) state=$(getprop sys.usb.state) cfg=$(getprop sys.usb.config)" >> /cache/permcheck.txt
echo "PERMCHECK adbd: $(getprop init.svc.adbd)" >> /cache/permcheck.txt
/system/bin/logcat -v threadtime -b main -b system -b crash > /cache/logcat_boot.txt &
cat /proc/kmsg > /cache/kmsg_stream.txt &

# 300 тиков по 2 с = ~10 минут: успеешь дойти до рабочего стола и нажать питание
i=0
while [ $i -lt 300 ]; do
  UP=$($TB cut -d' ' -f1 /proc/uptime)
  echo "up=$UP boot=$(getprop sys.boot_completed) screen=$(getprop debug.sf.showupdates)" >> /cache/progress.txt
  echo "=== t=$UP ===" >> /cache/dispc_trace.txt
  $TB cat /sys/class/graphics/fb0/dispc_dump >> /cache/dispc_trace.txt 2>&1
  i=$((i+1))
  sleep 2
done
sync
