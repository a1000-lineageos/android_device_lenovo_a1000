#!/system/bin/sh
# Дамп ЗАРЕЗЕРВИРОВАННОГО fb (0x9f82d000) и буфера композитора (0x9ffcc000).
# Если в fb лежит картинка "недавних", значит копия gralloc доходит и виноват вывод.
# Если fb чёрный — значит fb_post до него не доносит кадр.
wait_up() {
    while true; do
        u=$(cut -d. -f1 /proc/uptime)
        [ "$u" -ge "$1" ] && return
        sleep 2
    done
}
wait_up 150; insmod /system/lib/modules/bufcmp.ko pa=0x9f82d000 pb=0x9ffcc000 tag=1 2>/dev/null; sync
wait_up 180; insmod /system/lib/modules/bufcmp.ko pa=0x9f82d000 pb=0x9ffcc000 tag=2 2>/dev/null; sync
wait_up 210; insmod /system/lib/modules/bufcmp.ko pa=0x9f82d000 pb=0x9ffcc000 tag=3 2>/dev/null; sync
