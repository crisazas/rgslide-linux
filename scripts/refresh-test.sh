#!/bin/bash
# Prueba el modo de refresco de la plataforma drm de hybris en la Slide, con pantalla real.
# Uso (en la Slide): refresh-test.sh HZ [SEGUNDOS] [DIR_PLATAFORMA]   (HZ=60 hace de linea base)
# Para rgslide-es SOLO durante la prueba, corre hybristest con timeout y SIEMPRE arranca rgslide-es al final
# (al arrancar, ES hace su propio SETCRTC con el modo de 60 Hz: vuelve al modo original).
HZ=${1:-120}; SECS=${2:-5}; PD=${3:-/opt/hybris/lib/libhybris}
LOG=/tmp/refresh-test-$HZ.log
trap 'sudo systemctl start rgslide-es; sleep 1; echo "rgslide-es: $(systemctl is-active rgslide-es)" >>"$LOG"' EXIT
sudo systemctl stop rgslide-es
sleep 1
(
  for _ in $(seq 1 $((2*(SECS+6)))); do
    echo "$(date +%s.%N | cut -c1-14) actual_fps=$(cat /sys/class/display/dispc0/actual_fps) panel_fps=$(cat /sys/class/display/panel0/panel_fps) frames=$(cat /sys/class/display/dispc0/frame_count) bat_uA=$(cat /sys/class/power_supply/battery/current_now) load=$(cut -d' ' -f1 /proc/loadavg)"
    sleep 0.5
  done
) >"$LOG.sys" 2>&1 &
SP=$!
HT_EXIT=1 timeout 12 /usr/local/bin/rgslide-hybris-env env HYBRIS_EGLPLATFORM_DIR=$PD HYBRIS_DRM_REFRESH=$HZ /opt/hybris/bin/hybristest "$SECS" >"$LOG" 2>&1
echo "salida hybristest: $?" >>"$LOG"
kill $SP 2>/dev/null; wait 2>/dev/null
