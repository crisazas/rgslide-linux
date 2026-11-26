#!/bin/sh
# Convierte un snapshot .tar.zst / .tar.gz de la Slide en una imagen ext4 flasheable (out/rootfs-snapshot.ext4).
# La imagen se expande sola al tamaño completo de userdata en el primer arranque gracias a rgslide-resize.service.
# Uso: scripts/snapshot-to-ext4.sh out/snapshots/rgslide-....tar.zst [salida.ext4] [SIZE=3500M]
set -eu
cd "$(dirname "$0")/.."
SNAP=${1:?uso: scripts/snapshot-to-ext4.sh SNAPSHOT.tar.zst [salida.ext4]}
IMG=${2:-out/rootfs-snapshot.ext4}
SIZE=${SIZE:-3500M}
[ -f "$SNAP" ] || { echo "no existe $SNAP"; exit 1; }

T=$(mktemp -d out/snap-stage.XXXXXX)
trap 'sudo rm -rf "$T"' EXIT

case "$SNAP" in
    *.tar.zst) zstd -dc "$SNAP" | sudo tar --numeric-owner --xattrs --acls -C "$T" -xf - ;;
    *.tar.gz|*.tgz) gzip -dc "$SNAP" | sudo tar --numeric-owner --xattrs --acls -C "$T" -xf - ;;
    *) sudo tar --numeric-owner --xattrs --acls -C "$T" -xf "$SNAP" ;;
esac
sudo mkdir -p "$T/proc" "$T/sys" "$T/dev" "$T/run" "$T/tmp" "$T/mnt" "$T/media" "$T/home/console/ROMs"
sudo chmod 1777 "$T/tmp"
sudo chown 1000:1000 "$T/home/console/ROMs"

rm -f "$IMG"
truncate -s "$SIZE" "$IMG"
sudo mkfs.ext4 -q -F -L rgslide-root -d "$T" "$IMG"
sudo chown "$(id -u):$(id -g)" "$IMG"
ls -lh "$IMG"
echo "Imagen lista: $IMG (para sparse: scripts/mk-sparse.sh $IMG)"
