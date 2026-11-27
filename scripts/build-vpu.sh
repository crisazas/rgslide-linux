#!/bin/sh
# Compila tools/vpu/rgslide-vpu.c en cruzado para la RG Slide (aarch64 glibc + libhybris).
# Salida: out/rgslide-vpu
set -eu
cd "$(dirname "$0")/.."
mkdir -p out

HYBRIS_LIB=""
for d in out/hybris-glvnd-root/usr/lib out/hybris-root/usr/lib; do
    if [ -f "$d/libhybris-common.so" ]; then
        HYBRIS_LIB="$d"
        break
    fi
done
[ -n "$HYBRIS_LIB" ] || { echo "falta libhybris-common.so (ejecuta scripts/build-libhybris-glvnd.sh)"; exit 1; }

aarch64-linux-gnu-gcc -O2 -Wall -Wextra \
    tools/vpu/rgslide-vpu.c \
    -L"$HYBRIS_LIB" \
    -Wl,-rpath,/opt/hybris-glvnd/lib:/opt/hybris/lib \
    -lhybris-common -ldl \
    -o out/rgslide-vpu

aarch64-linux-gnu-strip out/rgslide-vpu
ls -lh out/rgslide-vpu
