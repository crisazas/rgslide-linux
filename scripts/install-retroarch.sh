#!/bin/sh
# Instala RetroArch + nucleos en la Slide (/opt/retroarch, lanzador y cfg). Requiere out/retroarch-root (scripts/build-retroarch.sh)
# y el arbol de nucleos en $CORES (por defecto /tmp/opencode/ra/tree, ver docs/retroarch.md). Uso: scripts/install-retroarch.sh
set -eu
cd "$(dirname "$0")/.."
SSH="ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh -o ConnectTimeout=8 console@${SLIDE:-10.55.0.2}"
T=$(mktemp -d)
cp -a "${CORES:-/tmp/opencode/ra/tree}/opt" "$T/"
cp out/retroarch-root/opt/retroarch/bin/retroarch "$T/opt/retroarch/bin/retroarch"
mkdir -p "$T/usr/local/bin" "$T/etc"
cp rootfs/overlay/usr/local/bin/rgslide-retroarch "$T/usr/local/bin/"
cp rootfs/overlay/etc/retroarch.cfg "$T/etc/"
# OJO: sin la entrada '.' (extraerla sobre / le aplicaria el modo 0700 de mktemp y se pierde el SSH); --no-overwrite-dir por si acaso.
chmod 755 "$T"
(cd "$T" && tar -cf - opt usr etc) | $SSH 'sudo tar -C / -xf - --no-same-owner --no-overwrite-dir && sudo chmod 755 /usr/local/bin/rgslide-retroarch && sudo chmod -R a+rwX /opt/retroarch/saves /opt/retroarch/states /opt/retroarch/system /opt/retroarch/config'
rm -rf "$T"
echo instalado
