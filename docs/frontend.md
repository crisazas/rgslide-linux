# Frontend de la RG Slide: EmulationStation + RetroArch (2026-11-25)

Estado: ES (RetroPie, GLES 2.0) sobre SDL2 con driver `hybris` lanza juegos con RetroArch y vuelve. Probado por el usuario: uCity (GBC), Libbet y Adjustris (GB), Thwaite y Nova (NES) con ROMs libres (creditos en `rootfs/overlay/usr/share/rgslide/roms-libres/CREDITOS.md`). SIN sonido (DSP, ver docs/audio-wifi.md).

## Piezas
- `/opt/emulationstation/` (binario + `lib/` + `resources/`), compilado con `scripts/build-es.sh`; se lanza con `/usr/local/bin/rgslide-es` (bucle de reinicio de ES; atiende "Reiniciar ES/sistema" y "Apagar" del menu).
- `/etc/emulationstation/es_systems.cfg`: gb, gbc, gba, nes, snes, nds, virtualboy, wonderswan -> `rgslide-es-launch NUCLEO ROM` -> `rgslide-retroarch --appendconfig=/etc/retroarch-es.cfg` (audio `null`).
- ROMs en `~/roms/<sistema>/` (NO se versionan).
- Mando: `~/.emulationstation/es_input.cfg` (ES) y `/usr/share/rgslide/autoconfig/RG_Slide.cfg` (RetroArch). Deben coincidir: A=boton 0, B=1, X=3, Y=4, Start=11, Select=10, hombros 6/7, gatillos 8/9, hat 0, HotKey=20.

## Atajos dentro de RetroArch (lanzado desde ES)
| Combinacion | Accion |
|---|---|
| **Boton 20 (Back) + Start (11)** | Salir del juego y volver a EmulationStation (`input_enable_hotkey_btn=20`, `input_exit_emulator_btn=11`) |
| Boton 20 solo | Abre el menu de RetroArch (lo mismo que antes) |

Como el 20 es a la vez "hotkey" y "menu", RetroArch (`input_driver.c`, `input_driver_button_combo_hold`) admite mismo boton para ambos. SIN PROBAR por persona el atajo 20+Start (solo se comprobo que el perfil carga sin errores).

## Diferencias ES <-> nuestro RetroArch encontradas y resueltas
- A/B/X/Y: ES (lo que configuro el usuario) tenia A=0, Y=4; el `RG_Slide.cfg` del subagente de entrada los tenia al reves (rotulado del DT del fabricante). Se alineo RetroArch a ES.
- `Failed to write controller profile index to .autoconfig_index`: el directorio del autoconfig debe ser escribible por el grupo `input` (775); `scripts/install-retroarch.sh` lo deja asi.
- El menu "Reiniciar" de ES termina el proceso y deja `/tmp/es-restart`: necesita el bucle de `rgslide-es`.
- Tras volver de un juego ES reinicia `InputManager` (el joystick cambia de `instance ID`): funciona si `es_input.cfg` ya existe; el primer arranque sin ese fichero dejo el mando "unconfigured" (no reproducido en el estado antiguo).

## Pendiente
- Arranque automatico de ES al iniciar sesion (servicio) y que hacer si muere.
- Sonido; previsualizaciones de video (libvlc sin plugins); decodificador por hardware (docs/video-decoder.md).
- Probar GBA, SNES, NDS con ROMs reales y medir rendimiento.
- Atajos extra (guardar/cargar estado con 20+L1/R1): no configurados.
