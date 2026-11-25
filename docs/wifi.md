# WiFi / Bluetooth (Marlin3 Lite, `unisoc,marlin3lite_sdio`) - 2026-11-25

## 1. Firmware: DONDE esta (medido)
- NO esta en `vendor_a` ni en `system_a` ni en la particion `wcnmodem` (no existe en by-name; el DT dice `/dev/block/by-name/wcnmodem` pero el driver NO la lee: pide `wcnmodem.bin` por `request_firmware`).
- SI esta en la particion **`odm`** del `super` (`dump/pac/ex/super.img`, extent `odm_a` offset 4524605440, EROFS): `/odm/firmware/{wcnmodem.bin,gnssmodem.bin,wifi_board_config*.ini,bt_configure_*.ini,fm_board_config*.ini}` y `/odm/etc/wifi_board_config*.ini` (idénticos).
- Copiado al PC en `out/wifi-fw/` (no versionado, `out/` esta en `.gitignore`):
  - `wcnmodem.bin` 3145728 B, sha256 `04a9d8de...5297bba`; contiene `Platform Version:MARLIN3_20A_RLS2_W24.17.7`, `Project Version:sc2355_marlin3_lite` = exactamente lo que imprime el chip en el dmesg de GammaOS (`dump/live/dmesg-gamma-now.txt`).
  - `gnssmodem.bin` 595513 B; `sprd/` con los `.ini` (el `wifi_board_config.xpe.ini` son 7396 B = `data_len:0x1ce4` del dmesg de Android; los 7018 B de `rg-rotate-linux` NO son el mismo fichero).
- Para montar la odm en el PC: `sudo mount -o ro,loop,offset=4524605440,sizelimit=244854784 dump/pac/ex/super.img /tmp/opencode/m_odm`.

## 2. Que hace `wcn_bsp` (de `dump/live/dlkm/wcn_bsp.ko` + dmesg de GammaOS)
- `modinfo`: depende de `pcie-sprd`, `pcie-sprd-misc`, `sipc-core`, `trusty-ipc` (los 4 ya estan cargados en la Slide salvo los dos `pcie-*`). `sprd_wlan_combo` -> `sipa-core`, `wcn_bsp`, `sipc-core`. `sprdbt_tty`, `sprd_fm`, `gnss_common_ctl_all` -> `wcn_bsp`.
- Se engancha al nodo DT `sprd-marlin3` (platform driver, compat `unisoc,marlin3lite_sdio`); NO hay alias de modulo para ese compat (`modules.alias` solo tiene el alias PCI `1db3:2355` -> `wcn_bsp`), asi que **udev no lo autocarga**: un reinicio no lo arrastra.
- En su probe (workqueue `marlin_power_wq`) enciende el chip (regulators avdd12/avdd33/dcxo18, GPIO enable, reloj 32k), escanea la tarjeta SDIO por `mmc2` (22220000.sdio, ya presente), pide `gnssmodem.bin` y `wcnmodem.bin` con `request_firmware` (nombre plano, ruta `/lib/firmware/`; en Android iba por el fallback sysfs de ueventd), los descarga por SDIO, y apaga el chip. Todo en ~1,7 s en GammaOS (4.3 s -> 5.99 s).
- Luego `sprd_wlan_combo` (hijo `wlan`, `sprd,sc2355-sdio-wifi`) vuelve a encender el chip, pide `wifi_board_config.xpe.ini` y registra `wlan0` (6.4 s en Android). **`connmgr`/`vendor.wcnd` arrancan a los 9.6 s, despues de wlan0**: no hacen falta para crear la interfaz (si para loopcheck/calibracion; ver "Siguiente").
- `wcn_bsp` tambien usa `trusty-ipc` (`wcn_ca tipc init`): hay un error "wcn_ca tipc init fail, Or we are in Nsec Mode" en el binario; no se si es fatal. En la Slide `trusty_ipc` esta cargado (lo usan gsp/drm).
- Rutas opcionales que no existen aqui y que el driver tolera: `/mnt/vendor/productinfo/wcn/tsx_bt_data.txt`, `/mnt/vendor/wcn/connectivity_calibration_bak.ini`.
- Config del kernel de la Slide: `CFG80211=y`, `MAC80211=y`, `RFKILL=y`, `BT=y`, `FW_LOADER=y`. wpa_supplicant esta en marcha (`-u -s`), `bluez` instalado.

## 3. Plan (escrito antes de tocar la Slide)
1. PC: copiar `out/wifi-fw/{wcnmodem.bin,gnssmodem.bin}` y `out/wifi-fw/sprd/wifi_board_config*.ini` a `/usr/lib/firmware/` de la Slide (`/lib` -> `usr/lib`). Es solo copiar ficheros (reversible: `rm`).
2. Abrir `dmesg -w` por ssh a un fichero en el PC (`/tmp/opencode/wifi-dmesg.log`) para no perder el log si hay panic.
3. UNA vez: `modprobe wcn_bsp` (arrastra `pcie-sprd-misc`, `pcie-sprd`). Mirar dmesg; si el SSH se cae -> parar y documentar.
4. Si `wcn_bsp` termina bien (`WCN BASE ... download finished and run ok`): `modprobe sprd_wlan_combo` y buscar `wlan0`, `iw dev`, `ip link`.
5. Si hay wlan0: probar escaneo (`iw dev wlan0 scan` / `wpa_cli`), sin conectar a ninguna red hasta que alguien lo decida.
6. BT: solo `modprobe sprdbt_tty` si WiFi fue limpio; ver si aparece `/dev/ttyBT*`/`rfkill`. NO hay `hci0` hasta tener un adaptador (ver abajo).
7. Dejar script (`scripts/`, `rootfs/overlay/`) y servicio SIN habilitar.

## 4. RESULTADOS (2026-11-25, medidos en la Slide)

### WiFi: FUNCIONA hasta el escaneo
- Carga 1 (a mano, `modprobe wcn_bsp`; udev autocargo `sprd_wlan_combo` y `sprd_fm`) y carga 2 tras un reinicio pedido por el usuario (via `systemctl start rgslide-wifi`, ~11 s). Sin panic ni oops en ninguna: `dmesg -w` a `/tmp/opencode/wifi-dmesg{,2}.log` del PC, sin pstore nuevo. (Los 2 `Call trace` del log 2 son del arranque, `unisoc_mailbox` t=6.8 s, anteriores a mi carga: es el panic de probe ya documentado en quirks.md, esta vez sin reinicio.)
- `wcn_bsp` pide por `request_firmware` (ruta plana `/usr/lib/firmware/`): bastan `gnssmodem.bin`, `wcnmodem.bin` y `wifi_board_config.xpe.ini`. Sin `ueventd`/fallback sysfs. Salida clave: `marlin btwifi_download_firmware successfully!`, `WCN_VER:... MARLIN3_20A_RLS2_W24.17.7`, `chipid=2355b001`.
- `wcn_bsp` solo avisa de `pcie-sprd`: "The ep doesn't power on / Phy link never came up / power off the RC" (el RC PCIe no se usa, el chip va por SDIO). Inofensivo.
- Resultado: `wlan0` (MAC aleatoria distinta en cada carga: 40:45:da:d0:10:09, 40:45:da:05:55:68; el driver dice "use random MAC address"), `phy0`, `rfkill0` (wlan, soft-unblocked).
- Escaneo (solo escaneo, sin conectar) con un `wpa_supplicant -i wlan0 -D nl80211` temporal propio (ctrl en /tmp) + `wpa_cli scan`/`scan_results`: 17 BSS en 2,4 y 5 GHz (senales -28..-83 dBm; p.ej. "Internet" -28, "Megacable_5G_A228", "INFINITUM70C2"). Cerrado despues; wlan0 vuelto a DOWN.
- El `wpa_supplicant` del sistema (`-u -s`, dbus) NO ve la interfaz hasta que algo la pida por dbus; no lo toque. `iw`/`rfkill` no estan instalados (no instale nada).
- Sin conectar a ninguna red: falta decidir SSID/clave y quien gestiona (systemd-networkd/wpa_supplicant@wlan0.conf).

### Bluetooth: transporte SI, hci0 NO utilizable
- `modprobe sprdbt_tty` (rc=0): crea `/dev/ttyBT0` y `rfkill1` (bluetooth, nace bloqueado: `echo 1 > /sys/class/rfkill/rfkill1/state` lo desbloquea y arranca el chip, `start_marlin [MARLIN_BLUETOOTH]`).
- `btattach -B /dev/ttyBT0 -P h4` registra `hci0` (Bus UART, BD 27:93:31:14:22:11, features leidas, 23 comandos/eventos intercambiados): el chip RESPONDE HCI por el ttyBT0.
- PERO `hciconfig hci0 up` -> `Can't init device hci0: Invalid argument (22)`, y `btmgmt info` ve 0 controladores. Causa NO confirmada; hipotesis (de `third_party/rg-rotate-linux/docs/BLUETOOTH-SCOPING.md`): el firmware exige la secuencia vendor Unisoc (VSC 0xfca0 pskey + 0xfca2 RF + 0xfca1 enable) con `bt_configure_pskey.ini`/`bt_configure_rf.ini` (ya en `out/wifi-fw/sprd/`) antes del `HCI_RESET`; en Android la hace `libbt-vendor`. La BD 27:93:... es la de fabrica sin provisionar. Opciones: (a) mini-herramienta userspace que envie esas VSC por `ttyBT0` antes de `btattach`; (b) driver `btsprd` de rg-rotate-linux (otro kernel, no portable tal cual); (c) correr el libbt-vendor de Android bajo linker64.
- Un `btattach` de prueba quedo como zombie/colgado en la Slide (`pgrep` -> `[btattach]`); desaparece al reiniciar.

## 5. Estado dejado en la Slide
- Instalado: `/usr/lib/firmware/{wcnmodem.bin,gnssmodem.bin,wifi_board_config{,_aa,.xpe}.ini,sprd/}`; `/usr/local/sbin/rgslide-wifi-prep`; `/etc/systemd/system/rgslide-wifi.service` (**disabled**). En el repo: `rootfs/overlay/usr/local/sbin/rgslide-wifi-prep`, `rootfs/overlay/etc/systemd/system/rgslide-wifi.service`.
- Cargado ahora: `wcn_bsp`, `pcie_sprd{,_misc}`, `sprd_wlan_combo`, `sprd_fm`, `sprdbt_tty` (sin rmmod; no se descarga nada). `wlan0` DOWN, `hci0` registrado y DOWN, rfkill1 desbloqueado.
- NO hace falta `connmgr`/`vendor.wcnd` para wlan0 ni para el escaneo.
