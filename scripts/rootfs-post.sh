#!/bin/sh
# Ajustes del rootfs para la Slide sobre un arbol ya debootstrapeado (out/rootfs). Idempotente. Necesita sudo, qemu-aarch64-static y red.
#  - e2fsprogs (resize2fs): la imagen mide 1.5 GB y userdata ~114 GB; el init crece el ext4 en caliente.
#  - red: la IP va en sipa_usb0 (el IPA entrega ahi los datos del RNDIS); usb0 solo debe estar `up`.
#  - authorized_keys: rootfs/authorized_keys, o la clave publica del PC (~/.ssh/id_ed25519.pub).
#  - ttyGS0: getty con autologin del usuario console (consola de rescate; el kernel no tiene VT).
# Uso: scripts/rootfs-post.sh [arbol]   (por defecto out/rootfs)
set -eu
cd "$(dirname "$0")/.."
R=${1:-$PWD/out/rootfs}
[ -x "$R/usr/bin/dpkg" ] || { echo "no hay rootfs en $R (scripts/build-rootfs.sh --no-image)"; exit 1; }

umnt() { for m in dev sys proc; do mountpoint -q "$R/$m" && { sudo mount --make-rslave "$R/$m"; sudo umount -R "$R/$m"; }; done; return 0; }
trap umnt EXIT
sudo cp /usr/bin/qemu-aarch64-static "$R/usr/bin/"; sudo cp /etc/resolv.conf "$R/etc/resolv.conf"
for m in proc sys dev; do mountpoint -q "$R/$m" || sudo mount --rbind /$m "$R/$m"; done
printf '#!/bin/sh\nexit 101\n' | sudo tee "$R/usr/sbin/policy-rc.d" >/dev/null; sudo chmod 755 "$R/usr/sbin/policy-rc.d"
sudo chroot "$R" /usr/bin/env DEBIAN_FRONTEND=noninteractive LC_ALL=C sh -c "apt-get update -qq && apt-get install -y --no-install-recommends e2fsprogs busybox-static && apt-get clean && rm -rf /var/lib/apt/lists/*"
sudo rm -f "$R/usr/sbin/policy-rc.d"

# red USB
sudo install -d -m 755 "$R/etc/systemd/network"
sudo rm -f "$R/etc/systemd/network/10-usb0.network"
printf '[Match]\nName=sipa_usb0\n\n[Network]\nAddress=10.55.0.2/24\n' | sudo tee "$R/etc/systemd/network/10-sipa-usb0.network" >/dev/null
printf '[Match]\nName=usb0\n\n[Link]\nRequiredForOnline=no\n\n[Network]\nLinkLocalAddressing=no\nDHCP=no\n' | sudo tee "$R/etc/systemd/network/20-usb0.network" >/dev/null

# ssh: clave del PC
KEYSRC=rootfs/authorized_keys
[ -f "$KEYSRC" ] || KEYSRC=$HOME/.ssh/id_ed25519.pub
if [ -f "$KEYSRC" ]; then
  sudo install -d -o 1000 -g 1000 -m 700 "$R/home/console/.ssh"
  sudo install -o 1000 -g 1000 -m 600 "$KEYSRC" "$R/home/console/.ssh/authorized_keys"
  sudo install -d -m 700 "$R/root/.ssh"; sudo install -m 600 "$KEYSRC" "$R/root/.ssh/authorized_keys"
  echo "authorized_keys: $KEYSRC"
else
  echo "AVISO: sin clave publica; solo consola serie"
fi

# consola serie con autologin
sudo install -d -m 755 "$R/etc/systemd/system/getty@ttyGS0.service.d"
printf '[Service]\nExecStart=\nExecStart=-/sbin/agetty --autologin console --noclear %%I 115200 linux\n' | sudo tee "$R/etc/systemd/system/getty@ttyGS0.service.d/autologin.conf" >/dev/null

# crecimiento del sistema de ficheros (una vez)
sudo tee "$R/usr/local/sbin/rgslide-resize" >/dev/null <<'EOS'
#!/bin/sh
# Crece el ext4 de / hasta ocupar la particion. Marca /var/lib/rgslide-resized para no repetir.
[ -e /var/lib/rgslide-resized ] && exit 0
DEV=$(findmnt -n -o SOURCE / 2>/dev/null)
[ -b "$DEV" ] || DEV=$(awk '$2=="/" && $3=="ext4"{print $1}' /proc/mounts | tail -1)
[ -b "$DEV" ] || { echo "rgslide-resize: sin dispositivo raiz"; exit 1; }
resize2fs "$DEV" && touch /var/lib/rgslide-resized
EOS
sudo chmod 755 "$R/usr/local/sbin/rgslide-resize"
sudo tee "$R/etc/systemd/system/rgslide-resize.service" >/dev/null <<'EOS'
[Unit]
Description=Crece el sistema de ficheros raiz (una vez)
ConditionPathExists=!/var/lib/rgslide-resized
After=local-fs.target
[Service]
Type=oneshot
ExecStart=/usr/local/sbin/rgslide-resize
[Install]
WantedBy=multi-user.target
EOS
sudo chroot "$R" systemctl enable rgslide-resize.service >/dev/null 2>&1 || true
sudo chroot "$R" systemctl enable systemd-networkd.service ssh.service >/dev/null 2>&1 || true

umnt
sudo rm -f "$R/usr/bin/qemu-aarch64-static" "$R/etc/resolv.conf"
for b in resize2fs e2fsck busybox; do printf '%-10s ' $b; ls "$R/sbin/$b" "$R/usr/sbin/$b" "$R/bin/$b" 2>/dev/null | head -1; echo; done | grep -v '^$'
echo ok
