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

## Arranque automatico de ES (2026-11-25) - `rootfs/overlay/etc/systemd/system/rgslide-es.service`
- `rgslide-es.service` (habilitado): `User=console`, `After=rgslide-android/gpu/input/clock`, `Requires=rgslide-android`, `Restart=on-failure`, `StartLimitBurst=5` en 120 s; `ExecStart=/usr/local/bin/rgslide-es`.
- Probado en caliente: arranca como `console` con las variables de hybris y `libtlspad`; "Reiniciar ES" (`/tmp/es-restart`) lo relanza el bucle de `rgslide-es`; `kill -9` -> systemd lo relanza (NRestarts=1).
- Probado en FRIO (reinicio pedido por SSH, 15:41): el usuario VIO ES aparecer solo en la pantalla y responder al mando. PERO el PC NO volvio a ver el USB de la Slide (ni red ni `ttyUSB0`) durante ~10 min, tampoco tras reconectar el cable. Causa NO identificada: el gadget USB (RNDIS+gser) lo arma el `initramfs`, no Debian. No se pudo mirar el estado de la Slide por SSH en ese arranque.
- Reinicio desde el menu "Reiniciar sistema" de ES (`/tmp/es-sysrestart` -> `sudo -n systemctl reboot` en `rgslide-es`): funciono; tras el el USB SI volvio a enumerar ("RG Slide rescue", interfaz `enx76c82d434206`).

## IP fija del PC para la Slide (2026-11-25)
- Problema: la interfaz USB del PC (`enx<mac>`, la MAC cambia en cada arranque de la Slide) caia al perfil "Wired connection 1" de NetworkManager (DHCP, que la Slide no sirve) y perdia `10.55.0.1`.
- Solucion: `tools/udev/rgslide-usb.nmconnection` (match `driver=rndis_host` + `interface-name=enx*`, IP manual `10.55.0.1/24`, sin gateway, prioridad 100, IPv6 desactivado).
- Instalar en el PC: `sudo install -m 600 -o root -g root tools/udev/rgslide-usb.nmconnection /etc/NetworkManager/system-connections/ && sudo nmcli connection reload`.
- Verificado: perfil activo con la interfaz actual, ssh sin poner la IP a mano, la ruta por defecto sigue por wifi, y baja/sube del perfil conserva la IP. NO probado: desenchufar el cable y volver a enchufarlo / un reinicio de la Slide (MAC nueva).

## Carrera del UDC en el arranque en frio (2026-11-25)
- Causa del arranque de las 15:41 sin USB (MEDIDA en `journalctl -b -1`): `setup_gadget` de `initramfs/init` busco el UDC a los 23,067 s y no habia ninguno (`gadget: sin UDC en /sys/class/udc`); `dwc3 gadget init done!` llego 0,2 ms despues. Sin gadget nunca hay `b_sess_vld`. En el arranque bueno el UDC aparecio unos ms antes. NO era cable ni ES.
- Arreglo (commit cc5c662): `setup_gadget` reintenta hasta 10 s; deja `gadget: UDC visible tras N esperas` si tuvo que esperar. Imagen `out/boot-rootfs-udcwait.img` (kernel y cmdline iguales; con el init antiguo el script reproduce `boot-rootfs.img` byte a byte).
- Flasheado en `boot_a` (`/dev/sda36`) desde Linux, releido md5 853536e0. `boot_b` queda con la imagen anterior (19b927a7), como ultimo arranque bueno.
- Probar: `misc` con `_a`, a = prio 15 tries 1 sin successful, b = prio 14 (copia previa en `out/misc-antes-udcwait.bin`). Si A no marca exito, el LK vuelve a B. Reproducir la carrera es improbable: un arranque bueno no la demuestra arreglada; la evidencia es la linea `UDC visible tras N esperas`.
- Resultado (16:17-16:20): la prueba por `misc` NO funciono (arranco `_b` con la imagen antigua; `uboot_log` no registra ningun intento de A y el `misc` final es `_b` con A prio 14 tries 1 sin exito: no se sabe quien lo cambio). Por eso la imagen nueva se escribio tambien en `boot_b` (releida md5 853536e0; `boot_a` igual; respaldo de la antigua: `out/boot-rootfs.img`, 19b927a7).
- Con la imagen nueva en `boot_b` la Slide arranco bien: ES activo, `gadget enlazado a 25100000.dwc3`, `b_sess_vld`, 0 panics, USB y SSH OK. NO hay linea `UDC visible tras N esperas` (en este arranque el UDC ya estaba): es consistente con el init nuevo pero no lo demuestra; solo esa linea lo haria. Un arranque bueno no prueba arreglada la carrera.
- Nota: la IP 10.55.0.1 del PC se puso sola en los dos arranques (perfil NM `rgslide-usb`).

## Wifi al arranque (2026-11-25)
- `rgslide-wifi.service` habilitado (After android/gpu/input/clock). En el reinicio de las 16:23: `wcn_bsp` cargo (servicio terminado a los ~60 s), `wlan0` existe, 0 panics. Avisos de wcn en dmesg: `failed to find pmu_apb_gpr(26M)(ignore)` y `tsx cali read fail! default 0xffff` (no se investigaron).
- FALLO hallado: `wpa_supplicant-nl80211@wlan0` no arrancaba (`Dependency failed`): su `Requires=sys-subsystem-net-devices-wlan0.device` nunca llega a `active` (tampoco el de usb0), el job quedaba esperando. Solucion: drop-in `rootfs/overlay/etc/systemd/system/wpa_supplicant-nl80211@wlan0.service.d/rgslide.conf` (vacia Requires/After y depende de `rgslide-wifi.service`).
- Verificado en caliente: arrancado el servicio con el drop-in, `wlan0` conecto a "Internet" (192.168.68.116/22), DNS resuelve, la Slide responde por ssh por wifi. NO verificado: que el drop-in lo arranque solo en un arranque en frio (hace falta otro reinicio).
- El PC no ve la Slide por USB tras reiniciarse el PC (RX 0, TX carrier errors en `enx...`) mientras la Slide estaba viva: el wifi sirvio de segundo canal. Causa sin investigar.
- CORRECCION (16:44, reinicio en frio): el drop-in NO funciono. `wpa_supplicant-nl80211@wlan0` volvio a fallar con `Dependency failed` aunque el drop-in estaba cargado (`DropInPaths` lo lista), porque `systemctl show -p Requires` seguia mostrando `sys-subsystem-net-devices-wlan0.device`: la linea `Requires=` vacia no lo quito. Causa de fondo NO identificada: `sys-subsystem-net-devices-wlan0.device` esta `inactive` en el arranque y pasa a `active` solo tras cancelar el job y `daemon-reload` (o con udevadm trigger).
- Solucion aplicada: se retira el drop-in y la plantilla `wpa_supplicant-nl80211@wlan0` (disabled); nueva unidad `rootfs/overlay/etc/systemd/system/rgslide-wpa.service` (Requires/After `rgslide-wifi.service`, `wpa_supplicant -c.../wpa_supplicant-nl80211-wlan0.conf -Dnl80211 -iwlan0`, `Restart=on-failure`), habilitada. Verificada en caliente: `wlan0` 192.168.68.116/22. NO verificada en frio.
- En ese mismo arranque en frio: USB OK (`gadget enlazado`, UDC `attached`, sin linea de espera), ES activo, 0 panics.
- Verificado en FRIO (reinicio 16:48): `rgslide-wifi` y `rgslide-wpa` activos solos (wpa a los ~60 s del arranque), `wlan0` con 192.168.68.116/22, USB y SSH OK, ES activo, 0 panics, IP del PC puesta sola. Tampoco salio la linea `UDC visible tras N esperas` (3 arranques seguidos sin necesitar la espera): la carrera sigue sin reproducirse.

## ROMs libres GBA y NDS: rendimiento medido (2026-11-25)
- ROMs (creditos y licencias en `CREDITOS.md`): `gba/glacia.gba` (GPL-3.0), `gba/notebook.gba` (GPL-3.0), `nds/delve.nds` (MIT + arte CC0). Release del autor, cabeceras comprobadas (logo GBA, CRC de logo NDS).
- Medida: `rgslide-retroarch --max-frames=1200` con ES parado, audio `null`. 1200 fotogramas a 60 fps = 20 s ideales.
  | Juego | Nucleo | Tiempo real | CPU usuario |
  |---|---|---|---|
  | Glacia Dungeon | mgba | 20,6 s | 9,4 s (~46 % de un nucleo) |
  | Notebook Adventure | mgba | 20,6 s | 6,8 s (~33 %) |
  | Delve | desmume | 20,7 s | 17,7 s (~86 %) |
  Los tres a velocidad completa (el tiempo lo fija el limitador de 60 fps, no la CPU). Temperatura del SoC tras las tres: 41,8 C. 0 panics. NDS es el mas justo: 86 % de UN nucleo (hay 8). Delve es 2D con algo de 3D; un juego NDS con 3D pesado puede no ir igual. NO medido por persona: control, imagen y sonido (sin audio).
- Fallo hallado y arreglado: `[SRAM] Failed to save SRAM` en mgba. `/opt/retroarch/saves` y `states` eran `root 755` en la Slide aunque `scripts/install-retroarch.sh` ya hace `chmod -R a+rwX`; se aplico a mano (`chmod -R a+rwX`) y el error desaparece (0). Si se reinstala el rootfs sin pasar por el script, hay que repetirlo.
- N64: no hay nucleo en la Slide; los nucleos de RetroArch (mupen64plus-next) usan GL y no se ha probado el contexto GL de nuestro RetroArch con ellos; los juegos libres de libdragon piden hardware exacto (solo ares los emula bien). Aparcado.
