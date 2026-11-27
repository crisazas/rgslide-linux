#!/bin/sh
# Compila third_party/libhybris en cruzado para la Slide (arm64 glibc 2.41): autoreconf en el contenedor
# rgslide-build:trixie (tiene libtoolize) y la compilacion con el clang del NDK contra out/sysroot-trixie.
# Resultado en out/hybris-root (prefix /usr). Uso: scripts/build-libhybris.sh
set -eu
cd "$(dirname "$0")/.."
R=$PWD
S=$R/out/sysroot-trixie
CL=${NDK:-$HOME/Android/Sdk/ndk/29.0.14206865}/toolchains/llvm/prebuilt/linux-x86_64/bin
H=$R/third_party/libhybris/hybris
AH=$R/third_party/android-headers-13
B=$R/out/hybris-build
[ -f "$S/usr/include/aarch64-linux-gnu/sys/cdefs.h" ] || { echo "falta el sysroot $S"; exit 1; }
if [ ! -x "$H/configure" ]; then
  docker run --rm --platform linux/arm64 -u "$(id -u):$(id -g)" -v "$R/third_party/libhybris:/src" -w /src/hybris \
    rgslide-build:trixie sh -c 'autoreconf -fi >/dev/null 2>&1 || ./autogen.sh'
fi
if [ -f "$R/patches/libhybris/files/drm_window.cpp" ]; then
  cp -p "$R/patches/libhybris/files/drm_window.cpp" "$H/egl/platforms/drm/drm_window.cpp"
fi
mkdir -p "$B"
cd "$B"
export CC="$CL/clang --target=aarch64-linux-gnu --sysroot=$S -fuse-ld=lld"
export CXX="$CL/clang++ --target=aarch64-linux-gnu --sysroot=$S -fuse-ld=lld"
export AR="$CL/llvm-ar" RANLIB="$CL/llvm-ranlib" STRIP="$CL/llvm-strip" NM="$CL/llvm-nm"
export CFLAGS="-O2 -Wno-error -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-pointer-types"
export CXXFLAGS="-O2 -Wno-error"
export LDFLAGS="-Wl,--allow-shlib-undefined"
export PKG_CONFIG_LIBDIR="$S/usr/lib/aarch64-linux-gnu/pkgconfig:$S/usr/share/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$S"
[ -f Makefile ] || "$H/configure" --host=aarch64-linux-gnu --prefix=/usr --enable-arch=arm64 --enable-mali-quirks \
  --with-android-headers="$AH" --with-default-egl-platform=null \
  --with-default-hybris-ld-library-path=/vendor/lib64:/system/lib64:/odm/lib64
make -j"$(nproc)"
rm -rf "$R/out/hybris-root"
make install DESTDIR="$R/out/hybris-root"
echo "libhybris instalado en out/hybris-root"
