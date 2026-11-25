# Hora, sensores IIO y suspension (Slide, kernel 5.4.233) - 2026-11-25

Medido por ssh en la Slide (uptime ~1500 s, boot con `modem_control` ya en marcha). **La sesion se corto en la tarea 3**: la Slide se reinicio sola
(ver "Incidente al final") y no pude repetir pruebas ni validar un arranque en frio con los servicios nuevos.

## 1. Hora (HECHO, probado reiniciando el servicio; NO probado en arranque en frio)

### Medido
- Hay RTC hardware: `/sys/class/rtc/rtc0` = `sprd-rtc sc27xx-rtc` (PMIC, SPI4), `/dev/rtc0`, rango `[0, 5662310399]`. Cuenta (`since_epoch` 38665 -> 38671 -> 38676 en 5 s)
  pero **arranca en la epoca 1970**: `since_epoch` = segundos desde un origen de arranque (~10 h), no una fecha. Dmesg:
  `sprd-rtc sc27xx-rtc: setting system clock to 1970-01-01T10:22:21 UTC (37341)` (HCTOSYS=y) y la fecha "abril 2026" sale de la fecha de compilacion de systemd
  (`/usr/lib/systemd/systemd` mtime 2026-04-13), que systemd usa como minimo cuando la hora es anterior (por eso apt falla las firmas).
- Es decir: el RTC existe y se lee, pero nadie le habia escrito jamas una fecha real y la Slide no tiene bateria de RTC valida/NTP.
- `fake-hwclock`, `systemd-timesyncd`, `chrony`, `hwclock` (util-linux): NO instalados. `busybox hwclock` SI (`busybox hwclock -w -u` escribe al RTC y devuelve rc=0).
  Tras `date -u -s` + `busybox hwclock -w -u`: `hwclock -r` = `Wed Nov 25 19:55:38 2026` y `rtc0/since_epoch` = 1795636538 (el RTC SI acepta una fecha real).
- `/var/lib/dpkg/status` no sirve como piso (se toca al instalar paquetes).

### Entregado (`rootfs/overlay/`, desplegado y habilitado en la Slide)
- `usr/local/sbin/rgslide-clock load|save` (sh, sin apt): `save` escribe `date +%s` en `/var/lib/rgslide/lasttime` (atomico) y a rtc0; ignora horas < 1790000000 (sep-2026).
  `load`: si la hora actual < la guardada, la restaura (`date -u -s` + `busybox hwclock -w -u`); si no, no toca nada. Como fake-hwclock: tras apagado la hora retrocede como mucho al ultimo guardado.
- `etc/systemd/system/rgslide-clock.service` (oneshot, `RemainAfterExit`, `WantedBy=sysinit.target`, `Before=sysinit.target time-set.target apt-daily.service`;
  `ExecStart=load`, `ExecStop=save` = al apagar).
- `rgslide-clock-save.service` + `rgslide-clock-save.timer` (`OnBootSec=2min`, `OnUnitActiveSec=5min`) guardan la hora periodicamente (el panic/reinicio por watchdog no pasa por ExecStop).
- Prueba: con `date -u -s "2026-04-13 20:00:00"` y `systemctl restart rgslide-clock` -> `rgslide-clock: hora restaurada a Wed Nov 25 07:56:06 PM UTC 2026 (era 1776110400)`;
  `systemctl stop` guardo 1795636566 y `hwclock -r` = `Nov 25 19:56:06 2026`. `systemctl is-enabled` = enabled (servicio y timer).
- LIMITE: **no hay fuente real de hora**. Tras un apagado largo la hora sera la del ultimo guardado (puede ir atrasada). Para ponerla bien: `ssh root@10.55.0.2 "date -u -s @$(date -u +%s) && busybox hwclock -w -u"` desde el PC (o NTP si algun dia hay red). NO lo automatice en el PC.
- Pendiente de persona/arranque: confirmar tras el proximo arranque en frio que `date` sale > 2026-11-25 y `journalctl -u rgslide-clock` muestra `restaurada`. Mientras no se reinicie, el servicio solo se probo con `restart`.

## 2. Sensores IIO (HECHO el diagnostico; NO hay sensor con datos)

### Inventario medido
- `iio:device0` = `sprd-sensor` (`/sys/devices/platform/sprd-sensorhub`, DT `sprd,qogirn6pro-sensorhub` / `sprd,sensor-hub`, `sipc_sensorhub_id` = 9), `trigger0` = `sprd-sensor-dev0`.
  Canales: `in_illuminance_raw` -> `Invalid argument`; scan_elements `in_accel_x/y/z` (index 0/1/2, `le:u64/64>>0`), `in_accel_x&y` (3), `in_timestamp` (4, `le:s64`). Buffer: `enable=0 length=2 watermark=1 data_available=0`.
- Clase `/sys/class/sprd_sensorhub/sensor_hub`: `raw_data_als` y `raw_data_ps` -> `Invalid argument`; `version` -> `Connection timed out`; `mcu_mode` = 0; `sensor_info` vacio; `flush_setcnt=0,flush_getcnt=0`.
  Ficheros de depuracion: `cm4_operate`, `cm4_spi_set`, `cm4_spi_sync` (leer/escribir I2C/SPI del CM4 por mando), `calibrator_*`, `logctl`, `enable`/`batch` (solo escritura). Strings del modulo: `RegMapR_GetProximityRawData`, `RegMapR_GetLightRawData`, `shub_save_mag_offset` (hay luz, proximidad y brujula previstas).
- Dmesg del sensorhub, en bucle: `sprd-sbuf: sbuf-9-4 not ready to write!` + `sprd-sensor: nwrite=-19,len=13,sent_len=0,timeout=100ms` + `nread=-19,retry=1`.
  `-19` = ENODEV: el canal sbuf 9-4 (SIPC, destino 9) no esta listo.
- `iio:device1` = `sc27xx-adc` (ADC del PMIC): no es movimiento. `/proc/bus/input/devices`: no hay acelerometro/giroscopio/proximidad como input. Hall: `gpio-491 GPIO KEY HALL in hi` (nivel alto = deslizador cerrado/abierto sin determinar), en `gpio-keys` event0 (capabilities key=`8000 1c000000000008 0`).
- **Nada del sensorhub da datos reales.** No se pudo confirmar que modelos de acelerometro/giroscopio/luz/proximidad hay (esa informacion vive en el firmware del CM4).

### Causa (medida + deducida)
- Medido: destino SIPC 9 = el CM4 "ch" (`chsys`). `/sys/kernel/debug/mpm/power_manage` lista `sipc-chsys-mpm-9` con `awake_cnt=0`, `last up module = smsg-9-41` (sin trafico), mientras
  `sipc-pmsys-mpm-6` (pm_sys, el que arranca `rgslide-cm4`) SI tiene actividad (`awake_cnt=4`). `/sys/kernel/debug/modem/chsys`: `run_state: 255` (sin arrancar) frente a `pmsys`: `run_state: 1`.
  `/dev/chsys` existe (mayor 494) pero `modem_control` en la Slide solo abre `/dev/sctl_pm`; `/dev/sctl_ch` -> `No such device`.
- En GammaOS (`dump/live/dmesg-gamma-now.txt`): a 10.07 s `modem@0/@1 run = 1`, a **10.536 s** `unisoc-mailbox channel 9-6/9-5/9-7/9-4/9-41 receive open msg!` (el CH/CM4 destino 9 arranca y abre sus canales) y a **11.648 s** `sprd-sensor sprd-sensorhub: CM4 Version:20250616`;
  solo entonces el HAL (`vendor.sensors-hal-multihal`, AIDL `android.hardware.sensors`) lee sensores. En Debian no hay HAL y el destino 9 nunca abre.
- Origen del arranque del CH: el `vendor_a` del .pac trae `/vendor/etc/modem_ch_info.xml` (que NO esta en la Slide ni en `dump/android/root/vendor/etc/`): `modem_name ch`, `ioctl_path /dev/chsys`, `src_file ch_sys` (particion `ch_sys_a/_b`, 0x100000 bytes
  en `0xAE640000`, flag `0x00100000`), `chsys_bootcode` en `0x65016000` (bootcode `0x2a000 0x9 0xf2404802 0x600121e6 ...`), `modem_range base=0x65016000 size=0x60000`. Extraido a `dump/android/modem_ch_info.xml` (dump/ no se versiona).
  `modem_control` lo referencia (`/vendor/etc/modem_ch_info.xml`, `start load ch image!`, `modem_ctrl_listen_ch`, `/dev/sctl_ch`, `CH State: CH Alive`). La reserva `ch_ddr@ae600000` esta en el DT y `ae600000-ae8fffff : reserved` en iomem. `ch_sys_a/_b` (sda30/sda31, 32768 sectores) existen y tienen contenido.
- **Hipotesis (NO probada)**: falta copiar `modem_ch_info.xml` a `/vendor/etc` (donde `install-cm4.sh` solo enlaza `*.xml` de `/opt/android/vendor/etc/`) para que el `modem_control` de `rgslide-cm4` cargue tambien el CH, y el sensorhub despierte. Es una accion con riesgo: cargar mas firmware/arrancar otro CPU (incidentes de reinicio). **No lo probe**: lo dejo para el agente principal o una persona con acceso fisico.
- Siguiente paso propuesto (reversible): copiar `dump/android/modem_ch_info.xml` a `/opt/android/vendor/etc/` y enlazarlo a `/vendor/etc/`, reiniciar SOLO `rgslide-cm4` (o el arranque), y mirar `dmesg | grep -E "9-[0-9]+ receive open|CM4 Version|sbuf-9-4"` y `cat sensor_hub/version`.

### Utilidad de orientacion
- **NO escrita**: sin acelerometro con datos no hay nada que leer; una utilidad inventada no seria verificable. Si el CH arranca, el scan_element `in_accel_x/y/z` (`le:u64`, 8 bytes por canal; la unidad/escala no esta documentada ni medida) con `buffer/enable=1` y el trigger `sprd-sensor-dev0` seria la via; habria que medir ejes/escala con la Slide en mano.

## 3. Suspension (NO hecha; solo lo ya medido en `docs/power-sensors.md`)
- No llegue a ejecutar los sondeos de solo lectura (`/sys/power/{state,mem_sleep,wakeup_count}`, wakeup_sources, logind): la sesion se corto justo en esa orden.
  Ojo para quien lo repita: **leer `/sys/power/wakeup_count` bloquea mientras haya un evento de despertar en curso** (charger_manager lo mantiene con temporizador de 3 s); mejor evitarlo o usar `timeout 2`.
- Sigue valido lo de `docs/power-sensors.md`: `state = freeze mem`, `mem_sleep = s2idle [deep]`, `rtcwake -m freeze` entra/sale pero despierta al instante (IRQ de `sprd,multi-sipa-0`, `alarmtimer` EBUSY); HandlePowerKey=suspend desplegado, logind sin reiniciar.
- Como se haria (NO probado): `systemctl suspend` (o `echo freeze > /sys/power/state`) con una persona delante, `rtcwake -m freeze -s 20` como red de seguridad, y despertar con el boton de encendido (`gpio-keys`, wakeup source). Falta probar: que despierte con el boton, que logind lo gestione, deep (`mem`), y el efecto del cargador.

## Que dejo cambiado en la Slide
- Ficheros nuevos: `/usr/local/sbin/rgslide-clock`, `/etc/systemd/system/rgslide-clock.service`, `rgslide-clock-save.service`, `rgslide-clock-save.timer`; habilitados (`rgslide-clock.service` en `sysinit.target.wants`, el timer en `timers.target.wants`), `/var/lib/rgslide/lasttime`.
- Reloj puesto al valor real (19:55:38 UTC del 25-nov-2026) y escrito en el RTC. Nada mas: no se cargaron modulos, no se hizo rmmod, no se toco audio/wifi/BT/ventilador/hybris, no se suspendio.
- En el PC: solo `dump/android/modem_ch_info.xml` (extraido del .pac), este informe y los ficheros de `rootfs/overlay/`. Sin git commit.

## Incidente al final de la sesion (medido, causa NO determinada)
- A ~14:03:40 (hora del PC, CST) la Slide dejo de responder (`ssh ... Connection timed out`, ping 100 % perdido) y en el PC reaparece como `Product: RG Slide rescue` (initramfs) con `rndis_host usb0 -> enx526850e09ea1`: se **reinicio sola**.
  El PC no tiene 10.55.0.1 en la nueva interfaz (regla conocida: tras cada reinicio hay que hacer `ip addr add 10.55.0.1/24 dev enx...`; no lo hice por la instruccion de no tocar el PC).
- Lo ultimo que ejecute fue una orden de solo lectura (cat de `/sys/power/*`, `suspend_stats`, `/sys/class/wakeup/*`, logind). Antes habia leido `sensor_hub/version` (timeout) sin efecto aparente. La Slide llevaba 1544 s de uptime y ya habia habido un panic de audio con el mismo tipo de carga; **no hay evidencia para culpar a mis lecturas ni para exculparlas**. Mirar `/var/lib/systemd/pstore/` y `journalctl -b -1` cuando vuelva.
- Consecuencia: un reinicio en frio sin pedir (riesgo de bucle de docs/quirks.md INCIDENTE). No reintente nada.
