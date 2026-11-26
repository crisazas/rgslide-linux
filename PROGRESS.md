# PROGRESS

## Ahora
- Brillo hecho (rgslide-brightness). Sensores en frio OK (rgslide-sensorhub). Tema ES carbon instalado (aspecto sin ver). Subagentes en marcha: investigacion audio/mic, menu de control rgslide-ctl en ES. Panic apsys_dvfs: 1 arranque sin panic con el serializado, no concluyente. BT/mic/audio pendientes. Al terminar TODO: llamar al usuario (agentring)

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
