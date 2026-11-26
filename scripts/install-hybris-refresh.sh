#!/bin/sh
# Instala el eglplatform_drm.so con el parche 0004 (HYBRIS_DRM_REFRESH) en la Slide, guardando antes el actual como .pre-refresh. Sin la variable, igual que antes (60 Hz).
# Deshacer: scripts/install-hybris-refresh.sh undo. No reinicia ES: el .so nuevo lo carga el proximo ES/RetroArch que se abra.
set -eu
cd "$(dirname "$0")/.."
H=${H:-console@10.55.0.2}
SSH="ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh -o ConnectTimeout=8"
SO=out/hybris-build/egl/platforms/drm/.libs/eglplatform_drm.so
D1=/opt/hybris/lib/libhybris; D2=/opt/hybris-glvnd/aux/libhybris
case "${1:-install}" in
install)
  [ -f "$SO" ] || { echo "falta $SO (compilar con scripts/build-libhybris*.sh)"; exit 1; }
  W=$(md5sum "$SO" | cut -c1-32)
  cat "$SO" | $SSH "$H" "cat > /tmp/eglplatform_drm.new && [ \$(md5sum /tmp/eglplatform_drm.new | cut -c1-32) = $W ] || { echo copia_corrupta; exit 1; }
    for d in $D1 $D2; do [ -e \$d/eglplatform_drm.so.pre-refresh ] || sudo cp -p \$d/eglplatform_drm.so \$d/eglplatform_drm.so.pre-refresh; sudo cp /tmp/eglplatform_drm.new \$d/eglplatform_drm.so.tmp && sudo mv -f \$d/eglplatform_drm.so.tmp \$d/eglplatform_drm.so; done
    rm -f /tmp/eglplatform_drm.new; md5sum $D1/eglplatform_drm.so $D2/eglplatform_drm.so | cut -c1-12" ;;
undo)
  $SSH "$H" "for d in $D1 $D2; do [ -e \$d/eglplatform_drm.so.pre-refresh ] && sudo cp -p \$d/eglplatform_drm.so.pre-refresh \$d/eglplatform_drm.so; done; md5sum $D1/eglplatform_drm.so | cut -c1-12" ;;
esac
