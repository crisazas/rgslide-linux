#!/bin/sh
# Builds out/rootfs.ext4: Debian 13 (trixie) arm64 MINIMO para la RG Slide, con /init, sshd, getty en ttyGS0 y red usb0 10.55.0.2/24.
# Mismo mecanismo que linux-x400/x400-debian/build-rootfs.sh (debootstrap --variant=minbase + qemu-aarch64-static + mkfs.ext4 -d).
# Needs sudo, debootstrap, qemu-user-static, e2fsprogs. Usage: scripts/build-rootfs.sh [--no-image]
set -eu
cd "$(dirname "$0")/.."
OUT=$PWD/out; R=$OUT/rootfs; IMG=$OUT/rootfs.ext4; SIZE=${SIZE:-1500M}
MIRROR=${MIRROR:-http://deb.debian.org/debian}
PKGS="systemd systemd-sysv init systemd-resolved openssh-server iwd wpasupplicant bluez usbutils pciutils evtest libinput-tools iproute2 iputils-ping ca-certificates locales sudo vim-tiny htop strace picocom mesa-utils kmscube weston fonts-noto-core fonts-dejavu-core libpython3.13 libsdl2-mixer-2.0-0 libsdl2-image-2.0-0 libsdl2-ttf-2.0-0 libopenal1 unzip curl"

umnt() { for m in dev sys proc; do mountpoint -q "$R/$m" && { sudo mount --make-rslave "$R/$m"; sudo umount -R "$R/$m"; }; done; return 0; }
trap umnt EXIT

mkdir -p "$OUT"
if [ ! -x "$R/usr/bin/dpkg" ]; then
  sudo rm -rf "$R"; sudo debootstrap --arch=arm64 --variant=minbase --include=systemd-sysv,init trixie "$R" "$MIRROR"
fi
sudo cp /usr/bin/qemu-aarch64-static "$R/usr/bin/"; sudo cp /etc/resolv.conf "$R/etc/resolv.conf"
for m in proc sys dev; do mountpoint -q "$R/$m" || sudo mount --rbind /$m "$R/$m"; done
# nada debe arrancar demonios dentro del chroot (comparte /proc, /sys y red del host)
printf '#!/bin/sh\nexit 101\n' | sudo tee "$R/usr/sbin/policy-rc.d" >/dev/null; sudo chmod 755 "$R/usr/sbin/policy-rc.d"
sudo chroot "$R" /usr/bin/env DEBIAN_FRONTEND=noninteractive LC_ALL=C sh -c "apt-get update -qq && apt-get install -y --no-install-recommends $PKGS && apt-get clean && rm -rf /var/lib/apt/lists/*"
sudo rm -f "$R/usr/sbin/policy-rc.d"

# locales
sudo sed -i 's/^# *en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' "$R/etc/locale.gen"
sudo chroot "$R" /usr/bin/env LC_ALL=C sh -c "locale-gen && update-locale LANG=en_US.UTF-8"

# usuario console con sudo sin contrasena y authorized_keys opcional
sudo chroot "$R" /usr/bin/env LC_ALL=C sh -c "useradd -m -s /bin/bash -G sudo,audio,video,input,render,plugdev console && passwd -d console"
printf 'console ALL=(ALL) NOPASSWD:ALL\n' | sudo tee "$R/etc/sudoers.d/console" >/dev/null; sudo chmod 440 "$R/etc/sudoers.d/console"
if [ -f rootfs/authorized_keys ]; then
  sudo install -d -o console -g console -m 700 "$R/home/console/.ssh"
  sudo install -o console -g console -m 600 rootfs/authorized_keys "$R/home/console/.ssh/authorized_keys"
else
  echo "WARNING: rootfs/authorized_keys no existe, /home/console/.ssh/authorized_keys no se instala"
fi

# red USB: el gadget lo crea el initramfs, aqui solo la direccion del lado Linux
sudo install -d -m 755 "$R/etc/systemd/network"
printf '[Match]\nName=usb0\n\n[Network]\nAddress=10.55.0.2/24\n' | sudo tee "$R/etc/systemd/network/10-usb0.network" >/dev/null
sudo chroot "$R" systemctl enable systemd-networkd >/dev/null 2>&1

# servicios: sshd habilitado y getty en la consola USB (ttyGS0)
sudo chroot "$R" systemctl enable ssh >/dev/null 2>&1
sudo chroot "$R" systemctl enable getty@ttyGS0 >/dev/null 2>&1
# root y ssh con clave: sin clave no hay login por contrasena
sudo sed -i 's/^#*PermitRootLogin.*/PermitRootLogin prohibit-password/' "$R/etc/ssh/sshd_config"
# host keys generadas ahora, no en el primer arranque (sin entropia alli)
sudo chroot "$R" /usr/bin/env LC_ALL=C sh -c "ssh-keygen -A >/dev/null"
# /init: lo que busca el kernel con rdinit=/init cuando el rootfs ya esta montado
printf '#!/bin/sh\nexec /sbin/init "$@"\n' | sudo tee "$R/init" >/dev/null; sudo chmod 755 "$R/init"
# nombre de host
printf 'rgslide\n' | sudo tee "$R/etc/hostname" >/dev/null
printf '127.0.0.1 localhost\n127.0.1.1 rgslide\n10.55.0.1 host\n' | sudo tee "$R/etc/hosts" >/dev/null

# nada de listas ni cache dentro de la imagen
sudo chroot "$R" apt-get clean; sudo rm -rf "$R/var/lib/apt/lists/"* "$R/var/cache/apt/"*.bin
umnt
sudo rm -f "$R/usr/bin/qemu-aarch64-static" "$R/etc/resolv.conf"
echo "installed: $PKGS"
[ "${1:-}" = --no-image ] && exit 0

sudo rm -f "$IMG"; sudo truncate -s "$SIZE" "$IMG"
sudo mkfs.ext4 -q -F -L linuxroot -d "$R" "$IMG"
sudo chown "$(id -u):$(id -g)" "$IMG"
ls -ls "$IMG"
