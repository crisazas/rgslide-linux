#!/bin/sh
# Extrae de las imagenes erofs de GammaOS (dump/pac/ex/lp/{system,vendor}_a.img) el cierre de dependencias NEEDED de libGLES_mali.so y los
# binarios/ficheros de servicio de gralloc (servicemanager, hwservicemanager, vndservicemanager, allocator@4.0-service, .rc, VINTF, gralloc.xml)
# hacia dump/android/root/ (misma disposicion que scripts/extract-android-bits.sh). Idempotente. Solo PC. Registro en dump/android/mali-deps.txt.
set -eu
cd "$(dirname "$0")/.."
SYS=${SYS_MNT:-/tmp/opencode/sys1}; VEN=${VEN_MNT:-/tmp/opencode/vend1}
IMG=dump/pac/ex/lp; D=dump/android; ROOT=$D/root; APX=${APX_DIR:-/tmp/opencode/apx}
mkdir -p "$SYS" "$VEN" "$APX" "$ROOT"
mounted=""
mnt() { mountpoint -q "$2" || { sudo -n mount -t erofs -o loop,ro "$1" "$2"; mounted="$mounted $2"; }; }
trap 'for m in $mounted; do sudo -n umount "$m"; done' EXIT
mnt $IMG/system_a.img "$SYS"; mnt $IMG/vendor_a.img "$VEN"

# apex que alojan libs (runtime = bionic; vndk = libs de vendor; art/i18n por si acaso): se sacan a $APX/<nombre>
for a in com.android.runtime.apex com.android.vndk.current.apex com.android.art.capex com.android.i18n.apex; do
  n=${a%.*}; [ -d "$APX/$n/lib64" ] && continue
  rm -rf "$APX/$n"; mkdir -p "$APX/$n"; f=$SYS/system/apex/$a
  case $a in *.capex) unzip -oq "$f" original_apex -d "$APX/$n.tmp"; f=$APX/$n.tmp/original_apex ;; esac
  unzip -oq "$f" apex_payload.img -d "$APX/$n.tmp"
  debugfs -R "rdump / $APX/$n" "$APX/$n.tmp/apex_payload.img" >/dev/null 2>&1 || true   # los chown fallan sin root: es normal
  rm -rf "$APX/$n.tmp"; [ -d "$APX/$n/lib64" ] || { echo "apex $a no extraido"; exit 1; }
done

export SYS VEN APX ROOT D
python3 - <<'PY'
import os, subprocess, shutil, sys
SYS, VEN, APX, ROOT, D = (os.environ[k] for k in ("SYS", "VEN", "APX", "ROOT", "D"))
# (directorio de origen real, ruta original en el equipo = destino relativo a ROOT)
SEARCH = [
 (f"{SYS}/system/lib64", "system/lib64"),
 (f"{APX}/com.android.runtime/lib64/bionic", "apex/com.android.runtime/lib64/bionic"),
 (f"{APX}/com.android.runtime/lib64", "apex/com.android.runtime/lib64"),
 (f"{VEN}/lib64", "vendor/lib64"),
 (f"{VEN}/lib64/hw", "vendor/lib64/hw"),
 (f"{APX}/com.android.vndk.current/lib64", "apex/com.android.vndk.current/lib64"),
 (f"{APX}/com.android.art/lib64", "apex/com.android.art/lib64"),
 (f"{APX}/com.android.i18n/lib64", "apex/com.android.i18n/lib64"),
]
log, missing, bad = [], [], []
def sh(*a, **k): return subprocess.run(a, capture_output=True, text=True, **k)
def read(p):
    return subprocess.run(["sudo", "-n", "cat", p], capture_output=True).stdout
def copy(src, rel):
    dst = os.path.join(ROOT, rel); os.makedirs(os.path.dirname(dst), exist_ok=True)
    data = read(src)
    if not data: bad.append(rel); return None
    log.append(f"{rel}\t<- {src}")
    if os.path.exists(dst) and open(dst, "rb").read() == data: return dst
    open(dst, "wb").write(data); os.chmod(dst, 0o755)
    return dst
def needed(p):
    out = sh("aarch64-linux-gnu-readelf", "-d", p).stdout
    return [l.split("[")[1].rstrip("]") for l in out.splitlines() if "(NEEDED)" in l]
def find(n):
    for d, rel in SEARCH:
        p = os.path.join(d, n)
        if os.path.lexists(p):
            if os.path.islink(p):  # system/lib64/libc.so -> /apex/com.android.runtime/lib64/bionic/libc.so: se toma del apex extraido
                t = os.readlink(p)
                if t.startswith("/apex/"):
                    parts = t.split("/"); name = parts[2]
                    name = "com.android.runtime" if name.startswith("com.android.runtime") else name
                    p = os.path.join(APX, name, *parts[3:]); rel2 = os.path.dirname(t.lstrip("/"))
                    if os.path.exists(p): return p, rel2
                    continue
            return p, rel
    return None, None
def closure(roots):
    seen, todo = set(), list(roots)
    while todo:
        f = todo.pop()
        if f in seen or not os.path.exists(f): continue
        seen.add(f)
        for n in needed(f):
            p, rel = find(n)
            if not p: 
                if n not in missing: missing.append(n)
                continue
            dst = copy(p, os.path.join(rel, n))
            if dst and dst not in seen: todo.append(dst)
    return seen
sysbin = [(f"{SYS}/system/bin/{n}", f"system/bin/{n}") for n in ("servicemanager", "hwservicemanager")]
venbin = [(f"{VEN}/bin/{n}", f"vendor/bin/{n}") for n in ("vndservicemanager", "hw/android.hardware.graphics.allocator@4.0-service")]
extra_libs = [(f"{VEN}/lib64/hw/{n}", f"vendor/lib64/hw/{n}") for n in
              ("android.hardware.graphics.mapper@4.0-impl-arm.so", "android.hardware.graphics.allocator@4.0-impl-arm.so", "gralloc.default.so")]
roots = [os.path.join(D, "mali", "libGLES_mali.so"), os.path.join(D, "mali", "gralloc.default.so"), os.path.join(D, "mali", "vulkan.ums9620.so")]
for s, rel in sysbin + venbin + extra_libs:
    if not os.path.exists(s): missing.append(rel); continue
    d = copy(s, rel)
    if d: roots.append(d)
closure(roots)
# ficheros de configuracion/servicio (no ELF)
cfg = [(f"{VEN}/etc/init/{n}", f"vendor/etc/init/{n}") for n in ("android.hardware.graphics.allocator@4.0-service.rc", "vndservicemanager.rc")]
cfg += [(f"{SYS}/system/etc/init/{n}", f"system/etc/init/{n}") for n in ("servicemanager.rc", "hwservicemanager.rc")]
cfg += [(f"{VEN}/etc/gralloc/gralloc.xml", "vendor/etc/gralloc/gralloc.xml"), (f"{VEN}/etc/mali_platform.config", "vendor/etc/mali_platform.config"),
        (f"{VEN}/etc/vintf/manifest.xml", "vendor/etc/vintf/manifest.xml"), (f"{VEN}/etc/vintf/compatibility_matrix.xml", "vendor/etc/vintf/compatibility_matrix.xml"),
        (f"{SYS}/system/etc/vintf/manifest.xml", "system/etc/vintf/manifest.xml")]
for d, rel in ((f"{VEN}/etc/vintf/manifest", "vendor/etc/vintf/manifest"), (f"{SYS}/system/etc/vintf/manifest", "system/etc/vintf/manifest")):
    out = sh("sudo", "-n", "ls", d).stdout.split()
    cfg += [(f"{d}/{n}", f"{rel}/{n}") for n in out]
for s, rel in cfg:
    if sh("sudo", "-n", "test", "-f", s).returncode: missing.append(rel + " (no existe en la imagen)"); continue
    dst = os.path.join(ROOT, rel); os.makedirs(os.path.dirname(dst), exist_ok=True)
    data = read(s)
    if not data: bad.append(rel); continue
    log.append(f"{rel}\t<- {s}")
    if not (os.path.exists(dst) and open(dst, "rb").read() == data): open(dst, "wb").write(data)
# verificacion: todo lo que no es config debe ser ELF
nonelf = []
for dp, _, fs in os.walk(ROOT):
    for f in fs:
        p = os.path.join(dp, f)
        if p.endswith((".xml", ".rc", ".config")): continue
        if open(p, "rb").read(4) != b"\x7fELF": nonelf.append(p)
with open(os.path.join(D, "mali-deps.txt"), "w") as fh:
    fh.write("# rutas del cierre/servicios de la ultima ejecucion (destino <- origen)\n" + "\n".join(sorted(set(log))) + "\n")
print(f"ficheros registrados: {len(log)}")
print("sin resolver:", missing or "ninguna"); print("vacios/ilegibles:", bad or "ninguno"); print("NO-ELF:", nonelf or "ninguno")
PY
find "$ROOT" -type f | wc -l
