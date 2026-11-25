#!/bin/sh
# Compila bionic (libc/libdl/libm + linker) de third_party/aosp13 (android-13.0.0_r1 + parches de Halium 13) con Soong.
# Uso: scripts/build-bionic-aosp.sh [modulos...]   (por defecto: libc libdl libm linker64)
# Salida en third_party/aosp13/out. Registro en out/build.log.
set -u
cd "$(dirname "$0")/../third_party/aosp13"
export TARGET_PRODUCT=${TARGET_PRODUCT:-module_arm64}
export TARGET_BUILD_VARIANT=${TARGET_BUILD_VARIANT:-userdebug}
export ALLOW_MISSING_DEPENDENCIES=true
export BUILD_NUMBER=rgslide
export OUT_DIR=$PWD/out
export LC_ALL=C
MODS=${*:-libc libdl libm linker64}
mkdir -p out
exec build/soong/soong_ui.bash --make-mode $MODS
