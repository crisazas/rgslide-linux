# PROGRESS

## Ahora
- Pantalla CONFIRMADA (el usuario ve el cubo de kmscube, via llvmpipe). Siguiente: GPU por hardware (driver de usuario Mali para kbase), mknod y carga de modulos en el arranque.

## Siguiente
- Arreglar /dev (sin devtmpfs: /dev/null 660, dbus/resolved/wpa_supplicant fallan, systemd degraded)
- /dev/sctl_ch da ENODEV en bucle (mayor dinamico 498); CM4 Version no visto
- Reflashear rootfs corregido por BROM (el overlay solo esta en la Slide por ssh)
- Sin probar por persona: pantalla/giro/entrada en Debian; GSI: ver docs/quirks.md
- NO rmmod sprd_drm (panic); cargar sprd-vpu-pw-domain antes que sprd-drm
- Probar panvk-kbase (perfil g57-v9-jm) en glibc: compilar Mesa con sus parches y native file; verificar UAPI de kbase 5.4 y vulkaninfo

## No olvidar
- Subagentes no commitean. Dump/out no se versionan.
