#!/bin/sh
# Prueba de SOLO LECTURA de la ruta BROM: carga FDL1/FDL2 del .pac y lista las particiones. No escribe nada.
# Uso: scripts/brom-probe.sh [segundos]  -> lanzarlo y despues entrar en BROM (equipo apagado, Back mantenido al enchufar el USB).
set -eu
cd "$(dirname "$0")/.."
WAIT=${1:-600}
OUT=out/brom-probe-$(date +%H%M%S); mkdir -p "$OUT"; R=$(pwd)
echo "salida en $OUT; esperando BROM (1782:4d00) hasta $WAIT s"
tools/spd_dump ${SPD_OPTS:-} --wait "$WAIT" \
  fdl /home/console/rgslide/dump/fdl/fdl1-dl.bin 0x65000800 \
  fdl /home/console/rgslide/dump/fdl/fdl2-dl.bin 0xb4fffe00 \
  exec \
  path "/home/console/rgslide/$OUT" \
  partition_list "/home/console/rgslide/$OUT/parts.xml" \
  size_part userdata \
  size_part misc \
  reset 2>&1 | tee "$OUT/spd_dump.log"
ls -l "$OUT"
