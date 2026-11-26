# PROGRESS

## Ahora
- Audio validado tras arranque en frio: tono 515 vs ~1 de reposo, mute/desmute OK, 0 panics. Causa del ruido del MIC = ventilador (curva nueva, docs 5u). AMP_MUTE=1, Inter PA=303, vol 100%=DG20. Falta solo el oido del usuario: estatica del altavoz, voz por mic. El overlay sigue sin reflashear (solo en la Slide por ssh)

## Siguiente
- Reflashear rootfs corregido por BROM (el overlay solo esta en la Slide por ssh)
- Sin probar por persona: pantalla/giro/entrada en Debian; GSI: ver docs/quirks.md
- NO rmmod sprd_drm (panic); cargar sprd-vpu-pw-domain antes que sprd-drm
- RTC vuelve a abril 2026 en cada arranque (apt falla firma): hora desde el PC por ssh o NTP
- Regla: ningun binario glibc que llame a GL por hybris puede tener PT_TLS (-DDT_NO_POOL en RetroArch)
- PENDIENTE: decodificador de video por hardware (vpu.ko/jpg.ko, ioctls propios de Unisoc, ver docs/video-decoder.md). No afecta a la emulacion (medido: ningun nucleo ni retroarch enlaza libav*); solo previsualizaciones de ES. No cargar vpu/jpg sin arranque estable.
- Habilitar rgslide-ch y rgslide-bt y probarlos en frio (hecho: instalados sin habilitar; ver docs/agent-sensors.md y docs/agent-bt.md)
- Audio en juegos: RetroArch usa alsa por defecto y el PCM solo avanza con NO_PERIOD_WAKEUP y buffer<=96000 B; hace falta plugin ALSA o driver propio (no probado, riesgo de panic). ES tiene audio null

## No olvidar
- Subagentes no commitean. Dump/out no se versionan.
