#!/bin/sh
# Saca un snapshot limpio del rootfs vivo de la Slide por SSH -> out/snapshots/rgslide-YYYYMMDD-HHMM.tar.zst (o .tar.gz).
# Excluye /proc, /sys, /dev, /run, /tmp, /mnt, /media, /lost+found, caches y ~/ROMs.
# Uso: scripts/snapshot-slide.sh [nombre-opcional]
set -eu
cd "$(dirname "$0")/.."
H=${SLIDE_HOST:-console@10.55.0.2}
SSH="ssh -o BatchMode=yes -o ConnectTimeout=8"
NAME=${1:-rgslide-$(date +%Y%m%d-%H%M)}
mkdir -p out/snapshots

if command -v zstd >/dev/null 2>&1; then
    OUT="out/snapshots/${NAME}.tar.zst"
    COMP="zstd -T0 -3 -o $OUT"
else
    OUT="out/snapshots/${NAME}.tar.gz"
    COMP="gzip -3 > $OUT"
fi

echo "Capturando snapshot de $H -> $OUT ..."
$SSH "$H" 'sudo tar --one-file-system --numeric-owner --xattrs --acls \
    --exclude=/proc/* --exclude=/sys/* --exclude=/dev/* --exclude=/run/* --exclude=/tmp/* \
    --exclude=/mnt/* --exclude=/media/* --exclude=/lost+found \
    --exclude=/var/cache/apt/archives/*.deb --exclude=/var/tmp/* \
    --exclude=/home/console/ROMs/* --exclude=/home/console/.cache/* \
    -C / -cf - .' | sh -c "$COMP"

ls -lh "$OUT"
echo "Snapshot guardado en $OUT"
echo "Para generar una imagen ext4 flasheable: scripts/snapshot-to-ext4.sh $OUT"
