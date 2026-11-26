# PROGRESS

## Ahora
- Audio y mic validados con medidas: voz sintetica SNR 11-19 dB (amp alta), tono 515 vs 1, ruido mic ~14 con ventilador callado (docs 5u/5v), 0 panics en frio, overlay==dispositivo (sha256) y persiste (sda73 ext4). Falta solo oido humano: estatica del altavoz y voz real

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
