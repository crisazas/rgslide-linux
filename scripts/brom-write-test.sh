#!/bin/sh
# Prueba de ESCRITURA por BROM con riesgo minimo: lee `misc`, escribe en `misc` su propio volcado verificado (dump/min/misc.img,
# md5 534e94b7...), lo vuelve a leer y compara. Rutas absolutas (spd_dump cambia de directorio).
# Uso: scripts/brom-write-test.sh [segundos]   (la Slide debe entrar en BROM: Back mantenido al enchufar el USB)
set -eu
cd "$(dirname "$0")/.."
R=$(pwd); WAIT=${1:-300}
MISC=$R/dump/min/misc.img
[ "$(md5sum "$MISC" | cut -c1-32)" = "534e94b7a56e8c17e71d5d3e1d50295c" ] || { echo "misc.img no es el volcado verificado"; exit 1; }
OUT=$R/out/brom-write-$(date +%H%M%S); mkdir -p "$OUT/pre" "$OUT/post"
echo "salida en $OUT; esperando BROM hasta $WAIT s"
tools/spd_dump --wait "$WAIT" \
  fdl "$R/dump/fdl/fdl1-dl.bin" 0x65000800 \
  fdl "$R/dump/fdl/fdl2-dl.bin" 0xb4fffe00 \
  exec \
  path "$OUT/pre" r misc \
  write_part misc "$MISC" \
  path "$OUT/post" r misc \
  reset 2>&1 | tee "$OUT/spd_dump.log"
echo "--- comparacion ---"
ls -l "$OUT/pre" "$OUT/post"
for f in "$OUT"/pre/* "$OUT"/post/*; do [ -f "$f" ] && md5sum "$f"; done
echo "esperado: $(md5sum "$MISC" | cut -c1-32)"
