#!/bin/sh
# Copia rootfs/overlay/ al rootfs de la Slide por ssh (sin reflashear) y habilita rgslide-cm4, rgslide-dev y rgslide-mdev. Hacerlo cuando la Slide este arriba.
set -eu
cd "$(dirname "$0")/.."
H=${1:-console@10.55.0.2}
SSH="ssh -n -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh -o ConnectTimeout=8"
tar -C rootfs/overlay --owner=0 --group=0 -cf - . | ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh "$H" 'sudo tar --warning=no-timestamp -C / -xf - && sudo chmod 755 /usr/local/sbin/rgslide-cm4-prep /usr/local/sbin/rgslide-dev-fix'
$SSH "$H" 'sudo systemctl daemon-reload && sudo systemctl enable rgslide-cm4.service rgslide-dev.service rgslide-mdev.service 2>&1 | tail -3; ls -l /etc/systemd/system/rgslide-cm4.service'
