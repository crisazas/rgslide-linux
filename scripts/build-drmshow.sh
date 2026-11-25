#!/bin/sh
# Compila tools/drmshow/drmshow.c como binario aarch64 estatico (glibc); sale en out/drmshow
set -eu
cd "$(dirname "$0")/.."
mkdir -p out
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -Werror -static -o out/drmshow tools/drmshow/drmshow.c
info=$(readelf -h out/drmshow)
echo "$info" | grep -q 'AArch64' || { echo "no es aarch64" >&2; exit 1; }
readelf -l out/drmshow | grep -q INTERP && { echo "no es estatico" >&2; exit 1; }
echo "out/drmshow listo (aarch64, estatico)"
