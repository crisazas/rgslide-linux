#!/bin/bash
# Sustituto nativo de PortMaster Westonpack (westonwrap.sh) para la Anbernic RG Slide.
# En sprd_drm no existe driver GBM/Mesa para Wayland, por lo que Westonpack original falla.
# Este wrapper arranca el Weston nativo de Debian con --backend=drm --use-pixman + kiosk-shell
# + XWayland y ejecuta los juegos X11/OpenGL acelerados por GPU Mali-G57 via Mesa Zink + PanVK
# (kopper sobre X11 MIT-SHM con LIBGL_KOPPER_DRI2=1 y PANVK_KBASE_DRI3=0, OpenGL 4.2 / GLES 3.1).
# Si se define RGSLIDE_WESTON_SW=1 (o falta PanVK), cae a Mesa llvmpipe (4 hilos).
set -u

UID_TAG="$(id -u)"
RT_DIR="/tmp/rgslide-weston-rt-${UID_TAG}"
PID_FILE="/tmp/rgslide-weston-${UID_TAG}.pid"
INI_FILE="/tmp/rgslide-weston-${UID_TAG}.ini"
LOG_FILE="/tmp/rgslide-weston-${UID_TAG}.log"

stop_weston() {
    if [ -f "$PID_FILE" ]; then
        wpid=$(cat "$PID_FILE" 2>/dev/null || true)
        if [ -n "$wpid" ] && kill -0 "$wpid" 2>/dev/null; then
            kill -TERM "$wpid" 2>/dev/null || true
            for _ in 1 2 3 4 5 6 7 8 9 10; do
                kill -0 "$wpid" 2>/dev/null || break
                sleep 0.1
            done
            kill -9 "$wpid" 2>/dev/null || true
        fi
        rm -f "$PID_FILE"
    fi
    rm -rf "$RT_DIR" "$INI_FILE"
}

if [ "${1:-}" = "cleanup" ]; then
    stop_weston
    exit 0
fi

# Descartar los 4 parametros posicionales de Westonpack: <backend> <renderer> <shell> <libs>
if [ $# -ge 4 ]; then
    shift 4
fi

# Extraer variables KEY=VAL opcionales antes del binario del juego
GAME_ENV=()
while [ $# -gt 0 ]; do
    case "$1" in
        SDL_VIDEODRIVER=)
            # Ignorar SDL_VIDEODRIVER vacio para usar X11 (Zink + PanVK sobre XWayland MIT-SHM)
            shift ;;
        [A-Za-z_]*=*)
            GAME_ENV+=("$1")
            shift ;;
        *)
            break ;;
    esac
done

if [ $# -lt 1 ]; then
    echo "rgslide-westonwrap: falta el comando del juego" >&2
    exit 2
fi

# Atajo nativo GPU Mali-G57 para juegos del motor AGS (Adventure Game Studio):
# evita Weston/XWayland por completo usando /usr/local/bin/rgslide-ags (compilado con OpenGL ES 2.0)
# directo sobre SDL2-hybris + libhybris Mali-G57.
case "$(basename "$1")" in
    ags|ags.*)
        if [ -x /usr/local/bin/rgslide-ags ]; then
            shift
            [ -r /opt/hybris-glvnd/etc/rgslide-hybris.sh ] && . /opt/hybris-glvnd/etc/rgslide-hybris.sh
            export SDL_VIDEODRIVER=hybris
            export LD_LIBRARY_PATH="/usr/local/lib/aarch64-linux-gnu:/opt/hybris/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
            export SDL_GAMECONTROLLERCONFIG="19009b4d4b4800000111000000010000,retrogame_joypad,a:b0,b:b1,x:b3,y:b4,back:b10,guide:b12,start:b11,leftstick:b13,rightstick:b14,leftshoulder:b6,rightshoulder:b7,dpup:b15,dpdown:b16,dpleft:b17,dpright:b18,leftx:a0,lefty:a1,rightx:a3,righty:a4,lefttrigger:a2,righttrigger:a5,crc:4d9b,platform:Linux,"
            unset LIBGL_ALWAYS_SOFTWARE MESA_LOADER_DRIVER_OVERRIDE GALLIUM_DRIVER HYBRIS_DRM_REFRESH
            exec env "${GAME_ENV[@]}" /usr/local/bin/rgslide-ags "$@"
        fi ;;
esac

trap stop_weston EXIT INT TERM
stop_weston

mkdir -p "$RT_DIR"
chmod 700 "$RT_DIR"

cat > "$INI_FILE" << 'EOF'
[core]
shell=kiosk-shell.so
xwayland=true
idle-time=0

[shell]
locking=false

[output]
name=DSI-1
scale=2
EOF

# Lanzar Weston DRM + Pixman (sin libhybris ni libtlspad)
env -u LD_PRELOAD \
    -u HYBRIS_LINKER_DIR -u HYBRIS_EGLPLATFORM_DIR -u HYBRIS_EGLPLATFORM \
    -u LIBEGL -u LIBGLESV2 -u LIBGLESV1_CM -u HYBRIS_LD_LIBRARY_PATH \
    -u SDL_VIDEODRIVER \
    XDG_RUNTIME_DIR="$RT_DIR" \
    WESTON_DISABLE_ATOMIC=1 \
    __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
    LIBGL_ALWAYS_SOFTWARE=1 \
    /usr/bin/weston --config="$INI_FILE" --backend=drm --drm-device=card0 --use-pixman --socket=wayland-1 \
    >"$LOG_FILE" 2>&1 &
WPID=$!
echo "$WPID" > "$PID_FILE"

# Esperar socket Wayland
ready=0
for _ in $(seq 1 50); do
    if [ -S "$RT_DIR/wayland-1" ]; then
        ready=1
        break
    fi
    kill -0 "$WPID" 2>/dev/null || break
    sleep 0.1
done

if [ "$ready" -ne 1 ]; then
    echo "rgslide-westonwrap: weston no pudo iniciar:" >&2
    cat "$LOG_FILE" >&2 || true
    exit 1
fi

# Detectar DISPLAY de XWayland si esta disponible
XDISP=""
for _ in $(seq 1 15); do
    for sock in /tmp/.X11-unix/X*; do
        if [ -S "$sock" ]; then
            XDISP=":${sock##*/X}"
            break 2
        fi
    done
    sleep 0.1
done

# Filtrar SDL2-hybris y libhybris del WRAPPED_LIBRARY_PATH para que el juego use SDL2 Wayland/X11 + Mesa
CLEAN_WLP=""
IFS=':' read -r -a parts <<< "${WRAPPED_LIBRARY_PATH:-}"
for p in "${parts[@]}"; do
    case "$p" in
        ""|/usr/local/lib/aarch64-linux-gnu|/opt/hybris/lib|/opt/es-de/lib) ;;
        *) CLEAN_WLP="${CLEAN_WLP:+$CLEAN_WLP:}$p" ;;
    esac
done

export XDG_RUNTIME_DIR="$RT_DIR"
export WAYLAND_DISPLAY="wayland-1"
[ -n "$XDISP" ] && export DISPLAY="$XDISP"
export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
export SDL_GAMECONTROLLERCONFIG="19009b4d4b4800000111000000010000,retrogame_joypad,a:b0,b:b1,x:b3,y:b4,back:b10,guide:b12,start:b11,leftstick:b13,rightstick:b14,leftshoulder:b6,rightshoulder:b7,dpup:b15,dpdown:b16,dpleft:b17,dpright:b18,leftx:a0,lefty:a1,rightx:a3,righty:a4,lefttrigger:a2,righttrigger:a5,crc:4d9b,platform:Linux,"
export LD_LIBRARY_PATH="/usr/lib/aarch64-linux-gnu${CLEAN_WLP:+:$CLEAN_WLP}"
unset LD_PRELOAD HYBRIS_LINKER_DIR HYBRIS_EGLPLATFORM_DIR HYBRIS_EGLPLATFORM LIBEGL LIBGLESV2 LIBGLESV1_CM HYBRIS_LD_LIBRARY_PATH

# Aceleracion GPU Mali-G57 para juegos X11/OpenGL dentro de Westonpack via Zink + PanVK:
# - PANVK_KBASE_DRI3=0 activa la presentacion X11 MIT-SHM/PutImage en PanVK (sin requerir DRI3)
# - LIBGL_KOPPER_DRI2=1 permite a Mesa GLX Zink usar kopper sobre XWayland sin DRI3
# - SDL_VIDEODRIVER=x11 enruta clientes SDL2 por XWayland para aprovechar Zink+PanVK (OpenGL 4.2)
# Fallback a Mesa llvmpipe (4 hilos) si RGSLIDE_WESTON_SW=1 o si no existe el ICD PanVK.
if [ "${RGSLIDE_WESTON_SW:-0}" != "1" ] && [ -r /etc/vulkan/icd.d/panfrost_icd.aarch64.json ]; then
    unset LIBGL_ALWAYS_SOFTWARE
    export VK_ICD_FILENAMES=/etc/vulkan/icd.d/panfrost_icd.aarch64.json
    export PANVK_KBASE_DRI3=0
    export LIBGL_KOPPER_DRI2=1
    export MESA_LOADER_DRIVER_OVERRIDE=zink
    export GALLIUM_DRIVER=zink
    export SDL_VIDEODRIVER=x11
else
    export LIBGL_ALWAYS_SOFTWARE=1
    export LP_NUM_THREADS=4
    unset MESA_LOADER_DRIVER_OVERRIDE GALLIUM_DRIVER LIBGL_KOPPER_DRI2 PANVK_KBASE_DRI3
    if [ -x "$1" ] && ldd "$1" 2>/dev/null | grep -qE 'libwayland-client|libSDL2-2\.0'; then
        export SDL_VIDEODRIVER=wayland
    else
        export SDL_VIDEODRIVER=x11
    fi
fi

env "${GAME_ENV[@]}" "$@"
rc=$?
exit "$rc"
