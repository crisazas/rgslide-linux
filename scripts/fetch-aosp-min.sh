#!/bin/sh
# Descarga el subconjunto minimo de AOSP android-13.0.0_r1 (TP1A.220624.014 = nuestro vendor) para compilar bionic con Soong.
# Clones superficiales (--depth 1) en third_party/aosp13/<ruta>. Reanudable: salta lo ya clonado. Registro en third_party/aosp13/fetch.log
set -u
cd "$(dirname "$0")/.."
T=android-13.0.0_r1
ROOT=third_party/aosp13
B=https://android.googlesource.com/platform
mkdir -p "$ROOT"
LIST="build/make:build build/soong:build/soong build/blueprint:build/blueprint build/bazel:build/bazel
external/golang-protobuf:external/golang-protobuf external/starlark-go:external/starlark-go
external/scudo:external/scudo external/jemalloc_new:external/jemalloc_new external/gwp_asan:external/gwp_asan
external/arm-optimized-routines:external/arm-optimized-routines external/libcxx:external/libcxx external/libcxxabi:external/libcxxabi
external/compiler-rt:external/compiler-rt external/zlib:external/zlib external/tinyxml2:external/tinyxml2
external/kernel-headers:external/kernel-headers system/libbase:system/libbase system/logging:system/logging
system/unwinding:system/unwinding system/libziparchive:system/libziparchive system/timezone:system/timezone
system/core:system/core prebuilts/build-tools:prebuilts/build-tools prebuilts/go/linux-x86:prebuilts/go/linux-x86
prebuilts/clang/host/linux-x86:prebuilts/clang/host/linux-x86 prebuilts/clang-tools:prebuilts/clang-tools
bionic:bionic"
for e in $LIST; do
  path=${e%%:*}; repo=${e##*:}
  [ "$path" = "bionic" ] && continue
  dest="$ROOT/$path"
  if [ -d "$dest/.git" ]; then echo "$(date +%T) ya: $path"; continue; fi
  mkdir -p "$(dirname "$dest")"
  n=0
  while [ $n -lt 4 ]; do
    n=$((n+1))
    rm -rf "$dest"
    if git clone -q --depth 1 --branch "$T" "$B/$repo" "$dest" 2>>"$ROOT/fetch.err"; then echo "$(date +%T) OK $path ($(du -sh "$dest" | cut -f1))"; break; fi
    echo "$(date +%T) reintento $n $path"; sleep 5
  done
  [ -d "$dest/.git" ] || echo "$(date +%T) FALLO $path"
done
echo "$(date +%T) FIN"
