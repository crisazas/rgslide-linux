#!/bin/sh
# Tras una prueba de boot_b, desde GammaOS con root: guarda pstore (consola/dmesg del kernel de la prueba), las marcas de boot_b,
# el estado de misc y el uboot_log, en dump/live/test-<hora>/. No escribe nada en la Slide.
# Uso: scripts/collect-test.sh [serie]
set -u
cd "$(dirname "$0")/.."
S=${1:-RGSLD005266585}; A="adb -s $S"
D=dump/live/test-$(date +%H%M%S); mkdir -p "$D"
echo "slot: $($A shell getprop ro.boot.slot_suffix | tr -d '\r')"
for f in $($A shell 'su -c "ls /sys/fs/pstore"' | tr -d '\r'); do $A shell "su -c 'cat /sys/fs/pstore/$f'" > "$D/$f.txt" 2>/dev/null; done
$A exec-out 'su -c "dd if=/dev/block/by-name/misc bs=4096 count=1 2>/dev/null"' > "$D/misc.bin"
$A exec-out 'su -c "dd if=/dev/block/by-name/boot_b bs=4096 skip=12288 count=64 2>/dev/null"' > "$D/marks.bin"
$A exec-out 'su -c "dd if=/dev/block/by-name/uboot_log bs=1048576 2>/dev/null"' | strings -n 8 > "$D/uboot_log.txt"
python3 - "$D" <<'PY'
import sys,struct
d=sys.argv[1]; m=open(d+'/misc.bin','rb').read(); o=0x800
print('misc: suffix',m[o:o+2],[('ab'[i],'prio',m[o+12+2*i]&15,'tries',(m[o+12+2*i]>>4)&7,'ok',m[o+12+2*i]>>7) for i in range(2)])
k=open(d+'/marks.bin','rb').read()
print('marcas en boot_b:', 'SI' if k[:12]==b'RGSLIDE-MARK' else 'no (zona a ceros)')
PY
echo "== marcas"; strings -n 3 "$D/marks.bin" | head -80
echo "== claves en el pstore"; grep -a -h -n -E 'rgslide|MARCA-|Kernel panic|Run /init|Unable to mount|not initramfs|RAMDISK|udc|dwc3|rndis|ufs' "$D"/*ramoops*.txt 2>/dev/null | cut -c1-190 | head -60
echo "guardado en $D"
