# PROGRESS

## Ahora
- DECIDIDO: GPU por el blob de Arm del vendor (GLES 3.2 + Vulkan 1.4). Plan pendiente de OK: hito 1 = render offscreen con el blob como proceso bionic (sin libhybris) para saber si Mali rinde; luego salida a DRM.

## Siguiente
- Arreglar /dev (sin devtmpfs: /dev/null 660, dbus/resolved/wpa_supplicant fallan, systemd degraded)
- /dev/sctl_ch da ENODEV en bucle (mayor dinamico 498); CM4 Version no visto
- Reflashear rootfs corregido por BROM (el overlay solo esta en la Slide por ssh)
- Sin probar por persona: pantalla/giro/entrada en Debian; GSI: ver docs/quirks.md
- NO rmmod sprd_drm (panic); cargar sprd-vpu-pw-domain antes que sprd-drm

## No olvidar
- Subagentes no commitean. Dump/out no se versionan.
