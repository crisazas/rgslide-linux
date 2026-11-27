#!/bin/sh
# Compila third_party/RetroArch en cruzado para la Slide (arm64 glibc 2.41), con el contexto propio
# gfx/drivers_context/hybris_ctx.c (parche en patches/retroarch/). Clang del NDK contra
# third_party/retroarch-build/sysroot (= out/sysroot-trixie + las -dev: asound, udev, z, freetype, EGL/GLES).
# Resultado: out/retroarch-root/opt/retroarch/bin/retroarch. Uso: scripts/build-retroarch.sh
set -eu
cd "$(dirname "$0")/.."
R=$PWD
SRC=$R/third_party/RetroArch
B=$R/third_party/retroarch-build
S=$B/sysroot
CL=${NDK:-$HOME/Android/Sdk/ndk/29.0.14206865}/toolchains/llvm/prebuilt/linux-x86_64/bin
[ -f "$S/usr/include/alsa/asoundlib.h" ] || { echo "falta $S (ver docs/retroarch.md: tar de las -dev del contenedor)"; exit 1; }
cd "$SRC"
export CC="$CL/clang --target=aarch64-linux-gnu --sysroot=$S -fuse-ld=lld"
export CXX="$CL/clang++ --target=aarch64-linux-gnu --sysroot=$S -fuse-ld=lld"
export AR="$CL/llvm-ar" RANLIB="$CL/llvm-ranlib" STRIP="$CL/llvm-strip" OBJCOPY="$CL/llvm-objcopy"
export PKG_CONFIG_LIBDIR="$S/usr/lib/aarch64-linux-gnu/pkgconfig:$S/usr/share/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$S"
export CFLAGS="-O2 -Wno-error -fcommon -DDT_NO_POOL -DUDEV_TOUCH_SUPPORT" CXXFLAGS="-O2 -Wno-error"
export LDFLAGS="-Wl,--allow-shlib-undefined -Wl,-rpath-link,$S/usr/lib/aarch64-linux-gnu -Wl,-rpath-link,$S/lib/aarch64-linux-gnu"
export PKG_CONF_PATH=/usr/bin/pkg-config
[ -f config.mk ] || ./configure --host=aarch64-linux-gnu --prefix=/opt/retroarch \
  --disable-x11 --disable-wayland --disable-pulse --enable-vulkan --enable-slang --disable-kms --disable-qt \
  --disable-discord --disable-ffmpeg --disable-dbus --disable-networking --disable-ssl \
  --disable-opengl --disable-opengl1 --disable-opengl_core --enable-opengles --enable-opengles3 --enable-egl --enable-hybris --enable-alsa --enable-udev \
  --enable-threads --disable-materialui --disable-ozone --enable-xmb --enable-rgui --enable-freetype \
  --disable-cdrom --disable-cheevos --disable-update_assets
make -j"$(nproc)" retroarch CC="$CC -DDT_NO_POOL -DUDEV_TOUCH_SUPPORT"
rm -rf "$R/out/retroarch-root"
mkdir -p "$R/out/retroarch-root/opt/retroarch/bin"
cp retroarch "$R/out/retroarch-root/opt/retroarch/bin/retroarch"
"$STRIP" -o "$R/out/retroarch-root/opt/retroarch/bin/retroarch" retroarch
echo "RetroArch en out/retroarch-root/opt/retroarch/bin/retroarch"
