#!/usr/bin/env bash
# Build in-tree (=m) and out-of-tree WCN Wi-Fi/BT modules (wcn_bsp,
# sprd_wlan_combo, sprdbt_tty) against the mainline 6.18 LTS build in out/mainline/.
#
# Usage:
#   mainline/scripts/build-modules.sh [module-dir...]

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
MU300_UPSTREAM="$ROOT_DIR/third_party/mu300-linux/upstream"
OUT_ROOT="$ROOT_DIR/out/mainline"

KV="${KV:-6.18.60}"
SRC_DIR="$OUT_ROOT/src/linux-$KV"
BUILD_DIR="$OUT_ROOT/build-$KV"
MOD_BUILD="$OUT_ROOT/mod-build-$KV"
MOD_OUT="$OUT_ROOT/modules"

[ -d "$BUILD_DIR" ] || { echo "Error: $BUILD_DIR not found. Run mainline/scripts/build-kernel.sh first." >&2; exit 1; }

HOST_MAKE_ARGS=()
if [ ! -f /usr/include/stdio.h ]; then
    HOSTDEV="$OUT_ROOT/hostdev"
    HOST_INC="-I$HOSTDEV/root/usr/include -I$HOSTDEV/root/usr/include/x86_64-linux-gnu"
    HOST_LIB="$HOSTDEV/root/usr/lib/x86_64-linux-gnu"
    HOST_MAKE_ARGS+=(
        "HOSTCFLAGS=$HOST_INC"
        "HOSTLDFLAGS=-L$HOST_LIB -B$HOST_LIB"
    )
fi

MODS="${*:-wcn_bsp sprd_wlan_combo sprdbt_tty sprd_modem sprd_audio}"
AUDIO_REF_SHA="b1f59b81ec9d7d7beee4db0a2fec669c07a4d5fb"
AUDIO_REF_DIR="$OUT_ROOT/mu300-audio-ref"
mkdir -p "$MOD_OUT" "$MOD_BUILD"

echo "[mainline] preparing module build environment..."
make -C "$SRC_DIR" O="$BUILD_DIR" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    "${HOST_MAKE_ARGS[@]}" -j"$(nproc)" modules_prepare > "$BUILD_DIR/modules.log" 2>&1 || {
    tail -n 40 "$BUILD_DIR/modules.log" >&2
    exit 1
}

if [ ! -f "$BUILD_DIR/Module.symvers" ] && [ -f "$BUILD_DIR/vmlinux.symvers" ]; then
    cp "$BUILD_DIR/vmlinux.symvers" "$BUILD_DIR/Module.symvers"
fi

KREL="$(cat "$BUILD_DIR/include/config/kernel.release")"
mkdir -p "$MOD_OUT/$KREL/extra"

EXTRA_SYMS=""
for m in $MODS; do
    echo "[mainline] building out-of-tree module: $m"
    rm -rf "$MOD_BUILD/$m"
    if [ "$m" = "sprd_audio" ]; then
        if [ ! -d "$AUDIO_REF_DIR/upstream/modules/sprd_audio" ]; then
            echo "[mainline] fetching sprd_audio 6.18 port from mu300-linux@$AUDIO_REF_SHA..."
            mkdir -p "$AUDIO_REF_DIR"
            git -C "$AUDIO_REF_DIR" init -q
            git -C "$AUDIO_REF_DIR" remote add origin https://github.com/dikeckaan/mu300-linux.git 2>/dev/null || true
            git -C "$AUDIO_REF_DIR" fetch --depth=1 origin "$AUDIO_REF_SHA"
            git -C "$AUDIO_REF_DIR" checkout -q FETCH_HEAD -- upstream/modules/sprd_audio
        fi
        cp -r "$AUDIO_REF_DIR/upstream/modules/sprd_audio" "$MOD_BUILD/sprd_audio"
        cp "$SRC_DIR/drivers/dma/virt-dma.c" "$SRC_DIR/drivers/dma/virt-dma.h" \
           "$SRC_DIR/drivers/dma/dmaengine.h" "$MOD_BUILD/sprd_audio/dma/"
        [ -d "$MOD_BUILD/sprd_modem" ] || cp -r "$MU300_UPSTREAM/modules/sprd_modem" "$MOD_BUILD/sprd_modem"
        for p in "$ROOT_DIR"/mainline/patches/audio/*.patch; do
            [ -f "$p" ] && patch -d "$MOD_BUILD/sprd_audio" -p1 -s < "$p"
        done
    else
        cp -r "$MU300_UPSTREAM/modules/$m" "$MOD_BUILD/$m"
        if [ "$m" = "sprd_modem" ]; then
            for p in "$ROOT_DIR"/mainline/patches/modem/*.patch; do
                [ -f "$p" ] && patch -d "$MOD_BUILD/sprd_modem" -p1 -s < "$p"
            done
        fi
    fi
    [ -d "$MOD_BUILD/wcn_bsp" ] || cp -r "$MU300_UPSTREAM/modules/wcn_bsp" "$MOD_BUILD/wcn_bsp"

    # RG Slide connects Marlin3 Wi-Fi over SDIO (hw_type == SPRD_HW_SC2355_SDIO),
    # whereas mu300-linux only dispatches PCIe in sprd_wlan_probe().
    if [ "$m" = "sprd_wlan_combo" ]; then
        python3 - "$MOD_BUILD/$m/common/sprd_wlan.c" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = '\tif (p_match_data->hw_type == SPRD_HW_SC2355_PCIE) {\n\t\treturn pcie_probe(pdev);\n\t} else {'
new = '\tif (p_match_data->hw_type == SPRD_HW_SC2355_PCIE) {\n\t\treturn pcie_probe(pdev);\n\t} else if (p_match_data->hw_type == SPRD_HW_SC2355_SDIO) {\n\t\treturn sc2355_sdio_probe(pdev);\n\t} else {'
assert old in s, 'sprd_wlan_probe anchor changed'
open(p, 'w').write(s.replace(old, new, 1))
PY
    fi

    make -C "$BUILD_DIR" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
        "${HOST_MAKE_ARGS[@]}" M="$MOD_BUILD/$m" \
        KBUILD_EXTRA_SYMBOLS="$EXTRA_SYMS" -j"$(nproc)" modules \
        > "$MOD_OUT/$m.log" 2>&1 || {
        echo "[mainline] ERROR building $m:" >&2
        tail -n 40 "$MOD_OUT/$m.log" >&2
        exit 1
    }

    [ -f "$MOD_BUILD/$m/Module.symvers" ] && EXTRA_SYMS="$EXTRA_SYMS $MOD_BUILD/$m/Module.symvers"
    for ko in $(find "$MOD_BUILD/$m" -name '*.ko'); do
        cp "$ko" "$MOD_OUT/$KREL/extra/"
        aarch64-linux-gnu-strip --strip-debug "$MOD_OUT/$KREL/extra/$(basename "$ko")"
    done
done

depmod -b "$MOD_OUT/.." "$KREL" 2>/dev/null || true
echo "[mainline] modules built for $KREL:"
ls -lh "$MOD_OUT/$KREL/extra/"*.ko
