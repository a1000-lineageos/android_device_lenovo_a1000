#!/system/bin/sh
# zram_setup.sh — activate compressed swap on the 1GB Lenovo A1000 (sc8830).
# Kernel: drivers/staging/zram (LZO, no comp_algorithm sysfs). Built-in -> zram0
# exists early. Android 9 does not auto-enable zram from ro.config.zram, and the
# fstab has no swap line, so we set it up here on `on post-fs`.

ZRAM=/sys/block/zram0
ZSIZE=268435456          # 256 MiB (было 768 MiB = 82% ОЗУ; своп всё равно не использовался)

[ -e "$ZRAM/disksize" ] || exit 0

# resolve the block device node (ueventd usually makes /dev/block/zram0)
DEV=/dev/block/zram0
if [ ! -e "$DEV" ]; then
    if [ -e /dev/zram0 ]; then
        DEV=/dev/zram0
    else
        MM=$(cat "$ZRAM/dev" 2>/dev/null)   # "major:minor"
        mknod /dev/block/zram0 b "${MM%:*}" "${MM#*:}" 2>/dev/null && DEV=/dev/block/zram0
    fi
fi

# idempotent: tear down any previous swap before resizing
swapoff "$DEV" 2>/dev/null
echo 1 > "$ZRAM/reset" 2>/dev/null
echo "$ZSIZE" > "$ZRAM/disksize"
/system/bin/mkswap "$DEV"
# high priority so anon pages prefer fast zram
/system/bin/swapon "$DEV" -p 32 2>/dev/null || /system/bin/swapon "$DEV"

# Low-RAM tuning: prefer swapping anon to (fast) zram over evicting file/code
# pages, and read one page at a time to cut zram decompress latency.
echo 100 > /proc/sys/vm/swappiness
echo 0   > /proc/sys/vm/page-cluster
echo 10  > /proc/sys/vm/vfs_cache_pressure

# status marker (readable from TWRP: /cache survives a normal boot)
{
    echo "zram_setup ran; dev=$DEV disksize=$(cat $ZRAM/disksize 2>/dev/null)"
    echo "swaps:"; cat /proc/swaps 2>/dev/null
    echo "swappiness=$(cat /proc/sys/vm/swappiness 2>/dev/null)"
} > /cache/zram.log 2>&1
