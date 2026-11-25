#!/bin/sh
# Construye out/initramfs.cpio.gz (reproducible: fechas fijas, sin dueño).
# Uso: scripts/build-initramfs.sh [busybox-aarch64]
# Sin argumento compila busybox estatico si falta el toolchain; ver README.
set -eu

SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT="$SRC/out"
STAGE="${STAGE_DIR:-$OUT/initramfs-root}"
IMG="${OUT_IMG:-$OUT/initramfs.cpio.gz}"
INIT_SRC="${INIT_SRC:-$SRC/initramfs/init}"
BB_SRC="$SRC/third_party/busybox-aarch64"

[ -f "$INIT_SRC" ] || { echo "falta $INIT_SRC" >&2; exit 1; }
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
cp "$INIT_SRC" "$STAGE/init"
chmod 755 "$STAGE/init"

# Applets: enlaces a busybox en bin/ (reboot/poweroff/halt tambien en sbin/).
for a in $(cd "$STAGE" && ./bin/busybox --list); do
	ln -sf busybox "$STAGE/bin/$a"
done
for a in reboot poweroff halt; do
	ln -sf ../bin/busybox "$STAGE/sbin/$a"
done
ln -sf ../bin/busybox "$STAGE/sbin/init"

# Modulos: copia de terceros si existen (orden en modules.load).
if [ -d "$SRC/initramfs/modules" ]; then
	cp -R "$SRC/initramfs/modules/." "$STAGE/lib/modules/"
fi

# misc original (fase 0), solo 4096 B: lo usa restore_misc con rgslide.restore=1.
if [ -f "$SRC/dump/min/misc.img" ]; then
	head -c 4096 "$SRC/dump/min/misc.img" > "$STAGE/lib/misc-orig.bin"
fi

# Reproducible: fechas fijas y sin uid/gid del host.
find "$STAGE" -exec touch -h -d '@0' {} +
(cd "$STAGE" && find . -print | LC_ALL=C sort | cpio -o -H newc --owner 0:0 --reproducible -R 0:0 2>/dev/null) > "$IMG.cpio"
# El LK concatena la ramdisk de vendor (LZ4 legacy) con esta: el kernel 5.4 solo decodifica la cadena si todo es LZ4.
lz4 -l -9 -f -q "$IMG.cpio" "$IMG" && rm -f "$IMG.cpio"

echo "$IMG"
