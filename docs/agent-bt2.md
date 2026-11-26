# Bluetooth 2: arranque robusto, emparejamiento y BD address (2026-11-25)

## Cambios (repo `rootfs/overlay/`, desplegados en la Slide; `rgslide-bt` sigue DESHABILITADO)
- `etc/systemd/system/rgslide-bt.service`: `Type=simple` (no bloquea multi-user.target aunque el chip no responda), `Restart=on-failure`, `RestartSec=15`, `StartLimitBurst=3`/`900 s`, `TimeoutStopSec=30`, `EnvironmentFile=-/etc/default/rgslide-bt`. Sin `Before=bluetooth`: ahora bluetoothd lo arranca rgslide-bt-up cuando hci0 existe.
- `usr/local/sbin/rgslide-bt-prep`: espera hasta 60 s a `wcn_bsp` (no falla de golpe si wifi aun carga) y 30 s a `/dev/ttyBT0`; lanza `rgslide-bt-up $$` en segundo plano; `rgslide-bt-init` con `timeout -k 5 90`; `BDADDR` opcional por el EnvironmentFile.
- `usr/local/sbin/rgslide-bt-up` (nuevo): espera hci0 (max 150 s; si no sale mata el servicio => Restart), `btmgmt power on`, `systemctl start --no-block bluetooth`, `bluetoothctl power on / pairable off / discoverable off`.
- `etc/systemd/system/bluetooth.service.d/rgslide.conf`: `ConditionPathExists=/sys/class/bluetooth/hci0` (sin chip, bluetoothd se omite sin error), `After=` y `PartOf=rgslide-bt.service` (reiniciar rgslide-bt para bluetoothd; bt-up lo vuelve a arrancar).
- `etc/bluetooth/main.conf`: reemplaza el de Debian (solo defaults comentados): `[Policy] AutoEnable=true`, timeouts 0. El kernel pone `Pairable: no` / `Discoverable: no` por defecto y bt-up lo reafirma.
- `usr/local/bin/rgslide-bt-pair` (nuevo): `scan [seg]` | `MAC` | `list` | `forget MAC`. Salida corta, solo actua sobre el MAC dado, `pairable on` solo durante el emparejamiento (agente NoInputNoOutput = Just Works). Para habilitar el servicio: `sudo systemctl enable rgslide-bt` (la unidad lleva `[Install]`).

## Tiempos MEDIDOS (Slide en marcha, load ~15)
- `systemctl restart rgslide-bt` hasta `hci0` presente: **66-68 s** (la VSC `0xfca0` de `rgslide-bt-init` tarda ~35-65 s con la Slide cargada). Parada del servicio vieja: 30 s (el proxy en estado D hace que venza `TimeoutStopSec` y systemd lo mata con SIGKILL; sin efecto secundario). Reinicio completo ~100 s: <2 min. hci0 `UP RUNNING`, `Powered: yes`, `Pairable: no`, `Discoverable: no`, ~15 s mas hasta el mensaje "listo" de bt-up (bluetoothd + power).
- `btmgmt power off/on`: 0.03 s / 1.8 s.
- `systemd-analyze verify rgslide-bt.service`: sin avisos (bluetooth.service solo se queja de `man bluetoothd(8)`).

## Escaneo (MEDIDO)
- `bluetoothctl --timeout 20 scan on`: **14 dispositivos distintos en 20 s** (BLE sobre todo, direcciones aleatorias), pero solo **3 con nombre** (CAR-KIT, S503_353506, S534_ee988d); `btmgmt find` no devolvio nada en esa ventana (bluetoothd ya poseia el descubrimiento). `rgslide-bt-pair scan 12` lista solo los 3 con nombre en 12 s. Un mando en modo emparejar anuncia nombre y aparecera ahi.
- Parseo: bluetoothctl mete secuencias ANSI aunque no haya tty; el script las quita con sed.
- `rgslide-bt-pair 00:11:22:33:44:55` (MAC inexistente): `FALLO ... not available` en 33 s, rc=1, `Pairable` vuelve a `no`. NO se emparejo con nada real.

## Soporte HID en el kernel (MEDIDO con /proc/config.gz)
- Integrado (`=y`): `CONFIG_BT_HIDP`, `CONFIG_UHID` (`/dev/uhid` existe, root:root 0660), `CONFIG_INPUT_UINPUT`, `HID_GENERIC`, `HID_SONY`, `HID_PLAYSTATION`, `HID_NINTENDO`, `HID_MICROSOFT`, `HID_WIIMOTE`, `HID_BATTERY_STRENGTH`, `JOYSTICK_XPAD`. Modulos que cargar: ninguno (hidp aparece en /sys/module).
- Faltan: `CONFIG_INPUT_JOYDEV` (no hay `/dev/input/js*`, solo evdev: SDL/RetroArch leen `event*`, vale), `BT_BNEP` (bluetoothd lo avisa, irrelevante), `HID_8BITDO`/similares (los mandos 8BitDo en modo X-input/HID generico entran por `HID_GENERIC`).
- bluetoothd (uhid por su plugin input/hog) corre como root; `/dev/input/event*` queda accesible al grupo `input`.

## BD address (MEDIDO + hipotesis)
- Hoy la Slide muestra **02:7B:5A:0C:18:5B** (= `02` + md5(machine-id)[:10], la que pasa `rgslide-bt-prep -a`), no la 8F:8F:8E:8E:6D:6D del `.ini`. Reiniciando el servicio con `BDADDR=02:11:22:33:44:55` (el EnvironmentFile llega al proxy, comprobado en `/proc/<pid>/environ`) `hcitool cmd 0x04 0x0009` y `hciconfig` siguieron dando 02:7B:5A:0C:18:5B.
- Hipotesis (NO probada): el firmware toma la direccion del pskey solo en la PRIMERA subida tras arrancar el modem (carga de `wcn_bsp`); el rfkill off/on no recarga el firmware y la direccion queda fija hasta el siguiente arranque. Encaja con agent-bt.md (primera init sin `-a` => 8F:8F...; `-a` posterior ignorado) y con hoy (primera init con `-a` => 02:7B...). Con el servicio habilitado, en frio saldra la de machine-id, unica por unidad (ya no chocan dos Slides). Verificar en frio.
- Cambio en caliente: `btmgmt public-addr` -> `Rejected (0x0b)` (sin `set_bdaddr` en este transporte). El comando vendor HCI para la dir no esta documentado en FINDINGS (solo pskey 0xFCA0); no se probaron `0xFC..` a ciegas (riesgo de colgar el chip).
- Para forzar una concreta en frio: `BDADDR=XX:XX:XX:XX:XX:XX` en `/etc/default/rgslide-bt` (fichero de prueba borrado, no existe ahora).

## Verificar tras el arranque en frio (coordinador)
1. `sudo systemctl enable rgslide-bt` (no lo he habilitado). Tras el arranque: `systemctl status rgslide-bt bluetooth`, `ls /sys/class/bluetooth`, `bluetoothctl show` (Powered: yes, Pairable: no).
2. `journalctl -u rgslide-bt -b -o short-monotonic`: busca `hci0 tras Ns` y `listo`; si salio `hci0 no aparecio`, ver reintentos (max 3 en 15 min).
3. Que multi-user.target / ES arrancan sin esperar a BT (`systemd-analyze critical-chain`), y que no hay panic.
4. Con un mando real (Sony/Nintendo/Xbox BLE): `sudo rgslide-bt-pair scan`, `sudo rgslide-bt-pair <MAC>`, `ls /dev/input/by-id`.
