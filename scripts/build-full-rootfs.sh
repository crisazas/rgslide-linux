#!/bin/sh
# Ensambla out/rootfs-full.ext4 completo y reproducible sobre out/rootfs (de scripts/build-rootfs.sh):
# overlay, modulos 5.4.233 de fabrica (SIN snd-soc-sprd-dai.ko parcheado), firmware Wi-Fi, /opt/android
# (con libselinux-shim + libpropshim + build.prop), libhybris glibc + GLVND + 120 Hz + libtlspad,
# SDL2-hybris, ES-DE 3.5.0, RetroArch 1.22.2 + 7 nucleos + XMB, rgslide-ui, rgslide-audiod y servicios.
# Uso: scripts/build-full-rootfs.sh [salida.ext4]   (SIZE=3500M por defecto)
set -eu
cd "$(dirname "$0")/.."
R=$PWD
DEVICE="${DEVICE:-rgslide}"
DEV_DIR="devices/$DEVICE"
if [ -f "$DEV_DIR/config.env" ]; then
    . "$DEV_DIR/config.env"
fi

BASE=${BASE_ROOTFS:-$R/out/rootfs}
if [ "$DEVICE" = "rgslide" ]; then
    IMG=${1:-$R/out/rootfs-full.ext4}
    DUMP_ROOT="dump"
    WIFI_FW_DIR="out/wifi-fw"
else
    IMG=${1:-$R/out/rootfs-$DEVICE.ext4}
    DUMP_ROOT="dump/$DEVICE"
    WIFI_FW_DIR="dump/$DEVICE/wifi-fw"
fi
SIZE=${SIZE:-3500M}

[ -x "$BASE/usr/bin/dpkg" ] || { echo "falta $BASE (ejecuta scripts/build-rootfs.sh --no-image)"; exit 1; }
[ -x out/rgslide-python3 ] || scripts/build-rgslide-python3.sh
for req in out/libselinux-shim.so out/libpropshim.so out/libtlspad.so out/rgslide-python3 out/build.prop \
           out/bionic-patched/libc.so out/audiod out/rgslide-ui \
           out/hybris-build/egl/platforms/drm/.libs/eglplatform_drm.so \
           out/esde-root/opt/es-de/bin/es-de out/retroarch-root/opt/retroarch/bin/retroarch; do
    [ -e "$req" ] || { echo "falta artefacto requerido: $req"; exit 1; }
done

T=$(mktemp -d out/full-stage.XXXXXX)
trap 'sudo rm -rf "$T"' EXIT

echo "1/8 Copiando base Debian desde $BASE ..."
sudo cp -a "$BASE/." "$T/"

echo "2/8 Aplicando rootfs/overlay/, overlays de dispositivo y binarios locales ..."
sudo tar -C rootfs/overlay -cf - . | sudo tar --no-overwrite-dir -C "$T" -xf -
if [ -d "$DEV_DIR/overlay" ]; then
    echo "  -> Aplicando overlay especifico de $DEVICE ($DEV_DIR/overlay) ..."
    sudo tar -C "$DEV_DIR/overlay" -cf - . | sudo tar -C "$T" -xf -
fi
sudo install -m 755 out/rgslide-ui "$T/usr/local/bin/rgslide-ui"
sudo install -m 755 out/audiod "$T/usr/local/bin/rgslide-audiod"
sudo chmod 755 "$T/usr/local/bin/rgslide-"* "$T/usr/local/sbin/rgslide-"* "$T/usr/local/sbin/android-services" "$T/usr/local/sbin/gpu-prep" "$T/usr/local/sbin/"* 2>/dev/null || true

echo "3/8 Modulos de fabrica 5.4.233 y firmware Wi-Fi ..."
if [ -d "$DUMP_ROOT/live/dlkm" ] && [ -d "$DUMP_ROOT/boot/rd_vendor/lib/modules" ]; then
    sudo mkdir -p "$T/lib/modules/5.4.233"
    sudo cp -a -f "$DUMP_ROOT/boot/rd_vendor/lib/modules/"*.ko "$T/lib/modules/5.4.233/"
    sudo cp -a -f "$DUMP_ROOT/live/dlkm/"*.ko "$T/lib/modules/5.4.233/"
fi
if [ -d "$WIFI_FW_DIR" ]; then
    sudo mkdir -p "$T/usr/lib/firmware"
    sudo cp -a "$WIFI_FW_DIR/." "$T/usr/lib/firmware/"
    sudo cp -a "$WIFI_FW_DIR/sprd/wifi_board_config"*.ini "$T/usr/lib/firmware/" 2>/dev/null || true
fi

echo "4/8 Arbol minimo Android (/opt/android + Mali + shims + enlaces /system /vendor /apex) ..."
sudo mkdir -p "$T/opt/android"
sudo cp -a "$DUMP_ROOT/android/root/." "$T/opt/android/"
sudo install -m 644 "$DUMP_ROOT/android/mali/libGLES_mali.so" "$T/opt/android/vendor/lib64/egl/libGLES_mali.so"
sudo install -m 644 "$DUMP_ROOT/android/mali/vulkan.ums9620.so" "$T/opt/android/vendor/lib64/hw/vulkan.ums9620.so"
sudo install -m 644 "$DUMP_ROOT/android/mali/gralloc.default.so" "$T/opt/android/vendor/lib64/hw/gralloc.default.so"
sudo cp -p "$T/opt/android/system/lib64/libselinux.so" "$T/opt/android/system/lib64/libselinux.so.orig" 2>/dev/null || true
sudo install -m 755 out/libselinux-shim.so "$T/opt/android/system/lib64/libselinux.so"
sudo install -m 755 out/libpropshim.so "$T/opt/android/libpropshim.so"
sudo install -m 644 out/build.prop "$T/opt/android/system/build.prop"

# modem_control_ch para segunda fase sensorhub (CM4)
if [ -f "$DUMP_ROOT/android/root/vendor/bin/modem_control" ] && [ -f "$DUMP_ROOT/android/modem_ch_info.xml" ]; then
    sudo mkdir -p "$T/opt/android/vendor/etc-ch"
    sudo cp -a "$DUMP_ROOT/android/modem_ch_info.xml" "$T/opt/android/vendor/etc-ch/modem_ch_info.xml"
    python3 - <<PY | sudo tee "$T/opt/android/vendor/bin/modem_control_ch" >/dev/null
import sys
d=bytearray(open('$DUMP_ROOT/android/root/vendor/bin/modem_control','rb').read())
assert d[0xcf4c:0xcf50].hex()=="68000035" and d[0xcf50:0xcf54].hex()=="e0078052"
d[0xcf4c:0xcf50]=bytes.fromhex("1f2003d5"); d[0xcf50:0xcf54]=bytes.fromhex("00048052")
sys.stdout.buffer.write(d)
PY
    sudo chmod 755 "$T/opt/android/vendor/bin/modem_control_ch"
fi
sudo chmod -R a+rX "$T/opt/android"
sudo chmod 755 "$T/opt/android/apex/com.android.runtime/bin/linker64" \
    "$T/opt/android/system/bin/"* "$T/opt/android/vendor/bin/"* "$T/opt/android/vendor/bin/hw/"* 2>/dev/null || true
sudo rm -rf "$T/system" "$T/vendor" "$T/apex"
sudo ln -s /opt/android/system "$T/system"
sudo ln -s /opt/android/vendor "$T/vendor"
sudo ln -s /opt/android/apex "$T/apex"
sudo ln -sf /apex/com.android.runtime/bin/linker64 "$T/opt/android/system/bin/linker64"

echo "5/8 libhybris (glibc + GLVND + 120 Hz + bionic parcheada + libtlspad) y SDL2-hybris ..."
sudo mkdir -p "$T/opt/hybris/alib" "$T/opt/hybris/lib" "$T/opt/hybris-glvnd/lib" "$T/opt/hybris-glvnd/aux/libhybris"
sudo install -m 644 out/bionic-patched/libc.so "$T/opt/hybris/alib/libc.so"
sudo ln -sf /system/lib64/ld-android.so "$T/opt/hybris/alib/ld-android.so"
sudo ln -sf /system/lib64/libdl.so "$T/opt/hybris/alib/libdl.so"
sudo tar -C out/hybris-root/usr/lib -cf - --exclude='*.la' --exclude='pkgconfig' --exclude='libOpenCL*' \
    --exclude='libhardware.so' --exclude='libsync.so' --exclude='libui.so' --exclude='libgralloc.so' . | \
    sudo tar --no-overwrite-dir -C "$T/opt/hybris/lib" -xf -
sudo tar -C out/hybris-glvnd-root/usr/lib -cf - --exclude='*.la' --exclude='pkgconfig' --exclude='libOpenCL*' \
    --exclude='libEGL.so*' --exclude='libGLESv2.so*' \
    --exclude='libhardware.so' --exclude='libsync.so' --exclude='libui.so' --exclude='libgralloc.so' \
    --exclude='libmedia.so' --exclude='libcamera.so' . | \
    sudo tar --no-overwrite-dir -C "$T/opt/hybris-glvnd/aux/libhybris" -xf -
sudo install -m 755 out/hybris-build/egl/platforms/drm/.libs/eglplatform_drm.so "$T/opt/hybris/lib/libhybris/eglplatform_drm.so"
sudo install -m 755 out/hybris-build/egl/platforms/drm/.libs/eglplatform_drm.so "$T/opt/hybris-glvnd/aux/libhybris/eglplatform_drm.so"
sudo install -m 755 out/hybris-build/egl/platforms/drm/.libs/eglplatform_drm.so "$T/opt/hybris-glvnd/lib/eglplatform_drm.so"
sudo cp -a "$T/opt/hybris-glvnd/aux/libhybris/libEGL_libhybris.so"* \
    "$T/opt/hybris-glvnd/aux/libhybris/libGLESv2_libhybris.so"* \
    "$T/opt/hybris-glvnd/aux/libhybris/libhybris-common.so"* \
    "$T/opt/hybris-glvnd/aux/libhybris/libhybris-eglplatformcommon.so"* \
    "$T/opt/hybris-glvnd/aux/libhybris/libhybris-platformcommon.so"* \
    "$T/opt/hybris-glvnd/aux/libhybris/libgralloc.so"* \
    "$T/opt/hybris-glvnd/aux/libhybris/libhardware.so"* \
    "$T/opt/hybris-glvnd/aux/libhybris/libsync.so"* \
    "$T/opt/hybris-glvnd/aux/libhybris/libui.so"* "$T/opt/hybris-glvnd/lib/"
sudo install -m 755 out/libtlspad.so "$T/opt/hybris-glvnd/lib/libtlspad.so"
sudo install -m 755 out/rgslide-python3 "$T/usr/local/bin/rgslide-python3"
sudo mkdir -p "$T/usr/local/lib/rgslide-pybin"
sudo ln -sf /usr/local/bin/rgslide-python3 "$T/usr/local/lib/rgslide-pybin/python3"
sudo mkdir -p "$T/usr/share/glvnd/egl_vendor.d" "$T/etc/ld.so.conf.d" "$T/etc/profile.d" "$T/etc/environment.d"
sudo cp -a "$T/opt/hybris-glvnd/etc/10_libhybris.json" "$T/usr/share/glvnd/egl_vendor.d/"
sudo cp -a "$T/opt/hybris-glvnd/etc/rgslide-hybris.conf" "$T/etc/ld.so.conf.d/"
sudo cp -a "$T/opt/hybris-glvnd/etc/rgslide-hybris.sh" "$T/etc/profile.d/"
sudo cp -a "$T/opt/hybris-glvnd/etc/60-rgslide-hybris.conf" "$T/etc/environment.d/"
sudo mkdir -p "$T/usr/local/lib/aarch64-linux-gnu"
sudo cp -a out/sdl2-hybris-root/usr/local/sdl2-hybris/lib/. "$T/usr/local/lib/aarch64-linux-gnu/"
if [ -f out/panvk-dist/libvulkan_panfrost.so ]; then
    sudo install -m 755 out/panvk-dist/libvulkan_panfrost.so "$T/usr/local/lib/aarch64-linux-gnu/libvulkan_panfrost.so"
fi

echo "6/8 ES-DE 3.5.0 y RetroArch 1.22.2 + nucleos + XMB ..."
sudo cp -a out/esde-root/opt/es-de "$T/opt/"
if [ -d out/retroarch-stage/opt/retroarch ]; then
    sudo cp -a out/retroarch-stage/opt/retroarch "$T/opt/"
    [ -f out/retroarch-root/opt/retroarch/bin/retroarch ] && \
        sudo install -m 755 out/retroarch-root/opt/retroarch/bin/retroarch "$T/opt/retroarch/bin/retroarch"
    [ -d out/extra-cores ] && \
        sudo cp -a out/extra-cores/*.so "$T/opt/retroarch/lib/libretro/" 2>/dev/null || true
    [ -d out/extra-cores-info ] && \
        sudo cp -a out/extra-cores-info/*.info "$T/opt/retroarch/info/" 2>/dev/null || true
    [ -d out/ra-extra-libs/usr/lib/aarch64-linux-gnu ] && \
        sudo cp -a out/ra-extra-libs/usr/lib/aarch64-linux-gnu/. "$T/opt/retroarch/lib/"
    if [ -d out/ra-assets-x/usr/share/libretro/assets ]; then
        sudo mkdir -p "$T/opt/retroarch/share/assets"
        sudo cp -a out/ra-assets-x/usr/share/libretro/assets/xmb \
            out/ra-assets-x/usr/share/libretro/assets/menu_widgets \
            out/ra-assets-x/usr/share/libretro/assets/branding \
            out/ra-assets-x/usr/share/libretro/assets/wallpapers \
            out/ra-assets-x/usr/share/libretro/assets/pkg "$T/opt/retroarch/share/assets/"
        for d in "$T/opt/retroarch/share/assets/xmb"/*; do
            [ -d "$d" ] && sudo ln -sf /usr/share/fonts/truetype/noto/NotoSans-Regular.ttf "$d/font.ttf" || true
        done
    fi
    sudo ln -sfn /opt/retroarch/info "$T/opt/retroarch/share/info"
    sudo chmod -R a+rwX "$T/opt/retroarch/saves" "$T/opt/retroarch/states" "$T/opt/retroarch/system" "$T/opt/retroarch/config" "$T/opt/retroarch/info"
fi

echo "7/8 Configuracion de usuario console (ES-DE, Ajustes, Wi-Fi base) y servicios systemd ..."
HDIR="$T/home/console"
sudo mkdir -p "$HDIR/ROMs/settings" "$HDIR/ROMs/ports" "$HDIR/ES-DE/custom_systems" "$HDIR/ES-DE/gamelists/nes" "$HDIR/ES-DE/gamelists/nds" "$HDIR/.config/retroarch"
sudo ln -sfn /opt/retroarch/share/assets "$HDIR/.config/retroarch/assets"
sudo cp -a rootfs/overlay/usr/share/rgslide/settings/*.sh "$HDIR/ROMs/settings/"
sudo chmod 755 "$HDIR/ROMs/settings/"*.sh
sudo cp -a rootfs/overlay/usr/share/rgslide/ports/PortMaster.sh "$HDIR/ROMs/ports/PortMaster.sh"
sudo chmod 755 "$HDIR/ROMs/ports/PortMaster.sh"
sudo ln -sfn /home/console/ROMs "$HDIR/roms"
sudo ln -sfn /home/console/ROMs "$T/roms"
sudo tee "$HDIR/ES-DE/custom_systems/es_find_rules.xml" >/dev/null <<'XML'
<?xml version="1.0"?>
<ruleList>
    <emulator name="RETROARCH">
        <rule type="staticpath">
            <entry>/usr/local/bin/rgslide-esde-retroarch</entry>
            <entry>/usr/local/bin/rgslide-retroarch</entry>
        </rule>
        <rule type="systempath">
            <entry>retroarch</entry>
        </rule>
    </emulator>
    <core name="RETROARCH">
        <rule type="corepath">
            <entry>/opt/retroarch/lib/libretro</entry>
            <entry>/usr/lib/aarch64-linux-gnu/libretro</entry>
            <entry>~/.config/retroarch/cores</entry>
        </rule>
    </core>
</ruleList>
XML
sudo tee "$HDIR/ES-DE/custom_systems/es_systems.xml" >/dev/null <<'XML'
<?xml version="1.0"?>
<systemList>
    <system>
        <name>settings</name>
        <fullname>Ajustes RG Slide</fullname>
        <path>%ROMPATH%/settings</path>
        <extension>.sh .SH</extension>
        <command>/bin/sh %ROM%</command>
        <platform>pc</platform>
        <theme>tools</theme>
    </system>
    <system>
        <name>ports</name>
        <fullname>Ports</fullname>
        <path>%ROMPATH%/ports</path>
        <extension>.sh .SH</extension>
        <command>/usr/local/bin/rgslide-port-launch %ROM%</command>
        <platform>pc</platform>
        <theme>ports</theme>
    </system>
</systemList>
XML
sudo chown -R 1000:1000 "$HDIR/ROMs" "$HDIR/ES-DE"
sudo mkdir -p "$T/etc/wpa_supplicant"
sudo tee "$T/etc/wpa_supplicant/wpa_supplicant-nl80211-wlan0.conf" >/dev/null <<'EOF'
ctrl_interface=DIR=/run/wpa_supplicant GROUP=sudo
update_config=1
EOF
sudo chmod 600 "$T/etc/wpa_supplicant/wpa_supplicant-nl80211-wlan0.conf"

# Habilitar servicios sin chroot (enlaces wants)
sudo mkdir -p "$T/etc/systemd/system/sysinit.target.wants" "$T/etc/systemd/system/multi-user.target.wants" "$T/etc/systemd/system/timers.target.wants"
for s in rgslide-dev.service rgslide-clock.service; do
    sudo ln -sf "/etc/systemd/system/$s" "$T/etc/systemd/system/sysinit.target.wants/$s"
done
for s in rgslide-mdev.service rgslide-cm4.service rgslide-ch.service rgslide-gpu.service rgslide-android.service \
         rgslide-input.service rgslide-audio.service rgslide-audiod.service rgslide-wifi.service \
         rgslide-wpa.service rgslide-bt.service rgslide-hotkeyd.service rgslide-fan.service rgslide-brightness.service \
         rgslide-fgu.service rgslide-sleep.service rgslide-imu.service rgslide-dp.service \
         rgslide-resize.service rgslide-esde.service; do
    sudo ln -sf "/etc/systemd/system/$s" "$T/etc/systemd/system/multi-user.target.wants/$s"
done
sudo ln -sf "/etc/systemd/system/rgslide-clock-save.timer" "$T/etc/systemd/system/timers.target.wants/rgslide-clock-save.timer"
sudo rm -f "$T/etc/systemd/system/multi-user.target.wants/rgslide-es.service"

echo "8/8 Empaquetando $IMG ($SIZE) ..."
rm -f "$IMG"
truncate -s "$SIZE" "$IMG"
sudo mkfs.ext4 -q -F -L rgslide-root -d "$T" "$IMG"
sudo chown "$(id -u):$(id -g)" "$IMG"
ls -lh "$IMG"
echo "Imagen completa lista: $IMG"
