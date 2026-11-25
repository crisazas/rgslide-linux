#!/bin/sh
# Compila tools/glbench/glbench.c dos veces: bionic (NDK) para el blob de Arm -> out/glbench-bionic, y glibc para Mesa -> out/glbench-glibc
set -eu
cd "$(dirname "$0")/.."
NDK=${NDK:-$HOME/Android/Sdk/ndk/29.0.14206865}
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64
mkdir -p out
$TC/bin/aarch64-linux-android35-clang -O2 -Wall -fPIE -pie -o out/glbench-bionic tools/glbench/glbench.c -ldl
H=out/glbench-inc
mkdir -p $H
rm -rf $H/EGL $H/GLES3 $H/KHR $H/GLES2
cp -r "$TC/sysroot/usr/include/EGL" "$TC/sysroot/usr/include/GLES3" "$TC/sysroot/usr/include/GLES2" "$TC/sysroot/usr/include/KHR" $H/
aarch64-linux-gnu-gcc -O2 -Wall -I$H -o out/glbench-glibc tools/glbench/glbench.c -ldl
echo "out/glbench-bionic y out/glbench-glibc listos"
