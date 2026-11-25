#!/bin/sh
# Escribe por BROM, en este orden (lo irreversible al final): boot_b, misc (B activo y bueno) y userdata (rootfs ext4). Cada escritura se lee de vuelta
# y se compara por md5 en brom-verify. Rutas absolutas (spd_dump cambia de directorio).
# Uso: scripts/brom-flash-rootfs.sh [segundos]   (Slide en BROM: Back mantenido al enchufar el USB)
set -eu
cd "$(dirname "$0")/.."
R=$(pwd); WAIT=${1:-300}
BOOT=$R/out/boot-rootfs.img; MISC=$R/out/misc-rootfs.img; ROOT=$R/out/rootfs.ext4
for f in "$BOOT" "$MISC" "$ROOT" "$R/dump/boot/boot_b.img" "$R/dump/min/misc.img"; do [ -f "$f" ] || { echo "falta $f"; exit 1; }; done
OUT=$R/out/brom-flash-$(date +%H%M%S); mkdir -p "$OUT/rb"
echo "salida en $OUT; esperando BROM hasta $WAIT s"
{ md5sum "$BOOT" "$MISC" "$ROOT"; } > "$OUT/md5-escritos.txt"
tools/spd_dump --wait "$WAIT" \
  fdl "$R/dump/fdl/fdl1-dl.bin" 0x65000800 \
  fdl "$R/dump/fdl/fdl2-dl.bin" 0xb4fffe00 \
  exec \
  write_part boot_b "$BOOT" \
  write_part misc "$MISC" \
  write_part userdata "$ROOT" \
  path "$OUT/rb" r boot_b r misc \
  reset 2>&1 | tee "$OUT/spd_dump.log"
echo "--- verificacion de lo releido (userdata es demasiado grande para releerla entera: se comprueban sus primeros 1.5 GB aparte) ---"
for p in boot_b misc; do
  case $p in boot_b) E=$BOOT;; misc) E=$MISC;; esac
  N=$(stat -c %s "$E")
  got=$(head -c "$N" "$OUT/rb/$p" | md5sum | cut -c1-32); want=$(md5sum "$E" | cut -c1-32)
  [ "$got" = "$want" ] && echo "OK   $p $got" || echo "FALLO $p leido=$got esperado=$want"
done
