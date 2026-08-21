#!/system/bin/sh
# A1000: (1) сохранение уровня заряда между перезагрузками,
#        (2) измерение реальной ёмкости аккумулятора за цикл зарядки.
#
# (1) У устройства нет кулонометра, при загрузке заряд оценивается по
# напряжению, а на зарядке оно завышено током — процент прыгал. Драйвер умеет
# принять сохранённое значение через save_capacity и САМ решает, доверять ли
# ему (отвергает при расхождении больше cap-valid-range-poweron = 30%).
#
# (2) Реальную ёмкость взять неоткуда — считаем сами, накапливая ток*время.

SYS=/sys/class/power_supply/battery
STORE=/data/misc/last_battery_capacity
MARK=/data/misc/last_battery_capacity.log
STATE=/data/misc/batt_wear.state
INTERVAL=30
DESIGN=$(getprop ro.baton4iks.batt.design_mah)
[ -z "$DESIGN" ] && DESIGN=2000

is_num() {
    case "$1" in
        ''|*[!0-9]*) return 1;;
        *) return 0;;
    esac
}

say() {
    log -t a1000_batcap "$1"
    echo "$(date): $1" >> "$MARK" 2>/dev/null
}

# --- восстановление уровня заряда ---
if [ -f "$STORE" ]; then
    saved=$(cat "$STORE" 2>/dev/null)
    if is_num "$saved" && [ "$saved" -ge 1 ] && [ "$saved" -le 100 ]; then
        # Ошибку НЕ глушим: если записать не вышло, это надо видеть.
        if echo "$saved" > $SYS/save_capacity; then
            say "восстановлен заряд: $saved% (оценка драйвера была $(cat $SYS/capacity 2>/dev/null)%)"
        else
            say "ОШИБКА: не удалось записать save_capacity (uid=$(id -u))"
        fi
    else
        say "сохранённое значение негодное: '$saved'"
    fi
else
    say "сохранённого заряда нет, первый запуск"
fi

start_cap=""
acc=0
[ -f "$STATE" ] && read start_cap acc < "$STATE" 2>/dev/null
is_num "$acc" || acc=0

while true; do
    cap=$(cat $SYS/capacity 2>/dev/null)
    st=$(cat $SYS/status 2>/dev/null)
    cur=$(cat $SYS/real_time_current 2>/dev/null)

    # --- сохранение уровня заряда ---
    if is_num "$cap" && [ "$cap" -ge 1 ] && [ "$cap" -le 100 ]; then
        echo "$cap" > "$STORE"
    fi

    # --- измерение ёмкости ---
    if is_num "$cap" && is_num "$cur"; then
        case "$st" in
        Charging)
            if [ -z "$start_cap" ]; then
                # начинаем только с низкого заряда: чем длиннее участок,
                # тем меньше влияет погрешность пересчёта на 100%
                if [ "$cap" -le 20 ]; then
                    start_cap=$cap
                    acc=0
                    echo "$start_cap $acc" > "$STATE"
                    say "начато измерение ёмкости с $start_cap%"
                fi
            else
                acc=$((acc + cur * INTERVAL))
                echo "$start_cap $acc" > "$STATE"
            fi
            ;;
        Full)
            if [ -n "$start_cap" ]; then
                delta=$((100 - start_cap))
                if [ "$delta" -ge 50 ]; then
                    full=$(( acc / 3600 * 100 / delta ))
                    if [ "$full" -ge 500 ] && [ "$full" -le 3000 ]; then
                        wear=$(( 100 - full * 100 / DESIGN ))
                        [ "$wear" -lt 0 ] && wear=0
                        setprop persist.baton4iks.batt.real_mah "$full"
                        setprop persist.baton4iks.batt.wear "$wear"
                        say "измерена ёмкость: $full мА·ч из $DESIGN, износ $wear% (участок $start_cap->100%)"
                    else
                        say "измерение отброшено: получилось $full мА·ч, вне разумных границ"
                    fi
                else
                    say "измерение отброшено: участок всего $delta%"
                fi
            fi
            start_cap=""; acc=0; rm -f "$STATE"
            ;;
        *)
            # зарядку отключили до полной — участок неполный, начинаем заново
            if [ -n "$start_cap" ]; then
                say "измерение прервано: отключили зарядку на $cap%"
                start_cap=""; acc=0; rm -f "$STATE"
            fi
            ;;
        esac
    fi

    sleep $INTERVAL
done
