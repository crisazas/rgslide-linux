#!/bin/sh
# Compila out/rgslide-python3: lanzador minimo de Python 3.13 con padding TLS en el ejecutable
# principal para evitar que Mali (TLS_SLOT_OPENGL en tp+0x18) pise _Py_tss_tstate.
set -eu
cd "$(dirname "$0")/.."
mkdir -p out
aarch64-linux-gnu-gcc -O2 -Wall -Wextra tools/pytls/rgslide-python3.c -o out/rgslide-python3 -ldl
ls -lh out/rgslide-python3
