# PROGRESS

## Ahora
- HITO 2 HECHO: imagen renderizada por la Mali visible en el panel (via ppm + drmshow, copia por CPU). Siguiente: render en bucle presentando sin ppm, medir rendimiento Mali vs llvmpipe, y ventana (binder/gralloc) para emuladores.

## Siguiente
- Arreglar /dev (sin devtmpfs: /dev/null 660, dbus/resolved/wpa_supplicant fallan, systemd degraded)
- /dev/sctl_ch da ENODEV en bucle (mayor dinamico 498); CM4 Version no visto
- Reflashear rootfs corregido por BROM (el overlay solo esta en la Slide por ssh)
- Sin probar por persona: pantalla/giro/entrada en Debian; GSI: ver docs/quirks.md
- NO rmmod sprd_drm (panic); cargar sprd-vpu-pw-domain antes que sprd-drm

## No olvidar
- Subagentes no commitean. Dump/out no se versionan.
