#!/bin/sh
# Prueba una imagen de arranque SOLO en RAM (fastboot boot, sin escribir en flash) y recoge el pstore al volver a Android.
# Uso: scripts/try-boot.sh out/boot-min.img [segundos_de_espera] [serie]
set -u
cd "$(dirname "$0")/.."
IMG=$1; WAIT=${2:-90}; S=${3:-RGSLD005266585}
FB=$HOME/Android/Sdk/platform-tools/fastboot
TAG=$(basename "$IMG" .img)-$(date +%H%M%S)
OUT=dump/live/try/$TAG; mkdir -p "$OUT"
adb -s "$S" reboot bootloader 2>/dev/null
i=0; while [ $i -lt 40 ]; do $FB devices 2>/dev/null | grep -q fastboot && break; sleep 2; i=$((i+1)); done
$FB devices | grep -q fastboot || { echo "no entro en fastboot"; exit 2; }
[ "$($FB getvar current-slot 2>&1 | head -1)" = "current-slot: a" ] || { echo "slot actual no es a: no pruebo"; exit 3; }
$FB boot "$IMG" 2>&1 | tail -2
t=0; seen=""
while [ $t -lt "$WAIT" ]; do
  sleep 2; t=$((t+2))
  u=$(lsusb | grep -E '1d6b:0104|18d1:|1782:' | tr '\n' ';')
  [ "$u" != "$seen" ] && { echo "t=${t}s usb: ${u:-nada}"; seen=$u; }
  adb -s "$S" get-state 2>/dev/null | grep -q device && { echo "t=${t}s adb de vuelta"; break; }
done
echo "$seen" > "$OUT/usb.txt"
if adb -s "$S" get-state 2>/dev/null | grep -q device; then
  adb -s "$S" shell 'su -c "cd /sys/fs/pstore && for f in *; do echo \"=== \$f\"; cat \$f; done"' > "$OUT/pstore.txt" 2>&1
  adb -s "$S" shell 'su -c "cat /proc/cmdline"' > "$OUT/cmdline.txt" 2>&1
  echo "pstore: $OUT/pstore.txt ($(wc -c < "$OUT/pstore.txt") B)"
fi
