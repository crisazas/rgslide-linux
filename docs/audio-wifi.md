# Audio, WiFi y Bluetooth en Debian (kernel de fabrica 5.4.233) - 2026-11-25

Estado: la tarjeta ALSA se registra; NO hay sonido comprobado; WiFi/BT NO probados. Nada de esto se ha commiteado.

## 1. Audio

### 1.1 Lo comprobado (con salidas)
- Los modulos `snd_soc_sprd_*`, `audio_*`, `agdsp_access`, `sprd_audcp_*` estan en `/lib/modules/5.4.233/`. Cargados con `rgslide-audio-prep` (orden de `dump/live/dlkm/modules.load`) aparece la tarjeta, **unos segundos despues** de acabar `modprobe` (el card hace probe diferido: -517 mientras faltan cpu/codec dai):
  ```
  $ cat /proc/asound/cards
   0 [sprdphonesc2730]: sprdphone-sc273 - sprdphone-sc2730
  ```
  (`--- no soundcards ---` si se lee demasiado pronto). `/dev/snd` (31 nodos) lo crea el `mdev -df` de `rgslide-mdev.service` con la regla `snd/.*` de `/etc/mdev.conf`.
- Faltaban en la lista que ya estaba cargada: `sprd_apipe`, `audio-pipe`, `audio-dsp-dump`, `sprd_audcp_boot`, `voice_trigger_irq`, `snd-soc-sprd-codec-ump9620-power(-dev)`, `snd-soc-sprd-dummy-codec`, `snd-soc-sprd-vbc-fe`, `snd-soc-sprd-pa-ucp1301`. Sin `ump9620-power` y `dummy-codec` el card da `Parsing dai link N failed(-517)`.
- PCM registrados (`/proc/asound/pcm`): 00-01 DisplayPort, 00-02 FE_ST_NORMAL_AP01 (play+cap), 00-03 AP23, 00-05 FAST, 00-07 VOICE, 00-12 LOOP, 00-14 A2DP_PCM, 00-57 MM_P, etc. (20 PCM). Mixer: 320 controles (`amixer -c0 controls`, solo lectura), p.ej. `Speaker Function`, `HPL Mixer DACLHPL Switch`, `VBC_DAC0_DP_EN`, `S_NORMAL_AP01_P_CODEC SWITCH`, `Inter PA Config`.
- `alsa-utils` NO esta instalado en la Slide (hay `libasound2`, `libpipewire`, `libpulse`, sin `aplay`/`amixer`/`pulseaudio`/`pipewire`). Probado con el .deb arm64 de Debian extraido en /tmp (`aplay`, `amixer`, `speaker-test`; no se instalo nada).

### 1.2 KERNEL PANIC (causado por mi `speaker-test`, 21:09 del reloj del RTC, uptime 5512 s)
Traza (`journalctl -b -1`):
```
[ASoC: PCM ] sprd_pcm_preallocate_dma_ddr32_buffer alloc size = 0x20000 for data
  ... buf->area(virt)=0xffffffc013c5b000, buf->addr(phy)=0xaf725000, size=131072
[ASoC: FE.VBC ] fe_hw_params fe dai: FE_DAI_ID_NORMAL_AP01(0) playback
[ASoC: PCM ] chan=2 totsize=192000 period=48000
Unable to handle kernel paging request at virtual address ffffffc013c7b000   (WnR=1)
pc : __memset+0x16c   lr : snd_pcm_hw_params+0x618   Comm: speaker-test   x0=ffffffc013c5b000 x8=ffffffc013c7b000
Call trace: snd_pcm_common_ioctl+0x148c  <-  snd_pcm_ioctl+0x30  <-  do_vfs_ioctl
```
Causa exacta: `SNDRV_PCM_IOCTL_HW_PARAMS` sobre `hw:0,2` (FE_ST_NORMAL_AP01, playback) aceptó un buffer de **192000 bytes** (periodo 48000 B x 4), pero el driver `sprd-pcm` (`sprd_pcm_preallocate_dma_ddr32_buffer`) solo preasigna **0x20000 = 131072 B**. `snd_pcm_hw_params` hace `memset` de `runtime->dma_bytes` (192000) sobre `dma_area` (`0xffffffc013c5b000`) y escribe pasado el final del buffer: la direccion de fallo es exactamente `dma_area + 0x20000`. Es un bug del driver (el limite `buffer_bytes_max` del PCM es mayor que lo preasignado), independiente de que el AGDSP este arrancado o no (el fallo ocurre antes de hablar con el DSP: `sprd_pcm_hw_params ... return 0` ya habia salido bien).
- Hipotesis NO probada: pedir buffer <= 131072 B (p.ej. 2 periodos de 16384 B) evitaria el panic. No lo he intentado tras el aviso del agente principal. Cualquier cliente ALSA/PipeWire/PulseAudio que pida buffer grande (el aplay por defecto pide ~500 ms) provocara el mismo panic: **no abrir los PCM de la tarjeta 0** hasta probar con un buffer acotado.
- Mi `speaker-test` ademas colgo el comando ssh (120 s sin salida) justo antes del panic; la Slide reinicio.

### 1.3 AGDSP (DSP de audio): que falta
- Firmware: particion `l_agdsp_a` / `l_agdsp_b` (6 MiB, cabecera `SharkL5_AUDCP_2022Y_VER_3125`, `AUDCP.SharkL6`, trailer AVB). No es el `agdsp.bin` de `third_party/rg-rotate-linux` (otro SoC UMS512; sha256 distinto).
- Quien lo carga en Android: el HAL de audio (`/vendor/lib/hw/audio.primary.whale.so`, funciones `agdsp_boot`, `agdsp_reboot`) via sysfs de `sprd_audcp_boot`: `/sys/devices/platform/audiocp_boot/{agdsp,ldinfo,start,stop,status}` (ueventd.rc: dueno audioserver). NO es modem_control (este solo carga `modem`, `phy`, `pm_sys`, nv; `ldsp`/`gdsp`/`cdsp` aparecen en sus cadenas pero `modem_cp_info.xml` no lista `l_agdsp`) ni un servicio aparte. `ldinfo` de la Slide: `agdsp`, dst=0xafa00000, size 0x600000 (6 MiB).
- Mecanica reconstruida del desensamblado de `sprd_audcp_boot.ko` (`agdsp_store` = memcpy por paginas de 4 KiB al buffer `0xafa00000`; `start_store` = memset de regiones + secuencia regmap de reset/shutdown/bootvector; `status_show` lee 3 estados): volcar la imagen a `agdsp` y escribir a `start`.
- Probado (UNA vez, antes del panic): `dd if=/dev/block/by-name/l_agdsp_a of=.../audiocp_boot/agdsp bs=4096 count=1536` (6291456 B copiados) y `echo 1 > start` con rc=0, sin errores en dmesg. `status` antes y despues: `07 00 00 00 07 00 00 00 00 00 00 00` (sin cambios), asi que **no se comprobo que el DSP arrancara**. Tampoco se probo `aud_send_cmd` real. Sin AGDSP no hay sonido: el VBC de este SoC es "DSP-mediated" (como documenta rg-rotate-linux en `DEVICE-BRINGUP.md`: tras arrancar el AGDSP hace falta un estado de mixer con `DSP VBC Profile Select` y rutas DAC->HPL/HPR, ver `docs/stock_mixer_{idle,during}.txt` de ese repo; son de UMS512, solo orientativos).
- En GammaOS (`dump/live/dmesg-gamma-now.txt`) `agdsp_access` y `audio_sipc` ya hablan con el DSP al probe (`aud_send_cmd ... wait dsp` a ~14.8 s); en Debian `agdsp_access_probe` da `Failed to open channel 0,dst=0,rval=-19` (ENODEV: el DSP no esta corriendo) - mismo sintoma antes de arrancarlo.
- Mixer/rutas de Android: `/vendor/etc/audio_policy_configuration.xml`, `audio_config.xml` estan en `vendor_a.img` (montado en /tmp/opencode/vend4); las rutas del mixer (`/odm/etc/audio_route.xml`, `/odm/etc/audio_params/sprd/*`) estan en la particion ODM/product, NO en vendor_a: no las he encontrado. Altavoz: segun el log de GammaOS (`hook_general_spk id:0, gpio:176`) el PA externo se controla por GPIO 176 desde el card; no verificado en Debian.
- Jack: el codec ump9620 registra "sprdphone-sc2730 Headset Jack" (input41/42). USB-C: PCM `00-01 DisplayPort MultiMedia` (snd-soc-unisoc-dp-codec) y nodos `*_USB` del VBC; sin probar.

### 1.4 Siguiente paso seguro (propuesta)
1. Con la tarjeta cargada y SIN abrir PCM: comprobar si `echo 1 > start` hace pasar `agdsp_access` a ok (reintentar el probe/leer `/proc/asound`, mirar `status`).
2. Solo entonces, un unico `hw_params` desde un programa propio con buffer <= 65536 B en un PCM de prueba, con una consola serie/ssh abierta y asumiendo que la Slide puede reiniciar (avisar al resto de agentes).
3. Si el panic persiste con buffer corto: parchear/evitar `sprd-pcm` (no hay fuentes del kernel aqui) o usar el camino de `compr`/`vbc_turning`.

## 2. WiFi (Marlin3 Lite / `unisoc,marlin3lite_sdio`) - NO probado
- DT de la Slide: nodo `sprd-marlin3` (`compatible=unisoc,marlin3lite_sdio`, `sprd,btwf-file-name=/dev/block/by-name/wcnmodem`, `sprd,gnss-file-name=/vendor/firmware/gnssmodem.bin`), con hijos `wlan`, `sprd-mtty` (BT), `gnss`, `sprd-fm`. Es el mismo diseno que describe `third_party/rg-rotate-linux` (driver de WiFi/BT propio sobre el bus `sprdwcn`/SDIO; ellos NO usaron `wcn_bsp`).
- Modulos de fabrica: `wcn_bsp`, `sprd_wlan_combo`, `sprdbt_tty` (+ `sprd_fm`, `gnss_common_ctl_all`); dependen de `pcie-sprd`, `pcie-sprd-misc`, `trusty-ipc`, `sipa-core`, `sipc-core`. En la Slide NO estan cargados; no hay `wlan0`, `/sys/class/ieee80211`, `bluetooth` ni `rfkill` utiles.
- En GammaOS `wcn_bsp` pide por `request_firmware` (via ueventd) `wcnmodem.bin` (3145728 B), `gnssmodem.bin` y `wifi_board_config.xpe.ini`, y despues los arrancan `vendor.wcnd` (`/vendor/bin/connmgr`), `wcn@1.0-service` y `wcn_chr` (propiedades Android). No he localizado esos ficheros en `vendor_a.img` ni `system_a.img` (ni hay particion `wcnmodem` en by-name); los de `rg-rotate-linux` (`wcnmodem.bin` 947120 B, ini de `sprd/`) son de otro tamano: no se si valen.
- En Debian: no existe `/lib/firmware` ni `/vendor/firmware`; haria falta aprovisionar el firmware y probablemente `connmgr` bajo `linker64`/`libpropshim`. No cargue `wcn_bsp` por el riesgo de otro panic (alimenta el chip, hace request_firmware con fallback sysfs sin udev/mdev que lo atienda: busybox mdev tiene `FEATURE_MDEV_LOAD_FIRMWARE`, no activado en nuestra conf).

## 3. Bluetooth - NO probado
- Mismo chip y bus que el WiFi (`sprdbt_tty`, `sprd-mtty`); en rg-rotate-linux el BT funciona con un driver HCI propio sobre el bus sprdwcn. Con los modulos de fabrica depende de que `wcn_bsp` arranque el chip (seccion 2) y de `libbt-vendor` de Android. `bluez` esta instalado en la Slide; sin `hci0`.

## 4. Archivos entregados (sin commitear, sin habilitar)
- `rootfs/overlay/usr/local/sbin/rgslide-audio-prep` (solo carga modulos, idempotente, no abre PCM; `RGSLIDE_AGDSP_BOOT=1` vuelca `l_agdsp_a` y escribe `start`, sin verificar)
- `rootfs/overlay/etc/systemd/system/rgslide-audio.service` (oneshot, NO habilitado por `push-overlay.sh`)
- `rootfs/overlay/etc/modules-load.d/rgslide-audio.conf` (todo comentado a proposito)
- Ejecutado a mano en la Slide: `/tmp/rgslide-audio-prep` -> tarjeta `sprdphonesc2730` tras ~5 s, 31 nodos `/dev/snd`, 0 oops/panic en dmesg. No se desplego a /usr/local/sbin ni se tocaron /opt/android ni /opt/hybris.
