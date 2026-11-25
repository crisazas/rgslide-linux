#!/bin/sh
# EmulationStation (RetroPie, GLES 2.0) en cruzado para la Slide, contra el SDL2 con driver hybris (out/sdl2-hybris-root).
# Dependencias (libvlc, FreeImage, curl, FreeType, RapidJSON) de los .deb de Debian trixie extraidos en $ES_DEPS (/tmp/opencode/esdeps/root).
# Salida: out/es-root/opt/emulationstation. Uso: scripts/build-es.sh
set -eu
cd "$(dirname "$0")/.."
R=$PWD
S=$R/out/sysroot-trixie
D=${ES_DEPS:-/tmp/opencode/esdeps/root}
SD=$R/out/sdl2-hybris-root/usr/local/sdl2-hybris
GX=${GLVND_X:-/tmp/opencode/glvnd-debs/x}
SX=${SDL_SX:-/tmp/opencode/sdl/sx}
CL=${NDK:-$HOME/Android/Sdk/ndk/29.0.14206865}/toolchains/llvm/prebuilt/linux-x86_64/bin
SRC=$R/third_party/EmulationStation
B=$R/out/es-build
[ -d "$SRC" ] || { echo "falta $SRC"; exit 1; }
[ -d "$D/usr/include/vlc" ] || { echo "faltan las dependencias en $D"; exit 1; }
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
set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld -Wl,--allow-shlib-undefined -Wl,-rpath-link,$D/usr/lib/aarch64-linux-gnu -Wl,-rpath-link,/tmp/opencode/gl_stub -L$D/usr/lib/aarch64-linux-gnu")
set(CMAKE_FIND_ROOT_PATH $D $SD $GX $SX $S)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_C_FLAGS "-I$D/usr/include -I$D/usr/include/aarch64-linux-gnu -I$SD/include/SDL2 -I$GX/usr/include -I$R/out/hybris-root/usr/include -I$SX/usr/include")
set(CMAKE_CXX_FLAGS "-I$D/usr/include -I$D/usr/include/aarch64-linux-gnu -I$SD/include/SDL2 -I$GX/usr/include -I$R/out/hybris-root/usr/include -I$SX/usr/include -Wno-error")
EOT
cd "$B"
export PKG_CONFIG_LIBDIR="$D/usr/lib/aarch64-linux-gnu/pkgconfig:$SD/lib/pkgconfig:$S/usr/lib/aarch64-linux-gnu/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$D"
cmake -G Ninja "$SRC" -DFreeImage_INCLUDE_DIR="$D/usr/include" -DFreeImage_LIBRARY_REL="$D/usr/lib/aarch64-linux-gnu/libfreeimage.so" -DFreeImage_LIBRARY_DBG="$D/usr/lib/aarch64-linux-gnu/libfreeimage.so" -DCMAKE_TOOLCHAIN_FILE="$B/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release -DGLES=On \
  -DSDL2_INCLUDE_DIR="$SD/include/SDL2" -DSDL2_LIBRARY="$SD/lib/libSDL2.so" -DSDL2_LIBRARIES="$SD/lib/libSDL2.so" \
  -DOPENGLES2_INCLUDE_DIR="$GX/usr/include" -DOPENGLES2_gl_LIBRARY=/tmp/opencode/gl_stub/libGLESv2.so \
  -DALSA_INCLUDE_DIR="$SX/usr/include" -DALSA_LIBRARY="$D/usr/lib/aarch64-linux-gnu/libasound.so.2" \
  -DRAPIDJSON_INCLUDE_DIRS="$D/usr/include"
ninja
mkdir -p "$R/out/es-root/opt/emulationstation"
cp "$SRC/emulationstation" "$R/out/es-root/opt/emulationstation/emulationstation"
echo "ES compilado: out/es-root/opt/emulationstation/emulationstation"
