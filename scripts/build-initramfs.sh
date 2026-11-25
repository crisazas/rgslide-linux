#!/bin/sh
# Construye out/initramfs.cpio.gz (reproducible: fechas fijas, sin dueño).
# Uso: scripts/build-initramfs.sh [busybox-aarch64]
# Sin argumento compila busybox estatico si falta el toolchain; ver README.
set -eu

SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT="$SRC/out"
STAGE="$OUT/initramfs-root"
IMG="$OUT/initramfs.cpio.gz"
BB_SRC="$SRC/third_party/busybox-aarch64"

[ -f "$SRC/initramfs/init" ] || { echo "falta initramfs/init" >&2; exit 1; }
if [ "$#" -ge 1 ]; then
	BB=$1
else
	BB="$BB_SRC/busybox"
fi
if [ ! -x "$BB" ]; then
	echo "falta busybox estatico aarch64 ($BB); ver initramfs/README.md" >&2
	exit 1
fi
case "$(file -b "$BB")" in
	*'ELF 64-bit'*'ARM aarch64'*'statically linked'*) ;;
	*) echo "$BB no es un ELF aarch64 estatico" >&2; exit 1 ;;
esac

rm -rf "$STAGE" "$IMG"
mkdir -p "$STAGE/bin" "$STAGE/sbin" "$STAGE/etc" "$STAGE/proc" "$STAGE/sys" \
	"$STAGE/dev" "$STAGE/run" "$STAGE/mnt/root" "$STAGE/root" \
	"$STAGE/tmp" "$STAGE/lib/modules"
chmod 1777 "$STAGE/tmp"
chmod 700 "$STAGE/root"

cp "$BB" "$STAGE/bin/busybox"
chmod 755 "$STAGE/bin/busybox"
cp "$SRC/initramfs/init" "$STAGE/init"
chmod 755 "$STAGE/init"

# Applets: enlaces a busybox en bin/ y sbin/.
for a in $(cd "$STAGE" && ./bin/busybox --list); do
	case "$a" in
		sh|ash|mount|umount|modprobe|dmesg|date|grep|seq|sleep|switch_root|mkdir|ln|ip|getty|setsid|cat|echo|mknod|chmod|cd|exec)
			ln -sf busybox "$STAGE/bin/$a" ;;
		reboot|poweroff|halt)
			ln -sf ../bin/busybox "$STAGE/sbin/$a" ;;
	esac
done
ln -sf ../bin/busybox "$STAGE/sbin/init"

# Modulos: copia de terceros si existen (orden en modules.load).
if [ -d "$SRC/initramfs/modules" ]; then
	cp -R "$SRC/initramfs/modules/." "$STAGE/lib/modules/"
fi

# Reproducible: fechas fijas y sin uid/gid del host.
find "$STAGE" -exec touch -h -d '@0' {} +
(cd "$STAGE" && find . -print | LC_ALL=C sort | cpio -o -H newc --owner 0:0 --reproducible -R 0:0 2>/dev/null) | gzip -9n > "$IMG"

echo "$IMG"
