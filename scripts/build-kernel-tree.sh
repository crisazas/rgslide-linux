#!/bin/sh
# Prepara un arbol 5.4.302 del mismo SoC (weikaizhi4/android_kernel_ums9620) para construir MODULOS de audio del OEM
# que no existen en el kernel de fabrica (codec ump9620, PA ucp1301, regulator). No cambia el kernel de la Slide.
# /tmp/opencode se vacia al reiniciar: este script rehace clon, cabeceras del host, parches de compilacion y config.
# Uso: scripts/build-kernel-tree.sh            (deja el arbol listo en /tmp/opencode/wk, config en out_stageb)
set -eu
W=${WK:-/tmp/opencode/wk}
H=/tmp/opencode/hostdev
[ -d "$W/.git" ] || git clone --depth 1 https://github.com/weikaizhi4/android_kernel_ums9620 "$W"
# cabeceras/crt de libc6-dev del host, extraidos sin instalar (el host no las tiene y libc6 2.43 choca con libc6-dev 2.39)
if [ ! -d "$H/root/usr/include" ]; then
  mkdir -p "$H" && cd "$H" && apt-get download libc6-dev libc-dev-bin >/dev/null 2>&1
  for f in *.deb; do dpkg-deb -x "$f" root; done
  for f in Scrt1.o crti.o crtn.o libc_nonshared.a libc.so; do sudo -n ln -sf "$H/root/usr/lib/x86_64-linux-gnu/$f" "/usr/lib/x86_64-linux-gnu/$f"; done
fi
cd "$W"
# el clon no trae estos ficheros y rompen mrproper/olddefconfig
# drivers/kernelsu es un enlace roto a ../KernelSU (submodulo ausente): fuera del arbol
rm -f drivers/kernelsu
sed -i '/drivers\/kernelsu/d; /kernelsu\//d' drivers/Makefile drivers/Kconfig 2>/dev/null || true
sed -i 's|obj-$(CONFIG_MALI_ARBITRATION) += ../arbitration/|obj-$(CONFIG_MALI_ARBITRATION) += arbitration/|' drivers/gpu/arm/midgard/mali/Kbuild
printf '# modulo de particion (no usado)\n' > drivers/gpu/arm/midgard/mali/arbitration/Makefile 2>/dev/null || true
printf '# sin contenido\n' > drivers/gpu/arm/midgard/mali/arbitration/ptm/Makefile 2>/dev/null || true
sed -i 's|LLVM=1 LLVM_IAS=1|LLVM=0 LLVM_IAS=0|; s|^KDIR=.*|KDIR=${OUT:-out_stageb}|' build_bsp_modules.sh
sed -i 's|MAKEARGS=(O="$KDIR" ARCH=arm64 LLVM=0 LLVM_IAS=0 CROSS_COMPILE=aarch64-linux-gnu- LOCALVERSION=|MAKEARGS=(O="$KDIR" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CC=aarch64-linux-gnu-gcc HOSTCC=gcc LOCALVERSION=|' build_bsp_modules.sh
sed -i 's|-Wno-error=compare-distinct-pointer-types||' build_bsp_modules.sh
# gcc del host es mas estricto que el clang del vendor: el build de vmlinux se cae por -Werror.
# OJO: -Wno-error=frame-larger-than NO existe en gcc; el marco grande de lz4/lz4hc se sube en su Makefile.
printf '\nccflags-y += -Wframe-larger-than=524288\n' >> lib/lz4/Makefile
sed -i 's|^export PATH=.*|export PATH=/usr/bin:$PATH|' build_bsp_modules.sh
rm -rf out_stageb
make ARCH=arm64 mrproper >/dev/null 2>&1 || true
HOSTCFLAGS="-I$H/root/usr/include -I$H/root/usr/include/x86_64-linux-gnu" \
  make O="$W/out_stageb" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CC=aarch64-linux-gnu-gcc HOSTCC=gcc sprd_qogirn6pro_defconfig >/dev/null
./scripts/config --file out_stageb/.config -m MALI_MIDGARD -d MALI_ARBITRATION -d MALI_KUTF \
  -d MODULE_SIG -d MODULE_SIG_FORCE -d MODULE_SIG_ALL -d MODULE_SIG_FORMAT -d LOCALVERSION_AUTO --set-str LOCALVERSION '' 2>/dev/null
HOSTCFLAGS="-I$H/root/usr/include -I$H/root/usr/include/x86_64-linux-gnu" \
  make O="$W/out_stageb" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CC=aarch64-linux-gnu-gcc HOSTCC=gcc olddefconfig prepare >/dev/null
echo "arbol listo en $W (config en out_stageb, UTS_RELEASE=$(sed -n 's/.*UTS_RELEASE "\(.*\)"/\1/p' out_stageb/include/generated/utsrelease.h))"
