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
TOOLS_DIR="/opt/tools"
PM_DIR="$TOOLS_DIR/PortMaster"
PM_ZIP_URL="https://github.com/PortsMaster/PortMaster-GUI/releases/latest/download/PortMaster.zip"

# Migrar instalacion previa si existia dentro de ROMs/ports/PortMaster (evita subcarpeta duplicada en ES-DE).
if [ ! -x "$PM_DIR/pugwash" ] && [ -d "$PORTS_DIR/PortMaster" ]; then
    sudo -n mkdir -p "$TOOLS_DIR" 2>/dev/null || mkdir -p "$TOOLS_DIR"
    sudo -n mv "$PORTS_DIR/PortMaster" "$PM_DIR" 2>/dev/null || mv "$PORTS_DIR/PortMaster" "$PM_DIR"
    sudo -n chown -R console:console "$PM_DIR" 2>/dev/null || true
fi

# Si PortMaster aun no esta instalado en una imagen recien flasheada, descargarlo automaticamente.
if [ ! -x "$PM_DIR/pugwash" ]; then
    sudo -n mkdir -p "$TOOLS_DIR" 2>/dev/null || mkdir -p "$TOOLS_DIR"
    sudo -n chown console:console "$TOOLS_DIR" 2>/dev/null || true
    tmpzip=$(mktemp /tmp/PortMaster.XXXXXX.zip)
    if curl -fsSL "$PM_ZIP_URL" -o "$tmpzip"; then
        unzip -q -o "$tmpzip" -d "$TOOLS_DIR"
        chmod +x "$PM_DIR/pugwash" "$PM_DIR/PortMaster.sh" 2>/dev/null || true
    fi
    rm -f "$tmpzip"
fi

if [ ! -x "$PM_DIR/pugwash" ]; then
    echo "PortMaster no esta instalado y no se pudo descargar (verifica la conexion Wi-Fi)." >&2
    exit 1
fi

# Limpiar posibles archivos creados como root por scripts antiguos de PortMaster.
if [ -e "$PM_DIR/utils/pmsplash/stopsplash" ] && [ ! -w "$PM_DIR/utils/pmsplash/stopsplash" ]; then
    sudo -n rm -f "$PM_DIR/utils/pmsplash/stopsplash" 2>/dev/null || true
fi

before_list=$(ls -1 "$PORTS_DIR"/*.sh 2>/dev/null | sort || true)

cd "$PM_DIR" || exit 1
rm -f "$PM_DIR/.pugwash-reboot"

rc=0
while true; do
    /usr/local/bin/rgslide-python3 ./pugwash "$@"
    rc=$?
    if [ ! -f "$PM_DIR/.pugwash-reboot" ]; then
        break
    fi
    rm -f "$PM_DIR/.pugwash-reboot"
done

after_list=$(ls -1 "$PORTS_DIR"/*.sh 2>/dev/null | sort || true)
if [ "$before_list" != "$after_list" ]; then
    mkdir -p /run/rgslide 2>/dev/null || true
    touch /run/rgslide/esde-rescan 2>/dev/null || true
    (sleep 0.8; pkill -TERM -x es-de 2>/dev/null || true) >/dev/null 2>&1 &
fi

exit "$rc"
