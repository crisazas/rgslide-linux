# PROGRESS

## Estado Actual
- **GPU Mali G57 3D**: Operativa a 120 Hz mediante `libhybris` + wrapper vendor + `tlspad`, y Vulkan 1.3 nativo via **Mesa PanVK 26.3** (`VK_KHR_display` + Wayland/X11 WSI).
- **Frontend / Emuladores / PortMaster**: ES-DE, RetroArch y PortMaster funcionando nativos en Linux; juegos X11/OpenGL bajo Westonpack (`rgslide-westonwrap.sh`) acelerados en GPU Mali-G57 via Mesa Zink + PanVK (`PANVK_KBASE_DRI3=0`, `LIBGL_KOPPER_DRI2=1`, OpenGL 4.2 / GLES 3.1, 395 FPS en `glxgears` sin fugas de memoria); motor AGS nativo (`rgslide-ags`) sobre `SDL2-hybris`.
- **Decodificador de video por hardware (VPU T820)**: `vpu.ko` y `jpg.ko` cargados tras GPU por `rgslide-vpu.service` (`/dev/{sprd_vsp,vpu_enc0,vpu_enc1,sprd_jpg}`), y herramienta `rgslide-vpu` (`tools/vpu/`) sobre `libhybris` + librerias 64-bit `libomx_*_hw_sprd.so` verificada bit-exacta en 1080p H.264 (75 FPS) y H.265/HEVC (160 FPS).
- **Audio**: Codec UMP9620 corregido con softdeps de reguladores (`rgslide-audio.conf`) y contrapresion FIFO a 48 kHz en `rgslide-audiod`.
- **Botones, Sensores, Tapa y Standby**: Gamepad integrado, pantalla tactil, rumble nativo, volumen fisico, standby instantaneo (`rgslide-sleepd`) y servidor DSU/Cemuhook (`rgslide-imud`) con acelerometro (handle 1) y giroscopio calibrado (handle 4, 50 Hz, deg/s).
- **Conectividad**: Wi-Fi (DHCP automático), Bluetooth (BlueZ + firmware Unisoc), USB Gadget SSH.
- **Localización**: Soporte bilingüe dinámico (`es` / `en`) para ES-DE y menú Tools.

## Pendiente
- Modo suspensión s2idle optimizado sin colgar sensorhub.
- Salida de vídeo externa por USB-C (DisplayPort alt-mode).
- Reflasheo final empaquetado del rootfs Debian.
- Exploración de kernel mainline (etapa 2).

## Reglas
- Subagentes no commitean. Dump y out permanecen en `.gitignore`.
