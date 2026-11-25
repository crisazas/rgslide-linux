#!/bin/sh
# Copia a la Slide (por ssh) lo necesario para ejecutar un binario bionic con el blob Mali del vendor: el cierre de libs/servicemanager/allocator
# de dump/android/root, el blob (libGLES_mali.so, vulkan.ums9620.so, gralloc.default.so) y out/gltest. Es idempotente y complementa a install-cm4.sh
# (mismos enlaces en /system, /apex y /vendor, pero para todo el arbol). NO ejecuta gltest ni arranca servicios. Uso: scripts/install-mali.sh [host]
set -eu
cd "$(dirname "$0")/.."
H=${1:-console@10.55.0.2}
SSH="ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh -o ConnectTimeout=8"
SRC=dump/android/root
MALI=dump/android/mali
[ -f "$SRC/system/bin/servicemanager" ] || { echo "falta $SRC (scripts/extract-android-bits.sh)"; exit 1; }
for f in libGLES_mali.so vulkan.ums9620.so gralloc.default.so; do
  [ -f "$MALI/$f" ] || { echo "falta $MALI/$f"; exit 1; }
done
HAVE_GLTEST=0
[ -f out/gltest ] && HAVE_GLTEST=1

# 1) arbol extraido de Android -> /opt/android
tar -C "$SRC" -cf - . | $SSH "$H" 'sudo mkdir -p /opt/android && sudo tar -C /opt/android -xf -'
# 2) blob Mali -> /opt/android/vendor/lib64/{egl,hw}
tar -C "$MALI" -cf - \
  --transform 's,^libGLES_mali.so$,vendor/lib64/egl/libGLES_mali.so,' \
  --transform 's,^vulkan.ums9620.so$,vendor/lib64/hw/vulkan.ums9620.so,' \
  --transform 's,^gralloc.default.so$,vendor/lib64/hw/gralloc.default.so,' \
  libGLES_mali.so vulkan.ums9620.so gralloc.default.so | $SSH "$H" 'sudo mkdir -p /opt/android && sudo tar -C /opt/android -xf -'
# 3) programa de prueba -> /opt/android/bin/gltest
if [ "$HAVE_GLTEST" = 1 ]; then
  tar -C out --transform 's,^gltest$,bin/gltest,' -cf - gltest | $SSH "$H" 'sudo mkdir -p /opt/android && sudo tar -C /opt/android -xf -'
else
  echo "aviso: no existe out/gltest (scripts/build-gltest.sh); se omite"
fi

# 4) permisos y enlaces: /system, /apex y /vendor apuntan a /opt/android (bionic busca ahi el linker, las libs y los HAL)
$SSH "$H" 'sudo sh -s' <<'EOS'
set -eu
cd /opt/android
chmod -R a+rX /opt/android
for f in apex/com.android.runtime/bin/linker64 system/bin/* vendor/bin/* vendor/bin/hw/* bin/*; do
  if [ -f "$f" ]; then chmod 755 "$f"; fi
done
find system vendor apex -type f | while IFS= read -r f; do
  d=/$f
  mkdir -p "$(dirname "$d")"
  if [ -e "$d" ] && [ "$d" -ef "/opt/android/$f" ]; then continue; fi
  if [ -e "$d" ] && [ ! -L "$d" ]; then echo "omito $d (existe y no es un enlace)"; continue; fi
  ln -sf "/opt/android/$f" "$d"
done
ln -sf /apex/com.android.runtime/bin/linker64 /system/bin/linker64
mkdir -p /dev/__properties__
EOS

echo "instalado en $H:/opt/android (enlaces en /system, /apex y /vendor). Copiado:"
echo "  - arbol de dump/android/root (libs, servicemanager, hwservicemanager, vndservicemanager, allocator@4.0-service, manifiestos VINTF, .rc, gralloc.xml)"
echo "  - /vendor/lib64/egl/libGLES_mali.so, /vendor/lib64/hw/{gralloc.default.so,vulkan.ums9620.so,*-impl-arm.so}"
if [ "$HAVE_GLTEST" = 1 ]; then echo "  - /opt/android/bin/gltest"; fi
cat <<EOT
Para probar (a mano, como root en la Slide; uid 1000 para el binario):
  scp scripts/gpu-prep.sh $H:/tmp/ && ssh $H 'sudo sh /tmp/gpu-prep.sh'
  ssh $H 'sudo setpriv --reuid=1000 --regid=1000 --clear-groups /opt/android/apex/com.android.runtime/bin/linker64 /opt/android/bin/gltest'
Los servicios (servicemanager, hwservicemanager, allocator@4.0-service) hay que lanzarlos aparte; este script no arranca nada.
EOT
