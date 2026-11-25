#!/bin/sh
# Compila tools/ahbdrm/ahbdrm.c (bionic, NDK) -> out/ahbdrm. En la Slide: ahbdrm [-g] [segundos]
set -eu
cd "$(dirname "$0")/.."
NDK=${NDK:-$HOME/Android/Sdk/ndk/29.0.14206865}
$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang -O2 -Wall -Wno-unused-parameter -fPIE -pie -o out/ahbdrm tools/ahbdrm/ahbdrm.c -ldl -lnativewindow
echo "out/ahbdrm listo"
