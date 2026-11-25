#!/bin/sh
# Lanza brom-probe.sh en bucle (siempre hay un spd_dump a la escucha cuando la Slide entra en BROM). Solo lectura.
# Uso: scripts/brom-loop.sh [segundos_totales]
END=$(( $(date +%s) + ${1:-480} ))
cd "$(dirname "$0")/.."
n=0
while [ "$(date +%s)" -lt "$END" ]; do
  n=$((n+1)); echo "== intento $n $(date +%T)"
  timeout 200 ./scripts/brom-probe.sh 180 2>&1 | tail -14
  grep -q -E 'Partition List|partition_list|part_size|userdata' out/brom-probe-*/spd_dump.log 2>/dev/null && { echo "RESPUESTA DEL FDL: salgo"; break; }
  sleep 2
done
echo "fin $(date +%T)"
