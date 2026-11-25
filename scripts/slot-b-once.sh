#!/bin/sh
# Desde Android con root: escribe en boot_b la imagen dada y deja `misc` con el slot B activo y UN solo intento
# (b: prio 15, tries 1, sin successful_boot; a: prio 14, intacto). El LK prueba B una vez y vuelve solo a A.
# NO usa fastboot ni reinicio manual (que borraria el pstore). Hace reboot al final.
# Uso: scripts/slot-b-once.sh out/boot-rescue.img [serie]
set -eu
cd "$(dirname "$0")/.."
IMG=$1; S=${2:-RGSLD005266585}
A="adb -s $S"
[ "$($A shell getprop ro.boot.slot_suffix | tr -d '\r')" = "_a" ] || { echo "no estoy en slot A: aborto"; exit 1; }
[ "$($A exec-out 'su -c "md5sum /dev/block/by-name/misc"' | cut -c1-32)" = "534e94b7a56e8c17e71d5d3e1d50295c" ] || { echo "misc no es el volcado verificado: aborto"; exit 1; }
SZ=$(stat -c %s "$IMG"); [ "$SZ" -lt $((48*1048576)) ] || { echo "imagen invade la zona de marcas"; exit 1; }
python3 - <<'PY'
import struct, zlib
m = bytearray(open('dump/min/misc.img', 'rb').read())
o = 0x800
m[o:o+4] = b'_b\0\0'
m[o+12] = (14 & 15) | (1 << 4) | (1 << 7)   # slot a: prio 14, tries 1, successful
m[o+14] = (15 & 15) | (1 << 4)              # slot b: prio 15, tries 1, no successful
struct.pack_into('<I', m, o + 0x1c, zlib.crc32(bytes(m[o:o+0x1c])) & 0xffffffff)
open('out/misc-b-once.img', 'wb').write(m)
PY
$A push "$IMG" /data/local/tmp/boot-test.img >/dev/null
$A push out/misc-b-once.img /data/local/tmp/misc-b-once.img >/dev/null
$A shell 'su -c "
 dd if=/data/local/tmp/boot-test.img of=/dev/block/by-name/boot_b bs=1048576 conv=fsync 2>&1 | tail -1;
 dd if=/data/local/tmp/misc-b-once.img of=/dev/block/by-name/misc bs=1048576 conv=fsync 2>&1 | tail -1;
 sync; rm /data/local/tmp/boot-test.img /data/local/tmp/misc-b-once.img"'
want=$(head -c "$SZ" "$IMG" | md5sum | cut -c1-32)
got=$($A exec-out "su -c \"dd if=/dev/block/by-name/boot_b bs=1048576 count=$(( (SZ+1048575)/1048576 )) 2>/dev/null\"" | head -c "$SZ" | md5sum | cut -c1-32)
[ "$want" = "$got" ] || { echo "boot_b no coincide ($got != $want): NO reinicio"; exit 2; }
echo "boot_b verificado ($got); misc listo (B una vez). Reiniciando..."
$A reboot
