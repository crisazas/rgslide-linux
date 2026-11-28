#!/usr/bin/env bash
# Build Linux 6.18 LTS (Milestone M0) for Anbernic RG Slide (ums9620_2h10).
# Outputs go strictly under out/mainline/ without touching Stage 1 (5.4) files.
#
# Usage:
#   mainline/scripts/build-kernel.sh
#   KV=6.18.60 mainline/scripts/build-kernel.sh

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
MU300_UPSTREAM="$ROOT_DIR/third_party/mu300-linux/upstream"
MAINLINE_DIR="$ROOT_DIR/mainline"
OUT_ROOT="$ROOT_DIR/out/mainline"

KV="${KV:-6.18.60}"
KV_SERIES="$(echo "$KV" | cut -d. -f1,2)"
SRC_ROOT="$OUT_ROOT/src"
SRC_DIR="$SRC_ROOT/linux-$KV"
BUILD_DIR="$OUT_ROOT/build-$KV"
DL_DIR="$OUT_ROOT/downloads"

mkdir -p "$OUT_ROOT" "$SRC_ROOT" "$BUILD_DIR" "$DL_DIR"

# 1. Fetch and verify kernel source tarball (kernel.org signed checksums + gpgv)
if [ ! -d "$SRC_DIR" ]; then
    BASE_URL="https://cdn.kernel.org/pub/linux/kernel/v${KV%%.*}.x"
    WORK_DL="$DL_DIR/tmp-$KV"
    mkdir -p "$WORK_DL"

    for f in "linux-$KV.tar.xz" "linux-$KV.tar.sign" "sha256sums.asc"; do
        if [ -f "$DL_DIR/$f" ] && [ ! -f "$WORK_DL/$f" ]; then
            ln -sf "$DL_DIR/$f" "$WORK_DL/$f"
        fi
        if [ ! -f "$WORK_DL/$f" ]; then
            url="$BASE_URL/$f"
            [ "$f" != "sha256sums.asc" ] || url="https://www.kernel.org/pub/linux/kernel/v${KV%%.*}.x/$f"
            echo "[mainline] fetching $url"
            curl -fsSL -o "$WORK_DL/$f" "$url"
        fi
    done

    echo "[mainline] verifying kernel.org signatures for linux-$KV"
    bash "$MU300_UPSTREAM/verify-kernel-src.sh" "$WORK_DL" "$KV"

    echo "[mainline] extracting linux-$KV.tar.xz"
    tar -xJf "$WORK_DL/linux-$KV.tar.xz" -C "$WORK_DL"
    mv "$WORK_DL/linux-$KV" "$SRC_DIR"
    rm -rf "$WORK_DL"
fi

# 2. Apply ums9620 mainline base ports/patches (idempotent) + rgslide patches
cd "$SRC_DIR"
echo "[mainline] applying mu300-linux ums9620 port edits"
python3 "$MU300_UPSTREAM/port/install.py" "$SRC_DIR"

apply_patch_dir() {
    local dir="$1"
    [ -d "$dir" ] || return 0
    for p in "$dir"/*.patch; do
        [ -e "$p" ] || continue
        local sum
        sum="$(sha256sum < "$p" | cut -d' ' -f1)"
        grep -qx "$sum" "$SRC_DIR/.rgslide-patches" 2>/dev/null && continue
        if patch -p1 -N -s --dry-run < "$p" >/dev/null 2>&1; then
            echo "[mainline] applying $(basename "$p")"
            patch -p1 -N -s < "$p"
        elif patch -p1 -R -s -f --dry-run < "$p" >/dev/null 2>&1; then
            :
        else
            echo "[mainline] ERROR: patch failed to apply to $KV: $p" >&2
            exit 1
        fi
        echo "$sum" >> "$SRC_DIR/.rgslide-patches"
    done
}

apply_patch_dir "$MU300_UPSTREAM/patches"
apply_patch_dir "$MU300_UPSTREAM/patches/$KV_SERIES"
apply_patch_dir "$MAINLINE_DIR/patches/common"
apply_patch_dir "$MAINLINE_DIR/patches/$KV_SERIES"

# 3. Host C headers fallback (if host libc6-dev is not installed system-wide)
HOST_MAKE_ARGS=()
if [ ! -f /usr/include/stdio.h ]; then
    HOSTDEV="$OUT_ROOT/hostdev"
    if [ ! -f "$HOSTDEV/root/usr/include/stdio.h" ]; then
        echo "[mainline] host /usr/include/stdio.h missing; extracting libc6-dev into $HOSTDEV (no sudo)"
        mkdir -p "$HOSTDEV/debs" "$HOSTDEV/root"
        (
            cd "$HOSTDEV/debs"
            if ! ls *.deb >/dev/null 2>&1; then
                apt-get download libc6-dev libc-dev-bin linux-libc-dev >/dev/null
            fi
            for deb in *.deb; do
                dpkg-deb -x "$deb" "$HOSTDEV/root"
            done
        )
    fi
    HOST_INC="-I$HOSTDEV/root/usr/include -I$HOSTDEV/root/usr/include/x86_64-linux-gnu"
    HOST_LIB="$HOSTDEV/root/usr/lib/x86_64-linux-gnu"
    # Rewrite hardcoded /usr/lib/.../libc_nonshared.a in the extracted GNU ld script
    sed -i "s|/usr/lib/x86_64-linux-gnu/libc_nonshared.a|$HOST_LIB/libc_nonshared.a|g" "$HOST_LIB/libc.so"
    HOST_MAKE_ARGS+=(
        "HOSTCFLAGS=$HOST_INC"
        "HOSTLDFLAGS=-L$HOST_LIB -B$HOST_LIB"
    )
fi

# 4. Configure kernel (allnoconfig + mu300 base + rgslide M0 overrides)
echo "[mainline] configuring linux-$KV for rgslide M0"
make O="$BUILD_DIR" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- "${HOST_MAKE_ARGS[@]}" allnoconfig >/dev/null
"$SRC_DIR/scripts/kconfig/merge_config.sh" -m -O "$BUILD_DIR" \
    "$BUILD_DIR/.config" \
    "$MU300_UPSTREAM/mu300-mainline.config" \
    "$MAINLINE_DIR/config/rgslide-m0.config" >/dev/null
make O="$BUILD_DIR" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- "${HOST_MAKE_ARGS[@]}" olddefconfig >/dev/null

# 5. Build arm64 Image
echo "[mainline] building Image (-j$(nproc))..."
if ! make O="$BUILD_DIR" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- "${HOST_MAKE_ARGS[@]}" \
        -j"$(nproc)" Image > "$BUILD_DIR/build.log" 2>&1; then
    echo "[mainline] ERROR: kernel build failed. Tail of $BUILD_DIR/build.log:" >&2
    tail -n 50 "$BUILD_DIR/build.log" >&2
    exit 1
fi

cp "$BUILD_DIR/arch/arm64/boot/Image" "$OUT_ROOT/Image"
python3 "$MU300_UPSTREAM/wrap-image.py" "$OUT_ROOT/Image" "$OUT_ROOT/Image.lk"

ls -lh "$OUT_ROOT/Image" "$OUT_ROOT/Image.lk"
