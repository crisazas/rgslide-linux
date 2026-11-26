# PROGRESS

## Ahora
- Todo verificado en frio (0 panics, ES/hotkeyd/sensorhub/brillo/BT activos, wifi, 120 Hz, tema carbon, ES en ingles, atajo Select+Home validado). Audio/mic sin resolver: el HAL de Android hace la misma secuencia que nuestro start, falta el estado previo del AUDCP (docs/agent-agdsp.md). Falta llamar al usuario

## Siguiente
- Reflashear rootfs corregido por BROM (el overlay solo esta en la Slide por ssh)
- Sin probar por persona: pantalla/giro/entrada en Debian; GSI: ver docs/quirks.md
- NO rmmod sprd_drm (panic); cargar sprd-vpu-pw-domain antes que sprd-drm
- RTC vuelve a abril 2026 en cada arranque (apt falla firma): hora desde el PC por ssh o NTP
- Regla: ningun binario glibc que llame a GL por hybris puede tener PT_TLS (-DDT_NO_POOL en RetroArch)
- PENDIENTE: decodificador de video por hardware (vpu.ko/jpg.ko, ioctls propios de Unisoc, ver docs/video-decoder.md). No afecta a la emulacion (medido: ningun nucleo ni retroarch enlaza libav*); solo previsualizaciones de ES. No cargar vpu/jpg sin arranque estable.
- Habilitar rgslide-ch y rgslide-bt y probarlos en frio (hecho: instalados sin habilitar; ver docs/agent-sensors.md y docs/agent-bt.md)

## No olvidar
- Subagentes no commitean. Dump/out no se versionan.
