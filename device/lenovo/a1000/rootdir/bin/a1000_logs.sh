#!/system/bin/sh
# A1000: постоянный сбор логов в /cache/logs.
#
# Зачем /cache, а не /data: раздел маленький, отдельный, и TWRP умеет положить
# его в бэкап целиком — то есть человеку не надо ничего искать вручную, он
# просто делает бэкап cache и присылает архив.
#
# Главная ценность — логи ПРЕДЫДУЩЕЙ загрузки. Ядро на этом устройстве собрано
# без pstore/ram_console, поэтому /proc/last_kmsg не существует и после
# зависания или сброса узнать причину неоткуда. Здесь это решается иначе: при
# каждом старте прошлые файлы переименовываются в *.prev, а живые логи
# дописываются с запасом по времени. Что успело записаться до сбоя — останется.

# --- 0. Гасим вендорный дампер логов в сырой раздел cache (A1000_NODBG) ----
# В /init (первая стадия, до перехода в домен SELinux — контекст так и остаётся
# u:r:kernel:s0) вшит спредтрумовский дампер: он форкается на 5-й секунде,
# открывает /dev/cache18dbg — это СЫРОЙ блочный раздел cache, 179:18, тот самый,
# что смонтирован в /cache, — и в цикле пишет туда кернел-лог. Видно по
# denied { write } for comm="init" path="/dev/cache18dbg" в audit.
#
# Стоит он 20 % одного ядра НЕПРЕРЫВНО (замер дельтой utime+stime по всем
# /proc/*/stat: 215 тиков за 10 с при HZ=100) — то есть почти всё системное
# время процессора на простое. На глаз: с ним прокрутка рвётся на 80 % кадров
# при медиане 46 мс, без него — 74-78 % при 42-44 мс.
#
# Смысла в нём нет: логи прошлой загрузки собирает этот же скрипт, а в ядре #77
# вдобавок работает pstore (см. /proc/consoles).
#
# Опознаётся однозначно: cmdline РОВНО "/init" и родитель — init. Subcontext-ы
# init'а имеют cmdline "/initsubcontextu:r:vendor_init:s0N" и под условие не
# попадают, сам init отсеян по pid.
for p in /proc/[0-9]*; do
    pid=${p#/proc/}
    [ "$pid" = 1 ] && continue
    [ "$(cat $p/cmdline 2>/dev/null | tr -d '\000')" = "/init" ] || continue
    set -- $(cat $p/stat 2>/dev/null)
    [ "$4" = "1" ] && kill -9 $pid
done

LOGDIR=/cache/logs
KEEP_FREE_MB=25          # ниже этого порога чистимся, чтобы не забить /cache
DMESG_PERIOD=20          # как часто перезаписывать снимок кольцевого буфера ядра

mkdir -p $LOGDIR
chmod 0777 $LOGDIR

# --- 1. Ротация: логи прошлой загрузки становятся *.prev -------------------
for f in uboot kmsg kmsg_boot logcat logcat_boot props; do
    rm -f $LOGDIR/$f.prev.txt
    [ -f $LOGDIR/$f.txt ] && mv -f $LOGDIR/$f.txt $LOGDIR/$f.prev.txt
done
rm -f $LOGDIR/logcat.txt.1 $LOGDIR/logcat.txt.2 $LOGDIR/logcat.txt.3

# --- 2. Журнал загрузок ----------------------------------------------------
# Накопительный, одна строка на загрузку. Отвечает на вопрос «почему оно
# перезагрузилось»: rst_mode пишет ядро перед сбросом, а «watchdog int raw»
# показывает, был ли это сторожевой таймер (то есть перезагрузка) или холодный
# старт по кнопке.
UBOOT=$LOGDIR/uboot.txt
dd if=/proc/bootloader/log_buf bs=4096 count=64 2>/dev/null | tr -d '\000' > $UBOOT

RST=$(grep -m1 'rst_mode==' $UBOOT | tr -d '\r\n')
WDG=$(grep -m1 'hw watchdog int raw' $UBOOT | tr -d '\r\n')
MODE=$(grep -m1 'boot mode is' $UBOOT | tr -d '\r\n')
{
    echo "=== $(date '+%Y-%m-%d %H:%M:%S') ==="
    echo "kernel : $(cat /proc/version)"
    echo "build  : $(getprop ro.build.display.id) / $(getprop ro.build.date)"
    echo "reset  : $RST | $WDG | $MODE"
} >> $LOGDIR/boots.txt

# --- 3. Снимок свойств и разделов -----------------------------------------
{
    echo "=== getprop ==="; getprop
    echo; echo "=== cmdline ==="; cat /proc/cmdline
    echo; echo "=== df ==="; df -h
    echo; echo "=== watchdog ==="; cat /proc/a1000_wdg 2>/dev/null
} > $LOGDIR/props.txt 2>&1

# --- 3a. Отдельный снимок РАННЕЙ загрузки ---------------------------------
# Кольцевой буфер ядра на этом устройстве забивается сообщениями audit за
# считанные минуты, и к моменту, когда лог кому-то понадобился, начала загрузки
# в нём уже нет. Поэтому снимаем dmesg один раз, сразу при старте службы, в
# отдельный файл, который дальше не перезаписывается.
dmesg > $LOGDIR/kmsg_boot.txt 2>/dev/null
logcat -b all -d -v threadtime > $LOGDIR/logcat_boot.txt 2>/dev/null

# --- 4. logcat пишется сам, с ротацией ------------------------------------
# 4 МБ на файл, 3 файла в запасе: примерно сутки обычной работы.
logcat -b all -v threadtime -f $LOGDIR/logcat.txt -r 4096 -n 3 &

# --- 5. Кольцевой буфер ядра снимается по кругу ----------------------------
# dmesg перезаписывается целиком: буфер 2 МБ (log_buf_len=2M в cmdline), так
# что в файле всегда последние сообщения, включая те, что были перед сбоем.
while true; do
    dmesg > $LOGDIR/kmsg.txt 2>/dev/null

    # tombstones и ANR — если что-то падало, это самое важное
    if [ -d /data/tombstones ]; then
        mkdir -p $LOGDIR/tombstones
        cp -f /data/tombstones/* $LOGDIR/tombstones/ 2>/dev/null
    fi
    if [ -f /data/anr/traces.txt ]; then
        cp -f /data/anr/traces.txt $LOGDIR/anr_traces.txt 2>/dev/null
    fi

    # если /cache подходит к концу — выкидываем самое старое
    FREE=$(df -m /cache 2>/dev/null | tail -1 | awk '{print $4}')
    case "$FREE" in
        ''|*[!0-9]*) ;;
        *) if [ "$FREE" -lt "$KEEP_FREE_MB" ]; then
               rm -f $LOGDIR/logcat.txt.3 $LOGDIR/logcat.txt.2
               rm -f $LOGDIR/*.prev.txt
               rm -rf $LOGDIR/tombstones
           fi ;;
    esac

    chmod -R 0666 $LOGDIR/*.txt 2>/dev/null
    sleep $DMESG_PERIOD
done
