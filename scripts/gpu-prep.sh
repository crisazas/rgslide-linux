#!/bin/sh
# Prepara la GPU en la Slide (se copia a /usr/local/sbin/ y lo ejecuta root): carga los modulos de pantalla/GPU y crea los nodos que falten
# (sin devtmpfs, con los mayores dinamicos de /proc/devices y /sys). NUNCA hace rmmod (descargar sprd_drm provoca panic). Idempotente.
CREADO=""
AVISOS=""

nota() { CREADO="$CREADO
  $1"; }
aviso() { AVISOS="$AVISOS
  $1"; }

carga() {
  if grep -q "^$(echo "$1" | tr - _) " /proc/modules; then return 0; fi
  if modprobe "$1" 2>/dev/null; then nota "modulo $1"; else aviso "modprobe $1 fallo"; fi
}

# nodo NOMBRE SYSDEV [MODO]: crea /dev/NOMBRE de caracteres con el M:m de SYSDEV (fichero 'dev' de sysfs)
nodo() {
  [ -e "/dev/$1" ] && return 0
  if [ ! -r "$2" ]; then aviso "sin $2: no creo /dev/$1"; return 0; fi
  MM=$(cat "$2")
  mkdir -p "$(dirname "/dev/$1")"
  if mknod -m "${3:-666}" "/dev/$1" c "${MM%%:*}" "${MM##*:}"; then nota "/dev/$1 c $MM"; else aviso "mknod /dev/$1 fallo"; fi
}

# 1) modulos: sprd-vpu-pw-domain ANTES que sprd-drm (si no, dpu/dsi quedan diferidos)
for m in pwm-sprd unisoc-iommu apsys-dvfs sprd-vpu-pw-domain sprd-drm mali_kbase; do carga "$m"; done

# 2) nodos
for d in /sys/class/drm/card[0-9]* /sys/class/drm/renderD[0-9]*; do
  case "$d" in *-*) continue ;; esac
  [ -d "$d" ] && nodo "dri/$(basename "$d")" "$d/dev"
done
nodo mali0 /sys/class/misc/mali0/dev
nodo ion /sys/class/misc/ion/dev
for d in /sys/class/dma_heap/*; do
  [ -d "$d" ] && nodo "dma_heap/$(basename "$d")" "$d/dev"
done

# binder: via misc (CONFIG_ANDROID_BINDER_DEVICES) o via binderfs (el mayor 'binder' de /proc/devices lo reserva binderfs)
for n in binder hwbinder vndbinder; do
  if [ -r /sys/class/misc/$n/dev ]; then nodo $n /sys/class/misc/$n/dev; fi
done
if [ ! -e /dev/binder ]; then
  if grep -qw binder /proc/filesystems; then
    mkdir -p /dev/binderfs
    grep -q ' /dev/binderfs ' /proc/mounts || { mount -t binder binder /dev/binderfs 2>/dev/null && nota "montado binderfs en /dev/binderfs"; }
    for n in binder hwbinder vndbinder; do
      if [ -e /dev/binderfs/$n ] && [ ! -e /dev/$n ]; then ln -s /dev/binderfs/$n /dev/$n && nota "/dev/$n -> /dev/binderfs/$n"; fi
    done
    chmod 666 /dev/binderfs/* 2>/dev/null
  fi
fi
for n in binder hwbinder vndbinder; do
  [ -e /dev/$n ] || aviso "no hay /dev/$n; hallado: /proc/devices='$(awk '$2 ~ /binder/{printf "%s=%s ", $2, $1}' /proc/devices)' misc='$(ls /sys/class/misc 2>/dev/null | tr '\n' ' ')' fs='$(grep -w binder /proc/filesystems | tr '\n' ' ')'"
done

chmod 666 /dev/null /dev/zero /dev/full /dev/random /dev/urandom /dev/tty /dev/dri/* /dev/mali0 /dev/ion /dev/binder /dev/hwbinder /dev/vndbinder 2>/dev/null
chmod 666 /dev/dma_heap/* 2>/dev/null

# 3) /dev/block/by-name (igual que rgslide-cm4-prep)
mkdir -p /dev/block/by-name
for u in /sys/class/block/sd*[0-9]; do
  n=$(sed -n 's/^PARTNAME=//p' "$u/uevent"); b=$(basename "$u")
  if [ -n "$n" ] && [ ! -e "/dev/block/by-name/$n" ]; then ln -s "/dev/$b" "/dev/block/by-name/$n" && nota "by-name/$n"; fi
done

# 4) resumen
if [ -n "$CREADO" ]; then echo "creado:$CREADO"; else echo "nada que crear"; fi
if [ -n "$AVISOS" ]; then echo "avisos:$AVISOS"; fi
ls -l /dev/dri /dev/mali0 /dev/ion /dev/binder /dev/hwbinder /dev/vndbinder /dev/dma_heap 2>&1 | head -30
exit 0
