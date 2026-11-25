#!/bin/sh
# Espera el gadget RNDIS de la Slide (VID:PID 1d6b:0104) y deja la IP 10.55.0.1/24 en la interfaz del PC.
# Uso: scripts/pc-usb-net.sh [segundos]   (necesita sudo -n)
WAIT=${1:-300}; i=0
while [ $i -lt "$WAIT" ]; do
  if lsusb -d 1d6b:0104 >/dev/null 2>&1; then
    echo "gadget 1d6b:0104 visible tras ${i}s"; sleep 2
    IF=$(ls -d /sys/bus/usb/devices/*/net/* 2>/dev/null | while read p; do d=${p%/net/*}; [ "$(cat $d/idProduct 2>/dev/null)" = 0104 ] && basename "$p"; done | head -1)
    [ -n "$IF" ] || IF=$(ip -br link | awk '$1 ~ /^(enx|usb|enp0s.*u)/ {print $1}' | tail -1)
    echo "interfaz del PC: ${IF:-?}"
    [ -n "$IF" ] && sudo -n ip addr add 10.55.0.1/24 dev "$IF" 2>&1; [ -n "$IF" ] && sudo -n ip link set "$IF" up
    ip -br addr show "$IF" 2>/dev/null; ping -c 2 -W 2 10.55.0.2 2>&1 | tail -3
    exit 0
  fi
  i=$((i+1)); sleep 1
done
echo "no aparecio 1d6b:0104 en ${WAIT}s"; exit 1
