#!/bin/sh
# Converts out/rootfs.ext4 to out/rootfs.sparse.img for `fastboot flash userdata` (flash en crudo, no `fastboot update`).
# Usage: scripts/mk-sparse.sh [imagen.ext4]
set -eu
cd "$(dirname "$0")/.."
SRC=${1:-out/rootfs.ext4}
[ -f "$SRC" ] || { echo "falta $SRC (scripts/build-rootfs.sh)" >&2; exit 1; }
DST=${SRC%.ext4}.sparse.img
img2simg "$SRC" "$DST"
ls -ls "$SRC" "$DST"
