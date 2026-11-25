# PROGRESS

## Ahora
- Pantalla CONFIRMADA (el usuario ve el cubo de kmscube, via llvmpipe). Siguiente: GPU por hardware (driver de usuario Mali para kbase), mknod y carga de modulos en el arranque.

## Siguiente
- Arreglar /dev (sin devtmpfs: /dev/null 660, dbus/resolved/wpa_supplicant fallan, systemd degraded)
- /dev/sctl_ch da ENODEV en bucle (mayor dinamico 498); CM4 Version no visto
- Reflashear rootfs corregido por BROM (el overlay solo esta en la Slide por ssh)
- Sin probar por persona: pantalla/giro/entrada en Debian; GSI: ver docs/quirks.md
- NO rmmod sprd_drm (panic); cargar sprd-vpu-pw-domain antes que sprd-drm
- GPU: elegir entre Arm blob+libhybris (GLES 3.2+Vulkan 1.4 nativos, mucho trabajo) y panvk-kbase (solo Vulkan, UAPI por verificar); ver docs/quirks.md

## No olvidar
- Subagentes no commitean. Dump/out no se versionan.
