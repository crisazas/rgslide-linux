#!/usr/bin/env bash
# Safely flash a mainline boot image to Slot A (boot_a) only.
# Refuses to write if current-slot is not 'a' so Slot B (Stage 1 v0.9.2) stays untouched.
#
# Usage:
#   mainline/scripts/flash-slot-a.sh [out/mainline/boot-mainline-m0.img]

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
IMG="${1:-$ROOT_DIR/out/mainline/boot-mainline-m0.img}"
FASTBOOT="${FASTBOOT:-$HOME/Android/Sdk/platform-tools/fastboot}"

[ -x "$FASTBOOT" ] || FASTBOOT="$(command -v fastboot)"
[ -f "$IMG" ] || { echo "Error: boot image not found: $IMG" >&2; exit 1; }

SLOT="$("$FASTBOOT" getvar current-slot 2>&1 | awk -F': ' '/current-slot:/ {print $2}' | tr -d '\r\n ')"
echo "[mainline] detected current-slot: '${SLOT:-unknown}'"

if [ "$SLOT" != "a" ]; then
    echo "ERROR: refusing to flash because current-slot is '$SLOT' (expected 'a')." >&2
    echo "Slot B is reserved for the known-good Stage 1 release." >&2
    exit 1
fi

echo "[mainline] flashing $IMG -> boot_a (vendor_boot and userdata untouched)..."
"$FASTBOOT" flash boot_a "$IMG"
echo "[mainline] done. Reboot with: $FASTBOOT reboot"
