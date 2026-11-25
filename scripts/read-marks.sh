#!/bin/sh
# Lee las marcas que dejo initramfs/init en la cola libre de boot_b (48 MiB + 0, 256 KiB) desde Android con root.
# Uso: scripts/read-marks.sh [serie]  -> imprime las marcas y las guarda en dump/marks/marks-<hora>.txt
set -eu
cd "$(dirname "$0")/.."
S=${1:-RGSLD005266585}
OUT=dump/marks/marks-$(date +%H%M%S).txt
adb -s "$S" exec-out 'su -c "dd if=/dev/block/by-name/boot_b bs=4096 skip=12288 count=64 2>/dev/null"' > "$OUT.bin"
if head -c 16 "$OUT.bin" | grep -a -q 'RGSLIDE-MARK'; then
  strings -n 3 "$OUT.bin" > "$OUT"; echo "marcas ENCONTRADAS -> $OUT"; cat "$OUT"
else
  echo "sin marcas (el /init no llego a escribirlas)"; xxd -l 32 "$OUT.bin"
fi
