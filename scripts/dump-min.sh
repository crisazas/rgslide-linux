#!/bin/sh
# Volcado minimo de solo lectura de la Slide por adb + su (Magisk): identidad/calibracion y cadena de arranque.
# Uso: scripts/dump-min.sh [serie]   -> dump/min/<nombre>.img + MD5SUMS (md5 del equipo comparado con el del PC)
set -eu
cd "$(dirname "$0")/.."
S=${1:-$(adb get-serialno)}
OUT=dump/min; mkdir -p "$OUT"
A="adb -s $S"
PARTS="prodnv miscdata misc calinv persist nr_fixnv1_a nr_fixnv1_b nr_fixnv2_a nr_fixnv2_b nr_runtimenv1 nr_runtimenv2 spl_a spl_b uboot_a uboot_b sml_a sml_b trustos_a trustos_b vbmeta_a vbmeta_b"
: > "$OUT/MD5SUMS"
$A exec-out 'su -c "dd if=/dev/block/sda bs=512 count=34 2>/dev/null"' > "$OUT/gpt-head.bin"
for p in $PARTS; do
  $A exec-out "su -c 'dd if=/dev/block/by-name/$p bs=1048576 2>/dev/null'" > "$OUT/$p.img"
  dev=$($A shell "su -c 'md5sum /dev/block/by-name/$p'" | cut -d' ' -f1)
  loc=$(md5sum "$OUT/$p.img" | cut -d' ' -f1)
  sz=$(stat -c %s "$OUT/$p.img")
  if [ "$dev" = "$loc" ] && [ "$sz" -gt 0 ]; then st=OK; else st=FALLO; fi
  echo "$loc  $p.img  $sz  $st" | tee -a "$OUT/MD5SUMS"
done
