# PROGRESS

## Ahora
- Camino completo Mali->ventana->panel verificado y visto (fakewin -d, ~160 fps de produccion, copia CPU 4 ms). Siguiente: emulador real sobre esto (hace falta un EGLNativeWindow real: libhybris o puente), sync con vblank, sin copia, entrada, arranque automatico.

## Siguiente
- Arreglar /dev (sin devtmpfs: /dev/null 660, dbus/resolved/wpa_supplicant fallan, systemd degraded)
- /dev/sctl_ch da ENODEV en bucle (mayor dinamico 498); CM4 Version no visto
- Reflashear rootfs corregido por BROM (el overlay solo esta en la Slide por ssh)
- Sin probar por persona: pantalla/giro/entrada en Debian; GSI: ver docs/quirks.md
- NO rmmod sprd_drm (panic); cargar sprd-vpu-pw-domain antes que sprd-drm

## No olvidar
- Subagentes no commitean. Dump/out no se versionan.
