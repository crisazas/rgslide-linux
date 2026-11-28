#!/usr/bin/env bash
# Flash a mainline boot image to Slot A (boot_a) over Unisoc BROM (spd_dump)
# and set misc to try Slot A ONCE with automatic fallback to Slot B (v0.9.2).
#
# WARNING: Unisoc FDL2 (fdl2-dl.bin) automatically mirrors boot_a <-> boot_b
# over BROM! Prefer mainline/scripts/usb-flash-slot-a.py (from Slot B Linux)
# or fastboot to keep Slot B untouched.
#
# Also note: Unisoc LK triggers immediate rollback when tries_remaining<=1 with
# successful_boot=0, so Slot A uses tries=2 for a single real boot attempt.
#
# Usage:
#   mainline/scripts/brom-flash-slot-a.sh [out/mainline/boot-mainline-m0.img] [wait_seconds]

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_ROOT="$ROOT_DIR/out/mainline"
IMG="${1:-$OUT_ROOT/boot-mainline-m0.img}"
WAIT="${2:-300}"

SPD_DUMP="$ROOT_DIR/tools/spd_dump"
FDL1="$ROOT_DIR/dump/fdl/fdl1-dl.bin"
FDL2="$ROOT_DIR/dump/fdl/fdl2-dl.bin"
MISC_BASE="$ROOT_DIR/dump/min/misc.img"
MISC_OUT="$OUT_ROOT/misc-a-once.img"

for f in "$IMG" "$SPD_DUMP" "$FDL1" "$FDL2" "$MISC_BASE"; do
    [ -f "$f" ] || { echo "Error: missing required file: $f" >&2; exit 1; }
done

# Generate misc image: Slot A prio=15 tries=1 ok=0; Slot B prio=14 tries=1 ok=1
python3 - "$MISC_BASE" "$MISC_OUT" <<'PY'
import struct, sys, zlib

src, dst = sys.argv[1], sys.argv[2]
m = bytearray(open(src, 'rb').read())
o = 0x800
m[o:o+4] = b'_a\0\0'
m[o+12] = (15 & 15) | (2 << 4)               # slot a: prio 15, tries 2 (1 real try), not successful yet
m[o+13] = 0
m[o+14] = (14 & 15) | (2 << 4) | (1 << 7)    # slot b: prio 14, tries 2, successful (fallback)
m[o+15] = 0
struct.pack_into('<I', m, o + 0x1c, zlib.crc32(bytes(m[o:o+0x1c])) & 0xffffffff)
open(dst, 'wb').write(m)
PY

RUN_DIR="$OUT_ROOT/brom-$(date +%H%M%S)"
mkdir -p "$RUN_DIR/pre" "$RUN_DIR/rb"

echo "[mainline] BROM log dir: $RUN_DIR"
echo "[mainline] Waiting up to ${WAIT}s for BROM (1782:4d00) — hold Back (right side of screen) while plugging USB..."

"$SPD_DUMP" --wait "$WAIT" \
    fdl "$FDL1" 0x65000800 \
    fdl "$FDL2" 0xb4fffe00 \
    exec \
    path "$RUN_DIR/pre" r misc \
    write_part boot_a "$IMG" \
    write_part misc "$MISC_OUT" \
    path "$RUN_DIR/rb" r boot_a r misc \
    reset 2>&1 | tee "$RUN_DIR/spd_dump.log"

echo "--- readback verification ---"
for p in boot_a misc; do
    case "$p" in
        boot_a) expected="$IMG" ;;
        misc)   expected="$MISC_OUT" ;;
    esac
    sz="$(stat -c %s "$expected")"
    got="$(head -c "$sz" "$RUN_DIR/rb/$p" | md5sum | cut -c1-32)"
    want="$(md5sum "$expected" | cut -c1-32)"
    if [ "$got" = "$want" ]; then
        echo "OK   $p ($got)"
    else
        echo "FAIL $p read=$got expected=$want" >&2
        exit 2
    fi
done
