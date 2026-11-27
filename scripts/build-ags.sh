#!/bin/sh
# Compila out/rgslide-ags (Adventure Game Studio v3.6.3.17) en cruzado para arm64 (Debian 13 Trixie),
# enlazado dinamicamente a libSDL2-2.0.so.0 para correr acelerado en GPU Mali-G57 (via SDL2-hybris o Wayland+Zink).
# Uso: scripts/build-ags.sh
set -eu
cd "$(dirname "$0")/.."
R=$PWD
SRC=$R/out/ags-src
BUILD=$R/out/ags-build
TAG=${AGS_TAG:-v3.6.3.17}

if [ ! -d "$SRC/.git" ]; then
  mkdir -p "$SRC"
  git clone --depth 1 --branch "$TAG" https://github.com/adventuregamestudio/ags.git "$SRC"
fi

mkdir -p "$BUILD"
docker run --rm --platform linux/amd64 \
  -v "$SRC:/src:ro" \
  -v "$BUILD:/build" \
  debian:trixie bash -c '
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
dpkg --add-architecture arm64
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
  ca-certificates cmake ninja-build pkg-config g++-aarch64-linux-gnu gcc-aarch64-linux-gnu \
  libsdl2-dev:arm64 libogg-dev:arm64 libvorbis-dev:arm64 libtheora-dev:arm64 \
  libfreetype-dev:arm64 zlib1g-dev:arm64 libgl-dev:arm64 libgles-dev:arm64

cat > /tmp/tc.cmake <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu /usr/lib/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
EOF

export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig
cmake -G Ninja -S /src -B /build \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/tc.cmake \
  -DSDL2_DIR=/usr/lib/aarch64-linux-gnu/cmake/SDL2 \
  -DALLEGRO_USE_CONSTRUCTOR_EXITCODE=0 \
  -DCMAKE_BUILD_TYPE=Release \
  -DAGS_USE_LOCAL_ALL_LIBRARIES=OFF \
  -DAGS_USE_LOCAL_SDL2=ON \
  -DAGS_USE_LOCAL_OGG=ON \
  -DAGS_USE_LOCAL_VORBIS=ON \
  -DAGS_USE_LOCAL_THEORA=ON \
  -DAGS_USE_LOCAL_SDL2_SOUND=OFF \
  -DAGS_USE_LOCAL_GLM=OFF \
  -DAGS_USE_LOCAL_MINIZ=OFF \
  -DAGS_USE_LOCAL_TINYXML2=OFF \
  -DAGS_BUILD_TOOLS=OFF \
  -DAGS_TESTS=OFF
ninja -j"$(nproc)" -C /build ags
'

cp "$BUILD/ags" "$R/out/rgslide-ags"
aarch64-linux-gnu-strip "$R/out/rgslide-ags"
ls -lh "$R/out/rgslide-ags"
