#!/bin/sh
# Install the Stage 1/Stage 2 systemd isolation into the Debian rootfs on userdata (/dev/sda73), from the
# Slot B rescue shell (10.55.0.2:23). Stage 1 unit files stay untouched: only drop-ins and Stage 2 units are added.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
DEV_IP=${RGSLIDE_DEV_IP:-10.55.0.2}
STAGE1_UNITS="esde audiod cm4 ch imu sleep dp mdev input hotkeyd es brightness fan fgu gpu sensorhub audio bt wifi wpa android vpu bootmenu"
b64() { base64 -w0 "$1"; }
{
  echo 'mkdir -p /mnt/u; umount /mnt/u 2>/dev/null; mount -t ext4 /dev/sda73 /mnt/u || exit 1'
  echo "echo $(b64 "$HERE/rgslide-mainline-modules.service") | base64 -d > /mnt/u/etc/systemd/system/rgslide-mainline-modules.service"
  echo 'ln -sf /etc/systemd/system/rgslide-mainline-modules.service /mnt/u/etc/systemd/system/multi-user.target.wants/rgslide-mainline-modules.service'
  echo "echo $(b64 "$HERE/rgslide-mainline-audio") | base64 -d > /mnt/u/usr/local/sbin/rgslide-mainline-audio && chmod 755 /mnt/u/usr/local/sbin/rgslide-mainline-audio"
  echo "echo $(b64 "$HERE/rgslide-mainline-audio.service") | base64 -d > /mnt/u/etc/systemd/system/rgslide-mainline-audio.service"
  echo 'ln -sf /etc/systemd/system/rgslide-mainline-audio.service /mnt/u/etc/systemd/system/multi-user.target.wants/rgslide-mainline-audio.service'
  echo "echo $(b64 "$HERE/rgslide-mainline-imu") | base64 -d > /mnt/u/usr/local/sbin/rgslide-mainline-imu && chmod 755 /mnt/u/usr/local/sbin/rgslide-mainline-imu"
  echo "echo $(b64 "$HERE/rgslide-mainline-imu.service") | base64 -d > /mnt/u/etc/systemd/system/rgslide-mainline-imu.service"
  echo 'ln -sf /etc/systemd/system/rgslide-mainline-imu.service /mnt/u/etc/systemd/system/multi-user.target.wants/rgslide-mainline-imu.service'
  echo "echo $(b64 "$HERE/20-usb0.network") | base64 -d > /mnt/u/etc/systemd/network/20-usb0.network"
  for s in $STAGE1_UNITS; do
    echo "mkdir -p /mnt/u/etc/systemd/system/rgslide-$s.service.d; echo $(b64 "$HERE/stage1-only.conf") | base64 -d > /mnt/u/etc/systemd/system/rgslide-$s.service.d/stage1-only.conf"
  done
  echo 'sync; umount /mnt/u; echo INSTALL_DONE'
  sleep 4
} | nc -w 15 "$DEV_IP" 23 | tr -d '\r' | grep -E 'INSTALL_DONE|rror|failed' | tail -n 3
