#!/usr/bin/env bash
# Pack Milestone M0 mainline initramfs and Android boot v4 image into out/mainline/.
#
# Usage:
#   mainline/scripts/pack-boot.sh [output.img]

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_ROOT="$ROOT_DIR/out/mainline"
KERNEL_LK="$OUT_ROOT/Image.lk"
INIT_SRC="$ROOT_DIR/mainline/initramfs/init"
BUSYBOX="$ROOT_DIR/third_party/busybox-aarch64/busybox"
RAMDISK="$OUT_ROOT/initramfs-m0.cpio.lz4"
IMG="${1:-$OUT_ROOT/boot-mainline-m0.img}"

[ -f "$KERNEL_LK" ] || { echo "Error: $KERNEL_LK not found. Run mainline/scripts/build-kernel.sh first." >&2; exit 1; }
[ -f "$INIT_SRC" ] || { echo "Error: $INIT_SRC not found." >&2; exit 1; }
[ -f "$BUSYBOX" ] || { echo "Error: $BUSYBOX not found." >&2; exit 1; }

STAGE="$OUT_ROOT/initramfs-m0.stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/dev" "$STAGE/proc" "$STAGE/sys" "$STAGE/tmp" "$STAGE/run" "$STAGE/config"

install -m 0755 "$BUSYBOX" "$STAGE/bin/busybox"
ln -sf busybox "$STAGE/bin/sh"
install -m 0755 "$INIT_SRC" "$STAGE/init"

# LK concatenates the vendor_boot ramdisk (LZ4 legacy) with boot's ramdisk,
# so boot's ramdisk must also use LZ4 legacy (-l) to be decoded cleanly.
(
    cd "$STAGE"
    find . -print0 | LC_ALL=C sort -z | cpio --null -o -H newc --owner 0:0 2>/dev/null > "$OUT_ROOT/initramfs-m0.cpio"
)
lz4 -l -9 -f -q "$OUT_ROOT/initramfs-m0.cpio" "$RAMDISK"
rm -rf "$STAGE" "$OUT_ROOT/initramfs-m0.cpio"

# Note: CONFIG_CMDLINE_FORCE=y is set in the mainline kernel, so LK's cmdline is ignored.
python3 "$ROOT_DIR/tools/mkbootimg.py" \
    --header_version 4 \
    --kernel "$KERNEL_LK" \
    --ramdisk "$RAMDISK" \
    --cmdline "enforcing=0 audit=0 androidboot.selinux=permissive" \
    --os_version 13.0.0 \
    --os_patch_level 2023-08 \
    -o "$IMG"

ls -lh "$RAMDISK" "$IMG"
