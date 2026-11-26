# Abrir Ajustes desde un juego: `rgslide-hotkeyd` + `rgslide-es-launch` (2026-11-26)

## Diseño elegido
- **Combo: Back (boton 20, KEY_BACK 158) + Select (boton 10, BTN_SELECT 314) mantenidos 300 ms.** Se cambia en `/etc/rgslide/hotkey.conf` (`COMBO`, `HOLD_MS`). Back+Start sigue siendo "salir a ES" y Back solo, el menu de RetroArch; Back+Select no esta asignado en `RG_Slide.cfg` (el demonio avisa en el journal si un boton del combo coincide con otro atajo `input_*_btn`).
- `rgslide-hotkeyd` (python3 stdlib, usuario `console` + grupo `input`, `Restart=on-failure`) **solo lee** el evdev `retrogame_joypad` (sin `EVIOCGRAB`, RetroArch lo sigue viendo). Al detectar el combo escribe `/run/rgslide/settings-request`, y solo si `/run/rgslide/session` dice `game` (lo escribe el supervisor; en panel/ES no hace nada). Una pulsacion mantenida dispara una sola vez.
- `rgslide-es-launch` ahora hace `exec python3 /usr/local/lib/rgslide/hotkey-launch.py NUCLEO ROM` (si falta python3 o el script, cae al `exec rgslide-retroarch` de siempre; `RGSLIDE_NO_HOTKEY=1` lo fuerza). Copia anterior: `/usr/local/bin/rgslide-es-launch.pre-hotkey`.
- Supervisor (`hotkey-launch.py`) ante la peticion: (1) `SHOW_MSG` + `SAVE_STATE_SLOT 99` por la **tuberia stdin** de RetroArch; (2) espera a que conteste `SAVE_STATE_SLOT 99` **y** el `.state99` tenga mtime nuevo y tamaño >0; (3) SIGTERM a RetroArch (SIGKILL a los 8 s); (4) abre `rgslide-ui panel.menu` con la pantalla libre; (5) relanza el mismo nucleo+ROM con `--entryslot=99` (RetroArch carga `<rom>.state99`); (6) al terminar el juego (20+Start o fallo) borra `*.state99`. Si el guardado falla o no contesta en 8 s: **el juego sigue**, vibra 3 veces y no se cierra nada. Cualquier excepcion: el wrapper devuelve el control y ES recupera la pantalla.
- Emulador que no es RetroArch: `hotkey-launch.py -- CMD...` (no cableado en `es_systems.cfg`: todos los sistemas actuales son RetroArch). Primer combo = vibracion + aviso en el log; repetir en 4 s = cierra (SIGTERM/SIGKILL), abre el panel y devuelve a ES; sin repetir no pasa nada.

## Por qué stdin y no UDP 55355
La build de RetroArch de la Slide tiene `HAVE_NETWORKING=0` y `HAVE_NETWORK_CMD=0` (`--disable-networking` en `scripts/build-retroarch.sh`): `network_cmd_enable` no existe. Si hay `HAVE_STDIN_CMD=1` (confirmado: `stdin_cmd_enable` en el binario y funciona). La tuberia tiene que ser **no bloqueante** (`os.set_blocking(r, False)`): RetroArch lee stdin con `read()` en el hilo principal. Por eso `stdin_cmd_enable=true` va en `/etc/retroarch-hotkey.cfg` (solo lo anade el supervisor) y no en `retroarch-es.cfg` ni `retroarch.cfg`. Con `--entryslot` el estado entra sin tocar el slot 0 del usuario; slot 99 dedicado, borrado al salir; los `.state`/`.state1..` del usuario no se tocan.

## Alternativas descartadas
- SIGSTOP al emulador + `rgslide-ui` encima: la plataforma drm de libhybris (`patches/libhybris/0002-egl-plataforma-drm.patch`, `DrmNativeWindow::openDrm`) abre `/dev/dri/card0` y llama `DRM_IOCTL_SET_MASTER` sin comprobar el resultado ni soltarlo (no hay `DROP_MASTER` en ningun parche). Un segundo master falla y el proceso parado lo conserva: **descartado sin probar a ciegas**.
- ROCKNIX/JELOS/Batocera (`batocera-hotkeys`/`hotkeygen`): un demonio mapea combos a acciones y el frontend (RetroArch) recibe `SAVE_STATE`/`QUIT` por comandos de red; mismo patron que aqui, pero ellos tienen compositor/DRM compartible o mueren el emulador sin guardar. muOS (`muhotkey`) tambien lee evdev aparte y lanza comandos. RetroPie `runcommand` envuelve el lanzamiento igual que nuestro wrapper (aqui, ademas, con estado).
- No se toco `retroarch.cfg`, ni `RG_Slide.cfg`, ni el mando.

## Pruebas
PC (cuenta de pruebas, RetroArch falso y `rgslide-ui` falso): ciclo guardar->TERM->panel->relanzar con `--entryslot=99` dos veces seguidas; guardado sin respuesta (el juego sigue, sesion `game`); RetroArch que sale solo (rc propagado, sesion `none`); emulador generico (1er combo avisa y sigue, 2º cierra y abre el panel). Demonio con mando virtual **uinput** (`rgtest_pad`): Back solo 0,6 s = nada; Back+Select 0,1 s = nada; Back+Start = nada; Back+Select 0,6 s = una peticion (mantener no repite); con sesion `panel` = nada.

Slide (2026-11-26 01:36-01:40; ES parado ≤ 20 s por prueba, `rgslide-es` activo despues de cada una, 0 panics): 
1. Peticion escrita a mano en `/run/rgslide/settings-request` con libbet (gambatte): log de RetroArch `Saving state ... libbet.state99 (51248 bytes)` -> panel `rgslide-ui` (3 s por `timeout`) -> 2º RetroArch `Found entry state ... Loading entry state ... succeeded` -> el wrapper sigue vivo con el juego en marcha.
2. Mando virtual uinput `rgtest_pad` en la Slide (`/dev/uinput` existe) + `rgslide-hotkeyd` real con `RGSLIDE_PAD_NAME=rgtest_pad`: Back+Select 0,6 s -> misma secuencia; luego SIGTERM a RetroArch = el wrapper salio con rc 0, sesion `none`, sin `.state99`, ES recupero la pantalla.
Servicio `rgslide-hotkeyd` instalado, **enabled y activo** leyendo `event3` (el mando real).

## NO probado (lo tiene que hacer una persona con el mando real)
- Pulsar Back+Select de verdad dentro de un juego y ver el panel (el panel solo se vio abrir 3 s, no se vio la imagen), salir con B, y que el juego vuelva donde estaba. Los 0,3 s de combo con el mando real; que **Back suelto no abra tambien el menu de RetroArch** (RetroArch ve Back a la vez que el demonio: el menu se abre al soltar Back salvo que otro atajo lo "cancele"; no se midio en la Slide, puede aparecer un instante el menu de RetroArch antes del cierre).
- Que el atajo 20+Start y el lanzamiento normal desde ES siguen funcionando **con pulsacion real**: el lanzamiento desde ES (`<command>` del sistema) usa el mismo `rgslide-es-launch` y se comprobo por la via de linea de comandos con `bash -lc`; la salida por 20+Start esta intacta (no se cambio `RG_Slide.cfg`; RetroArch sale solo y el supervisor devuelve su rc).
- Juegos con nucleo que no soporte estados (mgba/desmume si lo soportan; no se probaron): sin estado el juego sigue y vibra 3 veces. El panel no tiene aun `tr()`: el texto que se muestra viene de `panel.menu`.
- La relanzada tarda ~3 s (arranque de RetroArch + carga del estado).

## Deshacer
`scripts/install-hotkey.sh undo` (para y deshabilita `rgslide-hotkeyd`, restaura `rgslide-es-launch.pre-hotkey`, borra los ficheros nuevos). Reinstalar: `scripts/install-hotkey.sh`. Para solo desactivar el supervisor sin desinstalar: `RGSLIDE_NO_HOTKEY=1` en el entorno de ES, o `sudo systemctl disable --now rgslide-hotkeyd`.
