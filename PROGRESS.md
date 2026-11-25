# PROGRESS

## Ahora
- RIESGO: reinicio en frio puede dar bucle de ~40 s (panic de probe en arranque temprano, singleadcjoy/unisoc_mailbox; ver docs/quirks.md INCIDENTE). No reiniciar sin necesidad. GPU+RetroArch OK; autoarranque gpu/android OK; falta audio.

## Siguiente
- /dev/sctl_ch da ENODEV en bucle (mayor dinamico 498); CM4 Version no visto
- Reflashear rootfs corregido por BROM (el overlay solo esta en la Slide por ssh)
- Sin probar por persona: pantalla/giro/entrada en Debian; GSI: ver docs/quirks.md
- NO rmmod sprd_drm (panic); cargar sprd-vpu-pw-domain antes que sprd-drm
- RTC vuelve a abril 2026 en cada arranque (apt falla firma): hora desde el PC por ssh o NTP
- Regla: ningun binario glibc que llame a GL por hybris puede tener PT_TLS (-DDT_NO_POOL en RetroArch)

## No olvidar
- Subagentes no commitean. Dump/out no se versionan.
