# PROGRESS

## Ahora
- Opcion 1 COMPLETA en render: hybristest 59 fps y RetroArch (menu + gambatte) con GPU Mali por hybris, sin SEGV. Siguiente: audio (alsa/DSP), autoconfig gamepad, servicios al arranque (android-services/gpu-prep), prueba con ROM real del usuario.

## Siguiente
- /dev/sctl_ch da ENODEV en bucle (mayor dinamico 498); CM4 Version no visto
- Reflashear rootfs corregido por BROM (el overlay solo esta en la Slide por ssh)
- Sin probar por persona: pantalla/giro/entrada en Debian; GSI: ver docs/quirks.md
- NO rmmod sprd_drm (panic); cargar sprd-vpu-pw-domain antes que sprd-drm
- RTC vuelve a abril 2026 en cada arranque (apt falla firma): hora desde el PC por ssh o NTP
- Regla: ningun binario glibc que llame a GL por hybris puede tener PT_TLS (-DDT_NO_POOL en RetroArch)

## No olvidar
- Subagentes no commitean. Dump/out no se versionan.
