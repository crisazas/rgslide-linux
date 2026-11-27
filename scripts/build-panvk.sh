#!/bin/sh
# Compila Mesa PanVK 26.3 (libvulkan_panfrost.so) en cruzado para la RG Slide (Debian 13 arm64 glibc)
# usando el backend mali_kbase + Valhall v9 JM de third_party/panvk-kbase-android.
# Salida: out/panvk-dist/libvulkan_panfrost.so y out/panvk-dist/panfrost_icd.aarch64.json.
# Uso: scripts/build-panvk.sh [--clean]
set -eu
cd "$(dirname "$0")/.."
R=$PWD
PANVK=$R/third_party/panvk-kbase-android
MESA=$R/out/panvk-mesa
HOST_BUILD=$R/out/panvk-host-tools
ARM64_BUILD=$R/out/panvk-build
DEPS=$R/out/panvk-deps
DIST=$R/out/panvk-dist
CROSS=$R/out/panvk-cross.ini

[ -f "$PANVK/sources.lock" ] || { echo "falta $PANVK (clonar panvk-kbase-android en third_party/)"; exit 1; }
COMMIT=$(python3 -c "import json; print(json.load(open('$PANVK/sources.lock'))['mesaCommit'])")

if [ "${1:-}" = "--clean" ]; then
  rm -rf "$MESA" "$ARM64_BUILD" "$DIST"
fi

# 1. Descarga superficial del commit exacto de Mesa y aplicacion de parches
if [ ! -d "$MESA/.git" ]; then
  mkdir -p "$MESA"
  git -C "$MESA" init -q
  git -C "$MESA" remote add origin https://gitlab.freedesktop.org/mesa/mesa.git
  git -C "$MESA" fetch --depth 1 origin "$COMMIT"
  git -C "$MESA" checkout -q --detach FETCH_HEAD
  "$PANVK/scripts/apply-patches.sh" --profile g615-v11-csf --mesa "$MESA"
  for p in "$R"/patches/panvk/*.patch; do
    [ -e "$p" ] && git -C "$MESA" apply "$p"
  done
fi

# 2. Cabeceras arm64 de libdrm + Wayland + X11/XCB (Debian Trixie) para WSI Wayland/X11 (Zink)
if [ ! -f "$DEPS/usr/lib/aarch64-linux-gnu/pkgconfig/wayland-client.pc" ] || [ ! -f "$DEPS/usr/lib/aarch64-linux-gnu/pkgconfig/xcb-render.pc" ]; then
  mkdir -p "$DEPS"
  docker run --rm --platform linux/arm64 -v "$DEPS:/deps" debian:trixie sh -c '
    apt-get update -qq &&
    apt-get download -qq \
      libdrm-dev libdrm2 libdrm-common \
      libwayland-dev libwayland-client0 libffi-dev libffi8 wayland-protocols \
      libx11-dev libx11-6 libx11-xcb-dev libx11-xcb1 \
      libxcb1-dev libxcb1 libxcb-dri3-dev libxcb-dri3-0 \
      libxcb-present-dev libxcb-present0 libxcb-sync-dev libxcb-sync1 \
      libxcb-shm0-dev libxcb-shm0 libxcb-xfixes0-dev libxcb-xfixes0 \
      libxcb-randr0-dev libxcb-randr0 libxcb-render0-dev libxcb-render0 \
      libxcb-shape0-dev libxcb-shape0 \
      libxshmfence-dev libxshmfence1 libxrandr-dev libxrandr2 \
      libxext-dev libxext6 libxrender-dev libxrender1 \
      libxau-dev libxau6 libxdmcp-dev libxdmcp6 x11proto-dev &&
    for d in *.deb; do dpkg-deb -x "$d" /deps; done
  '
fi

# 3. Fichero cross de Meson (GCC aarch64 de Debian Trixie)
cat > "$CROSS" <<EOF
[binaries]
c = 'aarch64-linux-gnu-gcc'
cpp = 'aarch64-linux-gnu-g++'
ar = 'aarch64-linux-gnu-ar'
nm = 'aarch64-linux-gnu-nm'
strip = 'aarch64-linux-gnu-strip'
pkg-config = 'pkg-config'

[built-in options]
c_args = ['-I$DEPS/usr/include', '-I$DEPS/usr/include/libdrm', '-I$R/out/esde-deps/usr/include', '-Wno-error']
cpp_args = ['-I$DEPS/usr/include', '-I$DEPS/usr/include/libdrm', '-I$R/out/esde-deps/usr/include', '-Wno-error']
c_link_args = ['-L$DEPS/usr/lib/aarch64-linux-gnu', '-L$R/out/esde-deps/usr/lib/aarch64-linux-gnu', '-Wl,-rpath-link,$DEPS/usr/lib/aarch64-linux-gnu:$R/out/esde-deps/usr/lib/aarch64-linux-gnu']
cpp_link_args = ['-L$DEPS/usr/lib/aarch64-linux-gnu', '-L$R/out/esde-deps/usr/lib/aarch64-linux-gnu', '-Wl,-rpath-link,$DEPS/usr/lib/aarch64-linux-gnu:$R/out/esde-deps/usr/lib/aarch64-linux-gnu']

[properties]
pkg_config_libdir = '$DEPS/usr/lib/aarch64-linux-gnu/pkgconfig:$DEPS/usr/share/pkgconfig:$R/out/esde-deps/usr/lib/aarch64-linux-gnu/pkgconfig'
needs_exe_wrapper = true

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
EOF

# 4. Compilacion en contenedor nativo debian:trixie x86_64
mkdir -p "$HOST_BUILD" "$ARM64_BUILD" "$DIST"
docker run --rm --platform linux/amd64 \
  -v "$MESA:/src" \
  -v "$HOST_BUILD:/host-build" \
  -v "$ARM64_BUILD:/arm64-build" \
  -v "$CROSS:/cross.ini:ro" \
  -v "$DEPS:$DEPS:ro" \
  -v "$R/out/esde-deps:$R/out/esde-deps:ro" \
  debian:trixie bash -c '
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
  build-essential binutils gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
  meson ninja-build pkg-config python3 python3-mako python3-yaml \
  bison flex glslang-tools wayland-protocols libwayland-bin \
  llvm-19-dev libclang-19-dev libclang-cpp19-dev libclang-common-19-dev \
  libclc-19-dev libllvmspirvlib-19-dev spirv-tools zlib1g-dev libexpat1-dev libdrm-dev

if [ ! -x /host-build/bin/panfrost_compile ]; then
  if [ ! -f /host-build/build.ninja ]; then
    meson setup /host-build /src \
      -Dbuildtype=release -Dgallium-drivers= -Dvulkan-drivers=panfrost -Dplatforms= \
      -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled \
      -Dmesa-clc=enabled -Dinstall-mesa-clc=true -Dprecomp-compiler=enabled -Dinstall-precomp-compiler=true \
      -Dllvm=enabled -Dzstd=disabled -Dlibunwind=disabled -Dvalgrind=disabled -Dxmlconfig=disabled
  fi
  ninja -j"$(nproc)" -C /host-build \
    src/compiler/clc/mesa_clc \
    src/compiler/spirv/vtn_bindgen2 \
    src/panfrost/clc/panfrost_compile
  mkdir -p /host-build/bin
  ln -sf ../src/compiler/clc/mesa_clc /host-build/bin/mesa_clc
  ln -sf ../src/compiler/spirv/vtn_bindgen2 /host-build/bin/vtn_bindgen2
  ln -sf ../src/panfrost/clc/panfrost_compile /host-build/bin/panfrost_compile
fi

export PATH="/host-build/bin:$PATH"
if [ ! -f /arm64-build/build.ninja ]; then
  meson setup /arm64-build /src \
    --cross-file /cross.ini \
    -Dbuildtype=release \
    -Dplatforms=wayland,x11 \
    -Dgallium-drivers= \
    -Dvulkan-drivers=panfrost \
    -Dpanfrost-kmds=kbase \
    -Dmesa-clc=system \
    -Dprecomp-compiler=system \
    -Degl=disabled -Dgles1=disabled -Dgles2=disabled -Dopengl=false \
    -Dglx=disabled -Dgbm=disabled -Dzstd=disabled -Dlibunwind=disabled -Dllvm=disabled -Dvalgrind=disabled -Dxmlconfig=disabled
fi
ninja -j"$(nproc)" -C /arm64-build src/panfrost/vulkan/libvulkan_panfrost.so src/panfrost/vulkan/panfrost_icd.aarch64.json
'

cp "$ARM64_BUILD/src/panfrost/vulkan/libvulkan_panfrost.so" "$DIST/libvulkan_panfrost.so"
aarch64-linux-gnu-strip "$DIST/libvulkan_panfrost.so"
cp "$R/rootfs/overlay/etc/vulkan/icd.d/panfrost_icd.aarch64.json" "$DIST/panfrost_icd.aarch64.json"
ls -lh "$DIST/"
