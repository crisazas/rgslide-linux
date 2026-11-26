#!/bin/sh
# Instala en la Slide (sin reiniciar ni arrancar nada): xml del CH (sensorhub) para el modem_control del arranque, init del sensorhub, lector, y el serializado de apsys_dvfs.
# Uso: scripts/install-sensors.sh [host]
set -eu
cd "$(dirname "$0")/.."
H=${1:-console@10.55.0.2}
SSH="ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh -o ConnectTimeout=8"
put() { cat "$1" | $SSH "$H" "sudo mkdir -p $(dirname "$2") && sudo tee $2 >/dev/null && sudo chmod $3 $2"; }
put dump/android/modem_ch_info.xml /opt/android/vendor/etc/modem_ch_info.xml 644
put rootfs/overlay/usr/local/sbin/rgslide-sensorhub-init /usr/local/sbin/rgslide-sensorhub-init 755
put scripts/agent-sensors-read.py /usr/local/sbin/rgslide-sensors-read 755
put rootfs/overlay/etc/systemd/system/rgslide-sensorhub.service /etc/systemd/system/rgslide-sensorhub.service 644
put rootfs/overlay/etc/systemd/system/systemd-udev-trigger.service.d/rgslide-serial.conf /etc/systemd/system/systemd-udev-trigger.service.d/rgslide-serial.conf 644
put rootfs/overlay/etc/modules-load.d/00-rgslide-dvfs.conf /etc/modules-load.d/00-rgslide-dvfs.conf 644
put rootfs/overlay/usr/local/sbin/rgslide-cm4-prep /usr/local/sbin/rgslide-cm4-prep 755
$SSH "$H" 'sudo ln -sf /opt/android/vendor/etc/modem_ch_info.xml /vendor/etc/modem_ch_info.xml; sudo systemctl daemon-reload; sudo systemctl enable rgslide-sensorhub; systemd-analyze verify /etc/systemd/system/rgslide-sensorhub.service 2>&1 | head -2'
