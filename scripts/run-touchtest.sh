#!/bin/sh
# Prueba interactiva del tactil en la Slide: compila (cruzado), copia, para ES, ejecuta touchtest en pantalla y vuelve a arrancar ES.
# Con la pantalla solo puede haber un proceso, asi que ES se para durante la prueba y SIEMPRE se reinicia al terminar (trap).
# Uso: scripts/run-touchtest.sh [segundos=150]      Variables: RGSLIDE_HOST (def. console@10.55.0.2)
# Sobre la Slide: tocar los puntos amarillos, seguir la linea verde de punta a punta, dibujar con 4+ dedos. Start+Select sale; boton A salta de fase.
set -eu
D=$(cd "$(dirname "$0")/.." && pwd)
H=${RGSLIDE_HOST:-console@10.55.0.2}
T=${1:-150}
O="-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh -o ConnectTimeout=8"
SD=$D/out/sdl2-hybris-root/usr/local/sdl2-hybris
GLES=${GLESV2_LIB:-/tmp/opencode/slidelibs/libGLESv2.so.2}
[ -r "$GLES" ] || { mkdir -p "$(dirname "$GLES")"; scp -q $O "$H:/usr/lib/aarch64-linux-gnu/libGLESv2.so.2.1.0" "$GLES"; }
aarch64-linux-gnu-gcc -O2 -Wno-misleading-indentation -Wno-format-truncation -o "$D/out/touchtest" "$D/tools/touchtest/touchtest.c" \
	-I"$SD/include/SDL2" -I"$D/out/glbench-inc" -D_REENTRANT -L"$SD/lib" -lSDL2 "$GLES" -lm \
	-Wl,-rpath,/usr/local/lib/aarch64-linux-gnu -Wl,--allow-shlib-undefined
scp -q $O "$D/out/touchtest" "$H:/tmp/touchtest"
ssh $O "$H" "chmod +x /tmp/touchtest && sudo install -m 755 /tmp/touchtest /usr/local/bin/rgslide-touchtest"
ssh $O "$H" "sudo systemctl stop rgslide-es"
# la ejecucion va en una sesion de login (variables de hybris) y, pase lo que pase, ES vuelve
ssh $O "$H" "trap 'sudo systemctl start rgslide-es' EXIT; sleep 2; timeout $((T + 20)) sudo -u console bash -lc '/usr/local/bin/rgslide-touchtest $T'" || true
ssh $O "$H" "sudo systemctl start rgslide-es; sleep 4; systemctl is-active rgslide-es"
