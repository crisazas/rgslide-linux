#!/bin/sh
# Copia a la Slide (por ssh) el modem_control de GammaOS con su linker y librerias de bionic y crea los nodos /dev/{pmsys,modem,phycp,sctl_pm}
# con los mayores dinamicos de /proc/devices. NO lo ejecuta (eso se hace a mano para ver que pasa). Uso: scripts/install-cm4.sh [host]
set -eu
cd "$(dirname "$0")/.."
H=${1:-console@10.55.0.2}
SSH="ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh -o ConnectTimeout=8"
SRC=dump/android/root
[ -f "$SRC/vendor/bin/modem_control" ] || { echo "falta $SRC (scripts/extract-android-bits.sh)"; exit 1; }
cp tools/propshim/libpropshim.so "$SRC/libpropshim.so"
tar -C "$SRC" -cf - . | $SSH "$H" 'sudo mkdir -p /opt/android && sudo tar -C /opt/android -xf - && sudo chmod -R a+rX /opt/android && sudo chmod 755 /opt/android/vendor/bin/modem_control /opt/android/apex/com.android.runtime/bin/linker64'
$SSH "$H" 'sudo sh -s' <<'EOS'
# bionic busca su linker en /system/bin/linker64 y las libs en /system/lib64 y /apex/com.android.runtime/lib64/bionic
mkdir -p /system/bin /apex/com.android.runtime /vendor/etc /vendor/bin /system/lib64 /dev/__properties__
for d in /opt/android/system/lib64/*; do ln -sf "$d" /system/lib64/$(basename "$d"); done
mkdir -p /apex/com.android.runtime/lib64/bionic /apex/com.android.runtime/bin
for f in /opt/android/apex/com.android.runtime/lib64/bionic/*; do ln -sf "$f" /apex/com.android.runtime/lib64/bionic/$(basename "$f"); done
ln -sf /opt/android/apex/com.android.runtime/bin/linker64 /apex/com.android.runtime/bin/linker64
ln -sf /apex/com.android.runtime/bin/linker64 /system/bin/linker64
mkdir -p /vendor/lib64
for d in /opt/android/vendor/lib64/*; do [ -e "$d" ] && ln -sf "$d" /vendor/lib64/$(basename "$d"); done
for x in /opt/android/vendor/etc/*.xml; do ln -sf "$x" /vendor/etc/$(basename "$x"); done
ln -sf /opt/android/vendor/bin/modem_control /vendor/bin/modem_control
# nodos de dispositivo: el mayor es dinamico, se lee de /proc/devices
for n in pmsys modem phycp sctl_pm; do
  M=$(awk -v n="$n" '$2==n{print $1}' /proc/devices | head -1)
  if [ -n "$M" ]; then [ -e /dev/$n ] || mknod /dev/$n c "$M" 0; echo "nodo /dev/$n mayor $M"; else echo "sin mayor para $n (modulo no cargado?)"; fi
done
ls -l /dev/pmsys /dev/modem /dev/phycp /dev/sctl_pm 2>&1
EOS
echo "instalado en $H:/opt/android. Para probar: ssh $H 'sudo /vendor/bin/modem_control'"
