#!/bin/sh
# Recuperacion por BROM: devuelve `misc` al estado volcado en la fase 0 (slot A activo) y reinicia.
# Direcciones de carga de los FDL: las del .pac de la Slide (dump/pac/ums9620_2h10.xml), NO las de la T618.
# Uso: scripts/brom-restore-misc.sh [segundos_de_espera]   (lanzarlo y despues entrar en BROM: Back mantenido al enchufar el USB)
set -eu
cd "$(dirname "$0")/.."
WAIT=${1:-900}
MISC=dump/min/misc.img
[ "$(md5sum "$MISC" | cut -c1-32)" = "534e94b7a56e8c17e71d5d3e1d50295c" ] || { echo "misc.img no es el volcado verificado"; exit 1; }
OUT=out/brom-$(date +%H%M%S); mkdir -p "$OUT/pre" "$OUT/post"
echo "salida en $OUT; esperando BROM (1782:4d00) hasta $WAIT s"
tools/spd_dump --wait "$WAIT" \
  fdl dump/fdl/fdl1-dl.bin 0x65000800 \
  fdl dump/fdl/fdl2-dl.bin 0xb4fffe00 \
  exec \
  path "$OUT/pre" r misc \
  write_part misc "$MISC" \
  path "$OUT/post" r misc \
  reset 2>&1 | tee "$OUT/spd_dump.log"
echo "--- comparacion ---"
md5sum "$OUT"/pre/misc.bin "$OUT"/post/misc.bin "$MISC" 2>/dev/null || ls -l "$OUT/pre" "$OUT/post"
