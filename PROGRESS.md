# PROGRESS

## Ahora
- Sensores: CM4 ch arranca y el acelerometro da datos (rgslide-ch.service instalado SIN habilitar). BT: hci0 UP (rgslide-bt.service SIN habilitar). Suspension enmascarada. Esperando subagente de audio; luego habilitar ch+bt y reinicio en frio con vigilante. Falta por persona: ventilador, giro, A2DP, emparejar

## Siguiente
- /dev/sctl_ch da ENODEV en bucle (mayor dinamico 498); CM4 Version no visto
- Reflashear rootfs corregido por BROM (el overlay solo esta en la Slide por ssh)
- Sin probar por persona: pantalla/giro/entrada en Debian; GSI: ver docs/quirks.md
- NO rmmod sprd_drm (panic); cargar sprd-vpu-pw-domain antes que sprd-drm
- RTC vuelve a abril 2026 en cada arranque (apt falla firma): hora desde el PC por ssh o NTP
- Regla: ningun binario glibc que llame a GL por hybris puede tener PT_TLS (-DDT_NO_POOL en RetroArch)
- PENDIENTE: decodificador de video por hardware (vpu.ko/jpg.ko, ioctls propios de Unisoc, ver docs/video-decoder.md). No afecta a la emulacion (medido: ningun nucleo ni retroarch enlaza libav*); solo previsualizaciones de ES. No cargar vpu/jpg sin arranque estable.

## No olvidar
- Subagentes no commitean. Dump/out no se versionan.
