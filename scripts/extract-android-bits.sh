#!/bin/sh
# Extrae de GammaOS (adb + su) lo necesario para ejecutar modem_control en el rootfs Debian: el binario, el linker de bionic y el cierre de
# librerias NEEDED, los xml de carga del CM4/modem y las propiedades relevantes. Destino: dump/android/. Solo lectura.
set -eu
cd "$(dirname "$0")/.."
S=${1:-$(adb get-serialno)}; A="adb -s $S"
D=dump/android; mkdir -p "$D/root"
pull() { # ruta en el equipo -> dump/android/root/<ruta>
  mkdir -p "$D/root/$(dirname "$1")"
  $A exec-out "su -c 'cat $1'" > "$D/root/$1" 2>/dev/null && [ -s "$D/root/$1" ] || { rm -f "$D/root/$1"; return 1; }
}
for f in /vendor/bin/modem_control /vendor/etc/modem_cp_info.xml /vendor/etc/modem_sp_info.xml /vendor/etc/cp_dump_info.xml \
         /apex/com.android.runtime/bin/linker64 /apex/com.android.runtime/lib64/bionic/libc.so /apex/com.android.runtime/lib64/bionic/libm.so \
         /apex/com.android.runtime/lib64/bionic/libdl.so; do pull "$f" && echo "ok $f" || echo "NO $f"; done
# cierre de dependencias
todo="modem_control"; seen=""
while [ -n "$todo" ]; do
  set -- $todo; cur=$1; shift; todo="$*"
  case " $seen " in *" $cur "*) continue ;; esac; seen="$seen $cur"
  bin=$D/root/vendor/bin/modem_control; [ "$cur" = modem_control ] || bin=$(find "$D/root" -name "$cur" | head -1)
  [ -f "$bin" ] || continue
  for n in $(aarch64-linux-gnu-readelf -d "$bin" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'); do
    case " $seen " in *" $n "*) continue ;; esac
    for dir in /system/lib64 /vendor/lib64 /system/lib64/vndk-sp /apex/com.android.runtime/lib64/bionic /apex/com.android.art/lib64 /apex/com.android.i18n/lib64; do
      if pull "$dir/$n" 2>/dev/null; then echo "lib $dir/$n"; todo="$todo $n"; break; fi
    done
  done
done
$A shell 'getprop' > "$D/getprop.txt"
$A shell 'su -c "ls -l /dev/pmsys /dev/modem /dev/phycp /dev/sctl_pm /dev/spipe* /dev/stty* 2>&1"' > "$D/dev-nodes.txt" 2>&1 || true
du -sh "$D"; find "$D/root" -type f | wc -l
