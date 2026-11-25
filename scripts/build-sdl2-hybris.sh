#!/bin/sh
# SDL2 2.32.4 (fuentes de Debian) con el driver de video "hybris" (patches/sdl2/0001-driver-hybris.patch) en cruzado para la Slide.
# Salida: out/sdl2-hybris-root (prefix /usr/local/sdl2-hybris). Sin X11/Wayland/KMSDRM/Pulse/Pipewire: video hybris (EGL por glvnd), audio ALSA, entrada evdev.
# Necesita: /tmp/opencode/sdl/SDL2-2.32.4 (tar de Debian) y /tmp/opencode/sdl/sx (libasound2-dev, libudev-dev arm64 extraidos). Uso: scripts/build-sdl2-hybris.sh
set -eu
cd "$(dirname "$0")/.."
R=$PWD
S=$R/out/sysroot-trixie
SRC=${SDL_SRC:-/tmp/opencode/sdl/SDL2-2.32.4}
SX=${SDL_SX:-/tmp/opencode/sdl/sx}
GX=${GLVND_X:-/tmp/opencode/glvnd-debs/x}
CL=${NDK:-$HOME/Android/Sdk/ndk/29.0.14206865}/toolchains/llvm/prebuilt/linux-x86_64/bin
B=$R/out/sdl2-hybris-build
[ -d "$SRC" ] || { echo "falta $SRC"; exit 1; }
mkdir -p "$B"
cat > "$B/toolchain.cmake" <<EOT
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSROOT $S)
set(CMAKE_C_COMPILER $CL/clang)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_AR $CL/llvm-ar)
set(CMAKE_RANLIB $CL/llvm-ranlib)
set(CMAKE_LINKER_TYPE LLD)
set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS "-fuse-ld=lld -Wl,--allow-shlib-undefined")
set(CMAKE_FIND_ROOT_PATH $S $SX $GX)
set(CMAKE_C_FLAGS "-I$SX/usr/include -I$GX/usr/include -I$R/out/hybris-root/usr/include")
EOT
cd "$B"
cmake -G Ninja "$SRC" -DCMAKE_TOOLCHAIN_FILE="$B/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local/sdl2-hybris \
  -DSDL_STATIC=ON -DSDL_SHARED=ON -DSDL_TEST=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF \
  -DSDL_JACK=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=ON -DSDL_VULKAN=OFF -DSDL_RPI=OFF -DSDL_VIVANTE=OFF -DSDL_DIRECTFB=OFF -DSDL_SNDIO=OFF \
  -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON -DSDL_LIBUDEV=ON -DSDL_HIDAPI=OFF -DSDL_OFFSCREEN=ON -DSDL_HYBRIS=ON -DSDL_DBUS=OFF -DSDL_IBUS=OFF \
  -DSDL_FCITX=OFF -DSDL_LIBSAMPLERATE=OFF -DSDL_OSS=OFF -DSDL_ARTS=OFF -DSDL_ESD=OFF -DSDL_DISKAUDIO=OFF -DSDL_DUMMYAUDIO=ON -DSDL_DUMMYVIDEO=ON
ninja
rm -rf "$R/out/sdl2-hybris-root"
DESTDIR="$R/out/sdl2-hybris-root" ninja install
echo "SDL2 (hybris) instalado en out/sdl2-hybris-root/usr/local/sdl2-hybris"
