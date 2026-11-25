#!/bin/sh
# Compila tools/fakewin/fakewin.c como binario bionic (NDK) -> out/fakewin. Uso en la Slide: fakewin -d 20 (anima 20 s en el panel)
set -eu
cd "$(dirname "$0")/.."
NDK=${NDK:-$HOME/Android/Sdk/ndk/29.0.14206865}
$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang -O2 -Wall -Wno-unused-parameter -fPIE -pie -o out/fakewin tools/fakewin/fakewin.c -ldl -lnativewindow -lm
echo "out/fakewin listo"
