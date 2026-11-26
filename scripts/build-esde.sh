#!/bin/sh
# ES-DE Frontend (GLES 3.0 + DEINIT_ON_LAUNCH) en cruzado para la Slide (aarch64-linux-gnu glibc),
# contra SDL2 con driver hybris (out/sdl2-hybris-root) y libGLESv2.so.2 de GLVND arm64 (out/esde-deps).
# Salida: out/esde-root/opt/es-de. Uso: scripts/build-esde.sh
set -eu
cd "$(dirname "$0")/.."
R=$PWD
S=$R/out/sysroot-trixie
D=${ESDE_DEPS:-$R/out/esde-deps}
SD=$R/out/sdl2-hybris-root/usr/local/sdl2-hybris
CL=${NDK:-$HOME/Android/Sdk/ndk/29.0.14206865}/toolchains/llvm/prebuilt/linux-x86_64/bin
SRC=$R/third_party/ES-DE
B=$R/out/esde-build
OUT=$R/out/esde-root

[ -d "$SRC" ] || { echo "falta $SRC"; exit 1; }
[ -f "$D/usr/include/GLES3/gl3.h" ] || { echo "faltan cabeceras GLES3 en $D"; exit 1; }
[ -f "$SD/lib/libSDL2.so" ] || { echo "falta $SD/lib/libSDL2.so (ejecuta scripts/build-sdl2-hybris.sh)"; exit 1; }

mkdir -p "$B"
cat > "$B/toolchain.cmake" <<EOT
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSROOT $S)
set(CMAKE_C_COMPILER $CL/clang)
set(CMAKE_CXX_COMPILER $CL/clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_AR $CL/llvm-ar)
set(CMAKE_RANLIB $CL/llvm-ranlib)
set(CMAKE_LINKER_TYPE LLD)
set(gp_tool "none")
set(CMAKE_FIND_ROOT_PATH $D $SD $S)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_FLAGS_INIT "-I$D/usr/include -I$D/usr/include/aarch64-linux-gnu -I$SD/include -I$SD/include/SDL2")
set(CMAKE_CXX_FLAGS_INIT "-I$D/usr/include -I$D/usr/include/aarch64-linux-gnu -I$SD/include -I$SD/include/SDL2 -Wno-error")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -Wl,--allow-shlib-undefined -L$S/usr/lib/aarch64-linux-gnu -L$S/lib/aarch64-linux-gnu -L$D/usr/lib/aarch64-linux-gnu -L$SD/lib -Wl,-rpath-link,$S/usr/lib/aarch64-linux-gnu -Wl,-rpath-link,$D/usr/lib/aarch64-linux-gnu -Wl,-rpath-link,$SD/lib -Wl,-rpath,/opt/es-de/lib:/usr/local/lib/aarch64-linux-gnu")
EOT

cd "$B"
export PKG_CONFIG_LIBDIR="$D/usr/lib/aarch64-linux-gnu/pkgconfig:$SD/lib/pkgconfig:$S/usr/lib/aarch64-linux-gnu/pkgconfig:$D/usr/share/pkgconfig:$S/usr/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$D"
export PKG_CONFIG_PATH=

cmake -G Ninja "$SRC" \
  -DCMAKE_TOOLCHAIN_FILE="$B/toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/opt/es-de \
  -DGL=OFF \
  -DGLES=ON \
  -DDEINIT_ON_LAUNCH=ON \
  -DAPPLICATION_UPDATER=OFF \
  -DSDL2_INCLUDE_DIR="$SD/include/SDL2" \
  -DSDL2_LIBRARY="$SD/lib/libSDL2.so" \
  -DOPENGLES2_INCLUDE_DIR="$D/usr/include" \
  -DOPENGLES2_gl_LIBRARY="$D/usr/lib/aarch64-linux-gnu/libGLESv2.so" \
  -DFFMPEG_LIBAVCODEC_INCLUDE_DIRS="$D/usr/include/aarch64-linux-gnu" \
  -DFFMPEG_LIBAVFILTER_INCLUDE_DIRS="$D/usr/include/aarch64-linux-gnu" \
  -DFFMPEG_LIBAVFORMAT_INCLUDE_DIRS="$D/usr/include/aarch64-linux-gnu" \
  -DFFMPEG_LIBAVUTIL_INCLUDE_DIRS="$D/usr/include/aarch64-linux-gnu" \
  -DFreeImage_INCLUDE_DIR="$D/usr/include" \
  -DFreeImage_LIBRARY_REL="$D/usr/lib/aarch64-linux-gnu/libfreeimage.so" \
  -DFreeImage_LIBRARY_DBG="$D/usr/lib/aarch64-linux-gnu/libfreeimage.so" \
  -DCURL_INCLUDE_DIR="$D/usr/include/aarch64-linux-gnu" \
  -DCURL_LIBRARY="$D/usr/lib/aarch64-linux-gnu/libcurl.so" \
  -DFREETYPE_INCLUDE_DIRS="$D/usr/include/freetype2" \
  -DFREETYPE_LIBRARY="$D/usr/lib/aarch64-linux-gnu/libfreetype.so" \
  -DHarfBuzz_INCLUDE_DIRS="$D/usr/include/harfbuzz" \
  -DHarfBuzz_LIBRARIES="$D/usr/lib/aarch64-linux-gnu/libharfbuzz.so;$D/usr/lib/aarch64-linux-gnu/libharfbuzz-icu.so" \
  -DICU_INCLUDE_DIRS="$D/usr/include" \
  -DICU_LIBRARIES="$D/usr/lib/aarch64-linux-gnu/libicuuc.so;$D/usr/lib/aarch64-linux-gnu/libicui18n.so;$D/usr/lib/aarch64-linux-gnu/libicudata.so" \
  -DGIT2_INCLUDE_PATH="$D/usr/include" \
  -DGIT2_LIBRARY="$D/usr/lib/aarch64-linux-gnu/libgit2.so" \
  -DALSA_INCLUDE_DIR="$D/usr/include" \
  -DALSA_LIBRARY="$D/usr/lib/aarch64-linux-gnu/libasound.so" \
  -DBLUEZ_INCLUDE_DIR="$D/usr/include" \
  -DBLUEZ_LIBRARY="$D/usr/lib/aarch64-linux-gnu/libbluetooth.so" \
  -DPOPPLER_INCLUDE_DIR="$D/usr/include/poppler" \
  -DPOPPLER_LIBRARY="$D/usr/lib/aarch64-linux-gnu/libpoppler.so" \
  -DPOPPLER_CPP_INCLUDE_DIR="$D/usr/include/poppler/cpp" \
  -DPOPPLER_CPP_LIBRARY="$D/usr/lib/aarch64-linux-gnu/libpoppler-cpp.so"

ninja
rm -rf "$OUT"
DESTDIR="$OUT" ninja install
echo "ES-DE compilado e instalado en $OUT/opt/es-de"
