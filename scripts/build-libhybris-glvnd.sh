#!/bin/sh
# Compila libhybris como VENDOR DE GLVND (libEGL_libhybris.so.0 + libGLESv2_libhybris.so.2 + 10_libhybris.json) en cruzado para la Slide.
# Build y destino propios (out/hybris-glvnd-build, out/hybris-glvnd-root): NO toca out/hybris-root (la variante sin glvnd, que funciona).
# Necesita las cabeceras/.pc de glvnd 1.7.0 arm64 extraidas en $GLVND_X (por defecto /tmp/opencode/glvnd-debs/x): libglvnd-dev, libegl-dev, libgles-dev, libglvnd-core-dev.
set -eu
cd "$(dirname "$0")/.."
R=$PWD
S=$R/out/sysroot-trixie
GX=${GLVND_X:-/tmp/opencode/glvnd-debs/x}
CL=${NDK:-$HOME/Android/Sdk/ndk/29.0.14206865}/toolchains/llvm/prebuilt/linux-x86_64/bin
H=$R/third_party/libhybris/hybris
AH=$R/third_party/android-headers-13
B=$R/out/hybris-glvnd-build
[ -f "$GX/usr/include/glvnd/libeglabi.h" ] || { echo "faltan las cabeceras de glvnd en $GX"; exit 1; }
[ -x "$H/configure" ] || { echo "falta $H/configure (ejecuta antes scripts/build-libhybris.sh)"; exit 1; }
if [ -f "$R/patches/libhybris/files/drm_window.cpp" ]; then
  cp -p "$R/patches/libhybris/files/drm_window.cpp" "$H/egl/platforms/drm/drm_window.cpp"
fi
mkdir -p "$B"
cd "$B"
export CC="$CL/clang --target=aarch64-linux-gnu --sysroot=$S -fuse-ld=lld -I$GX/usr/include"
export CXX="$CL/clang++ --target=aarch64-linux-gnu --sysroot=$S -fuse-ld=lld -I$GX/usr/include"
export AR="$CL/llvm-ar" RANLIB="$CL/llvm-ranlib" STRIP="$CL/llvm-strip" NM="$CL/llvm-nm"
export CFLAGS="-O2 -Wno-error -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-pointer-types"
export CXXFLAGS="-O2 -Wno-error"
export LDFLAGS="-Wl,--allow-shlib-undefined"
export PKG_CONFIG_LIBDIR="$GX/usr/lib/aarch64-linux-gnu/pkgconfig:$S/usr/lib/aarch64-linux-gnu/pkgconfig:$S/usr/share/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$S"
export PKG_CONFIG_PATH=
[ -f Makefile ] || "$H/configure" --host=aarch64-linux-gnu --prefix=/usr --enable-arch=arm64 --enable-mali-quirks --enable-glvnd \
  --with-android-headers="$AH" --with-default-egl-platform=drm \
  --with-default-hybris-ld-library-path=/vendor/lib64:/system/lib64:/odm/lib64
make -j"$(nproc)"
rm -rf "$R/out/hybris-glvnd-root"
make install DESTDIR="$R/out/hybris-glvnd-root"
echo "libhybris (vendor glvnd) instalado en out/hybris-glvnd-root"
