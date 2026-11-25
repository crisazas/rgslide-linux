# PROGRESS

## Ahora
- SIN COPIA integrado y visto (fakewin -z): PAGE_FLIP+vblank, 59.4 fps = 60 Hz, 0 errores, usuario ve fluido. Siguiente: emulador real sobre esta ventana (nucleo libretro arm64 Android + frontend minimo), entrada, arranque automatico.

## Siguiente
- /dev/sctl_ch da ENODEV en bucle (mayor dinamico 498); CM4 Version no visto
- Reflashear rootfs corregido por BROM (el overlay solo esta en la Slide por ssh)
- Sin probar por persona: pantalla/giro/entrada en Debian; GSI: ver docs/quirks.md
- NO rmmod sprd_drm (panic); cargar sprd-vpu-pw-domain antes que sprd-drm
- RTC vuelve a abril 2026 en cada arranque (apt falla firma): hora desde el PC por ssh o NTP

## No olvidar
- Subagentes no commitean. Dump/out no se versionan.
