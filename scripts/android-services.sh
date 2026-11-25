#!/bin/sh
# Arranca en la Slide (como root) los servicios minimos de Android para gralloc: servicemanager, hwservicemanager,
# vndservicemanager y allocator@4.0-service, bajo linker64 como uid 1000 (system). Se copia a /usr/local/sbin/android-services.
# Uso: android-services start|stop|status. Registro en /tmp/android-services/*.log
LK=/opt/android/apex/com.android.runtime/bin/linker64
PRE=/opt/android/libpropshim.so
D=/tmp/android-services
RUN="setpriv --reuid=1000 --regid=1000 --clear-groups"
arranca() {
  n=$1; shift
  if pgrep -f "^/opt/android/apex/com.android.runtime/bin/linker64 .*$n" >/dev/null 2>&1; then echo "$n ya corre"; return 0; fi
  ( cd /tmp && setsid env LD_PRELOAD=$PRE $RUN $LK "$@" >$D/$n.log 2>&1 </dev/null & )
  sleep 1
  if pgrep -f "^/opt/android/apex/com.android.runtime/bin/linker64 .*$n" >/dev/null 2>&1; then echo "$n OK"; else echo "$n MUERTO ($D/$n.log)"; fi
}
case "${1:-status}" in
start)
  mkdir -p $D /dev/socket /dev/__properties__ /data/misc /dev/hwbinder-run
  chmod 777 $D /dev/socket
  arranca servicemanager /system/bin/servicemanager
  arranca vndservicemanager /vendor/bin/vndservicemanager /dev/vndbinder
  arranca hwservicemanager /system/bin/hwservicemanager
  arranca android.hardware.graphics.allocator@4.0-service /vendor/bin/hw/android.hardware.graphics.allocator@4.0-service
  ;;
stop) pkill -f "^/opt/android/apex/com.android.runtime/bin/linker64 .*servicemanager"; pkill -f "^/opt/android/apex/com.android.runtime/bin/linker64 .*graphics.allocator"; echo parados ;;
*) pgrep -af "^/opt/android/apex/com.android.runtime/bin/linker64 " | cut -c1-120 ;;
esac
