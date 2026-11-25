# Bluetooth (Marlin3 Lite / SC2355) - 2026-11-25

## Resultado
`hci0` queda UP RUNNING, `bluetoothctl show` -> `Powered: yes`, y `bluetoothctl scan on` lista 14 dispositivos BLE/BR-EDR reales. Sin recargar ni descargar ningun modulo (solo `modprobe sprdbt_tty`, ya cargado antes) y sin panic.

## Causa del EINVAL (MEDIDA con `btmon`)
1. El chip responde HCI por `/dev/ttyBT0` sin init vendor, pero `hciconfig up` falla en el ULTIMO paso del init del kernel: `Write Default Link Policy Settings` con `0x000d` (RSWITCH|SNIFF|PARK) -> `Status: Invalid HCI Command Parameters (0x12)`. Es la causa del EINVAL.
2. El firmware anuncia Hold y Park en LMP features pero rechaza cualquier politica que los incluya (lo mismo documenta `third_party/mu300-linux` FINDINGS #25 con su parche de kernel `bluetooth-marlin3-link-policy.patch`). Antes de esa orden los ~45 comandos de init (features, buffers, LE, event mask, ...) devuelven Success.
3. NO es firmware ni BD: `wcn_bsp` ya carga `wcnmodem.bin` (sirve WiFi y BT).

## Solucion SIN recompilar el kernel (MEDIDA)
- `rgslide-bt-init` (= `third_party/mu300-linux/tools/bt-init/mu300-bt-init.c` compilado estatico aarch64, `out/bt/bt-init`): envia por `ttyBT0` las VSC `0xFCA0` (pskey 176 B de `bt_configure_pskey.ini`), `0xFCA2` (RF 252 B) y `0xFCA1 00 00 01` (enable); las tres devuelven status 0. No es lo que arregla el EINVAL, pero es el init de fabrica (Android `libbt-vendor`) y conviene mantenerlo.
- `rgslide-bt-proxy` (python3, solo stdlib): proxy H4 entre `ttyBT0` y un pty; reescribe `Write_Default_Link_Policy_Settings` (`01 0f 08 02 <lo> <hi>`) quitando los bits Hold (0x2) y Park (0x8). `btattach -B <pty> -P h4` crea `hci0` sobre el pty. Con `--attach` lanza el `btattach` el propio proxy.
- Alternativa descartada: parchear `net/bluetooth/hci_core.c` (el BT es `=y`, exigiria rehacer el boot.img). El proxy es reversible y vale para el kernel de fabrica.
- Prueba negativa: quitar el bit Park del `feature_set` del pskey NO cambia nada (el firmware lo ignora: `Features` sigue en `0xbf 0xff ...`).

## Desplegado en la Slide (reversible: `rm` + `daemon-reload`)
- `/usr/local/sbin/{rgslide-bt-prep,rgslide-bt-proxy,rgslide-bt-init}`, `/usr/lib/firmware/bt_configure_{pskey,rf}.ini`, `/etc/systemd/system/rgslide-bt.service` (**NO habilitado**; `Requires=rgslide-wifi`, `Before=bluetooth`, Type=simple).
- En el repo: `rootfs/overlay/usr/local/sbin/{rgslide-bt-prep,rgslide-bt-proxy}`, `rootfs/overlay/etc/systemd/system/rgslide-bt.service`. El binario `rgslide-bt-init` NO esta en el overlay (se compila: `aarch64-linux-gnu-gcc -O2 -static -o out/bt/bt-init third_party/mu300-linux/tools/bt-init/mu300-bt-init.c`; la Slide no tiene gcc).
- Estado dejado: `rgslide-bt` arrancado y activo (proxy + btattach), `hci0` UP, `bluetoothd` ya lo enciende (`btmgmt info`: powered ssp br/edr le).
- Secuencia del servicio: `modprobe sprdbt_tty` -> rfkill1 off/on (rearranca el BT del chip) -> `rgslide-bt-init` -> proxy+btattach. Tarda ~1 min desde `systemctl start` hasta `hci0`: el `mtty_write` de la VSC bloquea el proceso en estado D varios segundos con la Slide cargada. Usar `start --no-block`.

## Limitaciones MEDIDAS
- **BD address fija e igual en todas las unidades: `8F:8F:8E:8E:6D:6D`** (la del `bt_configure_pskey.ini`). El `-a` del init mete otra dir en el pskey (offset 20..25, bytes enviados correctos) pero `Read BD ADDR` sigue devolviendo `6d 6d 8e 8e 8f 8f`: este firmware no la toma del pskey. Para dirigirla haria falta un `set_bdaddr` en el driver (rg-rotate-linux lo hace en su `btsprd_hci.c`, kernel distinto) o `btmgmt public-addr` con un controlador que lo soporte; no probado. Dos Slides cerca chocarian.
- `hcitool lescan` -> `Set scan parameters failed: I/O error` (herramienta legacy con bluetoothd ya dueno del adaptador); `hcitool scan` no mostro nada en 15 s; `bluetoothctl scan on` SI funciona.
- Si algo mata `btattach`/proxy el `hci0` desaparece: relanzar `systemctl restart rgslide-bt` (hace rfkill off/on). Un `btattach` huerfano sobre `ttyBT0` deja la tty ocupada.
- Alias/clase por defecto `rgslide`, clase 0x400000; `Pairable: no`, `Discoverable: no`.

## Supuesto / sin probar
- Que el servicio arranque bien en un arranque en frio (solo probado con wcn_bsp/wifi ya cargados; hay que habilitarlo despues de `rgslide-wifi`).
- Emparejamiento, conexion, A2DP/HID, audio SCO, coexistencia con wifi bajo carga; `bluetoothctl power on` tras reinicio del servicio.
- Ese transporte pasa por `sprdbt_tty` (UART virtual), no por el driver HCI de rg-rotate-linux; su rendimiento no se ha medido.
- Que el PA/I2S del BT-SCO funcione (depende del AGDSP, sin resolver).
