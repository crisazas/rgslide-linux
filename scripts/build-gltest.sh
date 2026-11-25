#!/bin/sh
# Compila tools/gltest/gltest.c como binario bionic (PIE) con el NDK; sale en out/gltest
set -eu
cd "$(dirname "$0")/.."
NDK=${NDK:-$HOME/Android/Sdk/ndk/29.0.14206865}
CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang
mkdir -p out
$CC -O2 -Wall -fPIE -pie -o out/gltest tools/gltest/gltest.c -ldl -llog
echo "out/gltest listo"
