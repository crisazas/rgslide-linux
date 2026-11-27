#!/bin/bash
# Lanzador de PortMaster para la Anbernic RG Slide (Debian arm64 + Mali/libhybris).
set -u

[ -r /opt/hybris-glvnd/etc/rgslide-hybris.sh ] && . /opt/hybris-glvnd/etc/rgslide-hybris.sh
export SDL_VIDEODRIVER=hybris
export PYSDL2_DLL_PATH=/usr/local/lib/aarch64-linux-gnu
export LD_LIBRARY_PATH="/usr/local/lib/aarch64-linux-gnu:/opt/hybris/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="/usr/local/lib/rgslide-pybin:$PATH"
unset HYBRIS_DRM_REFRESH

PORTS_DIR="/home/console/ROMs/ports"
PM_DIR="$PORTS_DIR/PortMaster"
PM_ZIP_URL="https://github.com/PortsMaster/PortMaster-GUI/releases/latest/download/PortMaster.zip"

# Si PortMaster aun no esta instalado en una imagen recien flasheada, descargarlo automaticamente.
if [ ! -x "$PM_DIR/pugwash" ]; then
    mkdir -p "$PORTS_DIR"
    tmpzip=$(mktemp /tmp/PortMaster.XXXXXX.zip)
    if curl -fsSL "$PM_ZIP_URL" -o "$tmpzip"; then
        unzip -q -o "$tmpzip" -d "$PORTS_DIR"
        chmod +x "$PM_DIR/pugwash" "$PM_DIR/PortMaster.sh" 2>/dev/null || true
    fi
    rm -f "$tmpzip"
fi

if [ ! -x "$PM_DIR/pugwash" ]; then
    echo "PortMaster no esta instalado y no se pudo descargar (verifica la conexion Wi-Fi)." >&2
    exit 1
fi

cd "$PM_DIR" || exit 1
rm -f "$PM_DIR/.pugwash-reboot"

while true; do
    /usr/local/bin/rgslide-python3 ./pugwash "$@"
    rc=$?
    if [ ! -f "$PM_DIR/.pugwash-reboot" ]; then
        exit "$rc"
    fi
    rm -f "$PM_DIR/.pugwash-reboot"
done
