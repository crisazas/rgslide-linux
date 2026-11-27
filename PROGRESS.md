# PROGRESS

## Estado Actual
- **GPU Mali G57 3D**: Operativa a 120 Hz mediante `libhybris` + wrapper vendor + `tlspad`.
- **Frontend / Emuladores**: ES-DE y RetroArch funcionando nativos en Linux.
- **Audio**: Codec UMP9620 corregido con softdeps de reguladores (`rgslide-audio.conf`). Sonido en ES-DE y RetroArch.
- **Botones y Volumen**: Gamepad integrado funcional; botones físicos de volumen enlazados con `rgslide-hotkeyd`.
- **Conectividad**: Wi-Fi (DHCP automático), Bluetooth (BlueZ + firmware Unisoc), USB Gadget SSH.
- **Localización**: Soporte bilingüe dinámico (`es` / `en`) para ES-DE y menú Tools.

## Pendiente
- Modo suspensión s2idle optimizado sin colgar sensorhub.
- Salida de vídeo externa por USB-C (DisplayPort alt-mode).
- Reflasheo final empaquetado del rootfs Debian.
- Exploración de kernel mainline (etapa 2).

## Reglas
- Subagentes no commitean. Dump y out permanecen en `.gitignore`.
