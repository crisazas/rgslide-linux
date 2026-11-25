# PROGRESS

## Ahora
- HITO 1 HECHO: Mali-G57 renderiza GLES 3.2 offscreen en Debian con el blob de Arm (gltest). Siguiente: hito 2 = sacar el buffer al panel (drmshow, sin probar) y medir rendimiento.

## Siguiente
- Arreglar /dev (sin devtmpfs: /dev/null 660, dbus/resolved/wpa_supplicant fallan, systemd degraded)
- /dev/sctl_ch da ENODEV en bucle (mayor dinamico 498); CM4 Version no visto
- Reflashear rootfs corregido por BROM (el overlay solo esta en la Slide por ssh)
- Sin probar por persona: pantalla/giro/entrada en Debian; GSI: ver docs/quirks.md
- NO rmmod sprd_drm (panic); cargar sprd-vpu-pw-domain antes que sprd-drm

## No olvidar
- Subagentes no commitean. Dump/out no se versionan.
