#!/bin/sh
# Instala el audio completo en la Slide: demonio (rgslide-audiod), unidades, /etc/asound.conf y ajustes de RetroArch.
# Uso: scripts/install-audiod.sh [--enable]     (sin --enable instala pero no habilita el arranque)
set -eu
D=$(dirname "$0")/..
H=${RGSLIDE_HOST:-console@192.168.68.116}
O="-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh"
aarch64-linux-gnu-gcc -O2 -Wall -pthread -o /tmp/audiod.out "$D/tools/pcmprobe/audiod.c"
scp -q $O /tmp/audiod.out "$D/rootfs/overlay/etc/asound.conf" "$D/rootfs/overlay/etc/systemd/system/rgslide-audiod.service" "$D/rootfs/overlay/etc/systemd/system/rgslide-audio.service" "$H:/tmp/"
ssh $O "$H" 'sudo install -m 755 /tmp/audiod.out /usr/local/bin/rgslide-audiod && sudo install -m 644 /tmp/asound.conf /etc/asound.conf &&
 sudo install -m 644 /tmp/rgslide-audiod.service /tmp/rgslide-audio.service /etc/systemd/system/ && sudo systemctl daemon-reload'
if [ "${1:-}" = "--enable" ]; then ssh $O "$H" 'sudo systemctl enable rgslide-audio rgslide-audiod && sudo systemctl restart rgslide-audio rgslide-audiod; systemctl is-active rgslide-audiod'; fi
