#!/bin/bash
# Mide los registros PMU/AON antes y despues de arrancar el AGDSP. NO abre ningun PCM.
# Requiere RGSLIDE_OK=1 (hace dd + stop + start de sprd_audcp_boot). Ejecutar EN la Slide con alguien delante.
set -u
[ "${RGSLIDE_OK:-0}" = 1 ] || { echo "poner RGSLIDE_OK=1 (hace start del AGDSP)"; exit 1; }
B=/sys/devices/platform/audiocp_boot
P=/sys/kernel/debug/regmap/dummy-syscon@64910000/registers
A=/sys/kernel/debug/regmap/dummy-syscon@64900000/registers
OUT=/tmp/agdsp-probe.out
snap() {
  echo "== $1 $(cut -d' ' -f1 /proc/uptime)"
  grep -E '^(03d0|03d4|0544|0818|0850|0860|0b88|0b98|0ba8):' "$P" | tr '\n' ' '; echo
  grep -E '^(0078|0140|0144|014c):' "$A" | tr '\n' ' '; echo
  od -An -tx1 "$B/status" | tr -s ' '
}
{
  snap antes
  dd if=/dev/block/by-name/l_agdsp_a of="$B/agdsp" bs=4096 count=1536 2>/dev/null
  echo 1 > "$B/stop"
  snap tras-stop
  echo 1 > "$B/start"; echo "start rc=$?"
  snap "+0s"; sleep 0.2; snap "+0.2s"; sleep 1.8; snap "+2s"; sleep 6; snap "+8s"
  dmesg | grep -E 'sprd-aud-agdsp|Audio:SMSG|Audio:SIPC' | tail -15
} > "$OUT" 2>&1
cat "$OUT"
