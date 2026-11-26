#!/bin/bash
# En la Slide: para ES, mide el intervalo real de flip a 60/120/90 Hz con refresh-flip (cada uno con timeout 10 s y
# restauracion del crtc) y SIEMPRE arranca rgslide-es al terminar. Uso: refresh-flip-test.sh [/ruta/refresh-flip]
BIN=${1:-/tmp/refresh-flip}; OUT=/tmp/refresh-flip-test.log
trap 'sudo systemctl start rgslide-es; sleep 1; echo "rgslide-es: $(systemctl is-active rgslide-es)" >>"$OUT"' EXIT
: >"$OUT"
sudo systemctl stop rgslide-es; sleep 1
for hz in 60 120 90 60; do
  echo "== $hz Hz" >>"$OUT"
  ( sleep 2; echo "sysfs: actual_fps=$(cat /sys/class/display/dispc0/actual_fps) panel_fps=$(cat /sys/class/display/panel0/panel_fps) pixel_clock=$(cat /sys/class/display/panel0/pixel_clock) bat_uA=$(cat /sys/class/power_supply/battery/current_now) ibat_load=$(cut -d' ' -f1 /proc/loadavg)" >>"$OUT" ) &
  timeout 10 "$BIN" $hz 3 >>"$OUT" 2>&1
  echo "salida: $?" >>"$OUT"
  wait
done
