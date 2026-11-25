# PROGRESS

## Ahora
- Ventanas: fakewin da 30/30 fotogramas correctos (reparto de buffers resuelto; el blob manda fence en cada queue). Siguiente: presentar los buffers a DRM (sync_wait del fence + drmshow/ADDFB2 del dma-buf) y luego un emulador real.

## Siguiente
- Arreglar /dev (sin devtmpfs: /dev/null 660, dbus/resolved/wpa_supplicant fallan, systemd degraded)
- /dev/sctl_ch da ENODEV en bucle (mayor dinamico 498); CM4 Version no visto
- Reflashear rootfs corregido por BROM (el overlay solo esta en la Slide por ssh)
- Sin probar por persona: pantalla/giro/entrada en Debian; GSI: ver docs/quirks.md
- NO rmmod sprd_drm (panic); cargar sprd-vpu-pw-domain antes que sprd-drm

## No olvidar
- Subagentes no commitean. Dump/out no se versionan.
