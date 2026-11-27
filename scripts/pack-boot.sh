#!/bin/sh
# Empaqueta boot image para el dispositivo seleccionado (por defecto rgslide).
# Soporta DEVICE=rgslide o DEVICE=rg406h (o cualquier dispositivo bajo devices/$DEVICE).
# Uso: [DEVICE=rg406h] scripts/pack-boot.sh [salida.img]
set -eu
cd "$(dirname "$0")/.."

DEVICE="${DEVICE:-rgslide}"
DEV_DIR="devices/$DEVICE"
if [ -f "$DEV_DIR/config.env" ]; then
    . "$DEV_DIR/config.env"
fi

# Seleccionar rutas segun dispositivo
if [ "$DEVICE" = "rgslide" ]; then
    KERNEL="dump/boot/boot_a.unp/kernel"
    DUMP_BOOT="dump/boot"
    IMG_DEFAULT="out/boot-rescue.img"
else
    KERNEL="dump/$DEVICE/boot/boot_a.unp/kernel"
    DUMP_BOOT="dump/$DEVICE/boot"
    IMG_DEFAULT="out/boot-$DEVICE.img"
fi

RAMDISK="${RAMDISK:-out/initramfs.cpio.gz}"
IMG="${1:-$IMG_DEFAULT}"
CMDLINE_DEFAULT="enforcing=0 audit=0 androidboot.selinux=permissive"
CMDLINE="${CMDLINE:-$CMDLINE_DEFAULT}"

[ -f "$KERNEL" ] || { echo "Error: no se encontro el kernel en $KERNEL" >&2; exit 1; }
[ -f "$RAMDISK" ] || { echo "Error: no se encontro el ramdisk en $RAMDISK" >&2; exit 1; }

echo "Empaquetando $IMG para dispositivo: $DEVICE ..."
python3 tools/mkbootimg.py \
    --header_version 4 \
    --kernel "$KERNEL" \
    --ramdisk "$RAMDISK" \
    --cmdline "$CMDLINE" \
    --os_version 13.0.0 \
    --os_patch_level 2023-08 \
    -o "$IMG"

ls -lh "$IMG"
