#!/usr/bin/env bash
# rgslide-linux: Safe flashing utility for Anbernic RG Slide (Unisoc T820 / ums9620_2h10)
#
# Slot convention (unified across repo, AGENTS.md, README, and releases):
#   - Slot B (boot_b): known-good / release installation (default for BROM release flashing)
#   - Slot A (boot_a): testing / development iteration (default for fastboot flashing)
#
# WARNING — STEP 0 (MANDATORY BACKUP):
#   Flashing userdata overwrites Android/GammaOS. Before flashing a stock console for
#   the first time, dump your stock partitions via BROM (e.g. scripts/dump-min.sh or
#   spd_dump). Without a stock dump or official Anbernic firmware, there is no rollback.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

MODE="brom"
SLOT=""
PAYLOAD_DIR="$SCRIPT_DIR"
DRY_RUN=0
ASSUME_YES=0
WAIT_SECS=300
SKIP_MISC=0
SKIP_USERDATA=0
FASTBOOT_BIN="${FASTBOOT:-}"
if [ -z "$FASTBOOT_BIN" ]; then
  if command -v fastboot >/dev/null 2>&1; then
    FASTBOOT_BIN="fastboot"
  elif [ -x "$HOME/Android/Sdk/platform-tools/fastboot" ]; then
    FASTBOOT_BIN="$HOME/Android/Sdk/platform-tools/fastboot"
  else
    FASTBOOT_BIN="fastboot"
  fi
fi

usage() {
  cat <<'EOF'
Usage: flash-rgslide.sh [OPTIONS]

Options:
  --mode brom|fastboot   Flashing transport (default: brom)
  --slot b|a             Target boot slot:
                           b = known-good / release slot (default in brom mode)
                           a = testing / dev slot (default in fastboot mode)
  --dir PATH             Directory containing boot.img, userdata.ext4, etc.
                         (default: directory of this script)
  --skip-misc            Do not write misc.img (brom mode only)
  --skip-userdata        Do not write userdata.ext4 (flash boot/misc only)
  --wait SECONDS         BROM wait timeout in seconds (default: 300)
  --dry-run              Validate inputs and print commands without touching the device
  --yes                  Skip interactive confirmation prompt (use with care)
  -h, --help             Show this help message

Examples:
  # Preview a full BROM release flash to Slot B (no root, no writes):
  ./tools/flash-rgslide.sh --dir out/rgslide-brom-flasher --dry-run

  # Flash a release package via BROM (writes boot_b, misc, userdata):
  sudo ./flash-rgslide.sh

  # Iterate on a test boot image via fastboot to Slot A:
  ./tools/flash-rgslide.sh --mode fastboot --slot a --skip-userdata --dir out
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --mode)
      MODE="${2:?--mode requires brom or fastboot}"
      shift 2
      ;;
    --slot)
      SLOT="${2:?--slot requires a or b}"
      shift 2
      ;;
    --dir)
      PAYLOAD_DIR="${2:?--dir requires a path}"
      shift 2
      ;;
    --skip-misc)
      SKIP_MISC=1
      shift
      ;;
    --skip-userdata)
      SKIP_USERDATA=1
      shift
      ;;
    --wait)
      WAIT_SECS="${2:?--wait requires seconds}"
      shift 2
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    --yes)
      ASSUME_YES=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "ERROR: Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

case "$MODE" in
  brom|fastboot) ;;
  *)
    echo "ERROR: Invalid --mode '$MODE' (expected 'brom' or 'fastboot')." >&2
    exit 2
    ;;
esac

if [ -z "$SLOT" ]; then
  if [ "$MODE" = "brom" ]; then
    SLOT="b"
  else
    SLOT="a"
  fi
fi

case "$SLOT" in
  a|b) ;;
  *)
    echo "ERROR: Invalid --slot '$SLOT' (expected 'a' or 'b')." >&2
    exit 2
    ;;
esac

if ! [[ "$WAIT_SECS" =~ ^[0-9]+$ ]]; then
  echo "ERROR: --wait must be a positive integer (got '$WAIT_SECS')." >&2
  exit 2
fi

PAYLOAD_DIR="$(cd "$PAYLOAD_DIR" && pwd)"

BOOT_IMG="$PAYLOAD_DIR/boot.img"
MISC_IMG="$PAYLOAD_DIR/misc.img"
USERDATA_IMG="$PAYLOAD_DIR/userdata.ext4"
FDL1_BIN="$PAYLOAD_DIR/fdl1-dl.bin"
FDL2_BIN="$PAYLOAD_DIR/fdl2-dl.bin"

SPD_DUMP=""
if [ -x "$PAYLOAD_DIR/spd_dump" ]; then
  SPD_DUMP="$PAYLOAD_DIR/spd_dump"
elif [ -x "$SCRIPT_DIR/spd_dump" ]; then
  SPD_DUMP="$SCRIPT_DIR/spd_dump"
elif [ -f "$PAYLOAD_DIR/spd_dump" ]; then
  SPD_DUMP="$PAYLOAD_DIR/spd_dump"
fi

require_file() {
  local f="$1"
  if [ ! -s "$f" ]; then
    echo "ERROR: Missing or empty required file: $f" >&2
    exit 1
  fi
}

require_file "$BOOT_IMG"
if [ "$SKIP_USERDATA" -eq 0 ]; then
  require_file "$USERDATA_IMG"
fi

if [ "$MODE" = "brom" ]; then
  require_file "$FDL1_BIN"
  require_file "$FDL2_BIN"
  if [ "$SKIP_MISC" -eq 0 ]; then
    require_file "$MISC_IMG"
  fi
  if [ -z "$SPD_DUMP" ] || [ ! -s "$SPD_DUMP" ]; then
    echo "ERROR: spd_dump binary not found in $PAYLOAD_DIR or $SCRIPT_DIR." >&2
    exit 1
  fi
fi

# Verify optional SHA256SUMS if present in payload directory
if [ -f "$PAYLOAD_DIR/SHA256SUMS" ]; then
  echo "Verifying SHA256SUMS in $PAYLOAD_DIR ..."
  (cd "$PAYLOAD_DIR" && sha256sum -c --ignore-missing SHA256SUMS)
fi

echo "=========================================================="
echo "          rgslide-linux Flashing Utility                  "
echo "=========================================================="
echo "Mode:        $MODE"
echo "Target slot: $SLOT (boot_${SLOT})"
echo "Payload dir: $PAYLOAD_DIR"
echo "Dry-run:     $([ "$DRY_RUN" -eq 1 ] && echo yes || echo no)"
echo "Partitions to write:"
echo "  - boot_${SLOT}   <- $BOOT_IMG ($(du -h "$BOOT_IMG" | cut -f1))"
if [ "$MODE" = "brom" ] && [ "$SKIP_MISC" -eq 0 ]; then
  echo "  - misc     <- $MISC_IMG ($(du -h "$MISC_IMG" | cut -f1))"
fi
if [ "$SKIP_USERDATA" -eq 0 ]; then
  echo "  - userdata <- $USERDATA_IMG ($(du -h "$USERDATA_IMG" | cut -f1))"
fi
echo ""
echo "WARNING: Writing userdata DESTROYS Android/GammaOS and all data on it."
echo "         Ensure you have a BROM backup of stock partitions before continuing."
echo ""

if [ "$MODE" = "fastboot" ]; then
  if [ "$DRY_RUN" -eq 1 ]; then
    echo "[dry-run] Would query device via: $FASTBOOT_BIN getvar product && $FASTBOOT_BIN getvar current-slot"
    echo "[dry-run] Would run: $FASTBOOT_BIN flash boot_${SLOT} $BOOT_IMG"
    if [ "$SKIP_USERDATA" -eq 0 ]; then
      echo "[dry-run] Would run: $FASTBOOT_BIN flash userdata $USERDATA_IMG"
    fi
    echo "[dry-run] Would run: $FASTBOOT_BIN set_active $SLOT"
    exit 0
  fi

  if ! command -v "$FASTBOOT_BIN" >/dev/null 2>&1 && [ ! -x "$FASTBOOT_BIN" ]; then
    echo "ERROR: fastboot binary '$FASTBOOT_BIN' not found in PATH." >&2
    exit 1
  fi

  echo "Checking connected fastboot device..."
  FB_PRODUCT="$("$FASTBOOT_BIN" getvar product 2>&1 | awk -F': ' '/product:/ {print $2; exit}' | tr -d '\r')"
  FB_SLOT="$("$FASTBOOT_BIN" getvar current-slot 2>&1 | awk -F': ' '/current-slot:/ {print $2; exit}' | tr -d '\r')"
  echo "  Detected product:      ${FB_PRODUCT:-<unknown>}"
  echo "  Detected current-slot: ${FB_SLOT:-<unknown>}"

  if [ -n "$FB_PRODUCT" ] && [[ "$FB_PRODUCT" != *ums9620* ]] && [[ "$FB_PRODUCT" != *rgslide* ]]; then
    echo "ERROR: Refusing to flash unexpected device product '$FB_PRODUCT' (expected ums9620 / rgslide)." >&2
    exit 1
  fi

  if [ "$SLOT" = "b" ]; then
    echo "NOTE: Target is Slot B (known-good/release slot). For dev testing, use --slot a."
  fi

  if [ "$ASSUME_YES" -eq 0 ]; then
    read -r -p "Type FLASH to confirm writing to slot '${SLOT}' on '${FB_PRODUCT:-device}': " CONFIRM
    if [ "$CONFIRM" != "FLASH" ]; then
      echo "Aborted."
      exit 1
    fi
  fi

  "$FASTBOOT_BIN" flash "boot_${SLOT}" "$BOOT_IMG"
  if [ "$SKIP_USERDATA" -eq 0 ]; then
    "$FASTBOOT_BIN" flash userdata "$USERDATA_IMG"
  fi
  "$FASTBOOT_BIN" set_active "$SLOT"
  echo "Fastboot flashing completed. Run '$FASTBOOT_BIN reboot' when ready."
  exit 0
fi

# BROM mode
CMD=(
  "$SPD_DUMP" --wait "$WAIT_SECS"
  fdl "$FDL1_BIN" 0x65000800
  fdl "$FDL2_BIN" 0xb4fffe00
  exec
  write_part "boot_${SLOT}" "$BOOT_IMG"
)
if [ "$SKIP_MISC" -eq 0 ]; then
  CMD+=(write_part misc "$MISC_IMG")
fi
if [ "$SKIP_USERDATA" -eq 0 ]; then
  CMD+=(write_part userdata "$USERDATA_IMG")
fi
CMD+=(reset)

if [ "$DRY_RUN" -eq 1 ]; then
  echo "[dry-run] Would execute (with root privileges):"
  printf ' '
  printf ' %q' "${CMD[@]}"
  echo ""
  exit 0
fi

if [ "$SLOT" = "a" ] && [ "$SKIP_MISC" -eq 0 ]; then
  echo "WARNING: misc.img in release packages typically selects Slot B."
  echo "         Consider passing --skip-misc when flashing Slot A."
fi

if [ "$ASSUME_YES" -eq 0 ]; then
  read -r -p "Confirm this is an Anbernic RG Slide (ums9620) and you have a stock backup. Type FLASH to proceed: " CONFIRM
  if [ "$CONFIRM" != "FLASH" ]; then
    echo "Aborted."
    exit 1
  fi
fi

echo ""
echo "INSTRUCTIONS:"
echo "1. Power OFF your Anbernic RG Slide completely (hold Vol-Down + Power if frozen)."
echo "2. Hold the BACK button on the right side of the screen."
echo "3. While holding the BACK button, connect the USB-C cable to your PC."
echo ""
echo "Waiting for RG Slide in BROM mode (up to ${WAIT_SECS} seconds)..."

chmod +x "$SPD_DUMP" 2>/dev/null || true
if [ "$(id -u)" -ne 0 ]; then
  sudo "${CMD[@]}"
else
  "${CMD[@]}"
fi

echo ""
echo "=========================================================="
echo " Flashing completed successfully! Device is rebooting...  "
echo "=========================================================="
