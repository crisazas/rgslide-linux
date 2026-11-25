# Sensorhub (CM4 "ch", destino SIPC 9): arrancado y con datos reales

## MEDIDO (Slide, 23:37-23:51 UTC del 25-nov, uptime 14 min sin reinicios)
- La hipotesis del xml sola NO basta. Con `modem_ch_info.xml` en `/vendor/etc` el `modem_control` lo parsea (strace: `modem_name:ch`, `partition[0] chsys`, `load info img=2`), pero imprime `modem_has_been_boot: PM has been boot!` y **no llama a la carga del CH** (nunca dice `start load ch image!`). `/dev/sctl_ch` ya existia (mayor 498) y `ch_sys_a/_b` = sda30/sda31.
- Causa en el binario (`dump/android/root/vendor/bin/modem_control`, funcion en 0xced0): solo llama a `load_modem(mask)` (0xa290) con `0x3f` si `modem=shutdown` en cmdline y `modem_has_been_boot()==0`. Aqui el PM ya estaba arrancado (`rgslide-cm4` lo hizo antes), asi que mask=0 y el CH no se carga. La rama de `0x20` = solo CH (`tbz w19,#5`).
- Prueba: copia con 2 instrucciones parcheadas (0xcf4c `cbnz`->`nop`, 0xcf50 `mov w0,#0x3f`->`#0x20`) = `modem_control_ch`. Resultado: `start load ch image!`, `load_img... ch_sys_b ==> /dev/chsys`.
- 1.er intento: `open ch_sys_b` -> `EACCES` (las particiones son `root:disk 660`): hizo falta `chmod o+r /dev/sda30 /dev/sda31` (el servicio original ya hace lo mismo con pm_sys/nr_*; `rgslide-cm4-prep` no incluye ch_sys). Con eso: verificacion trusty `kbc verify` pasa y a +0,4 s `unisoc-mailbox channel 9-6/9-5/9-4/9-7/9-41 receive open msg!`, `chsys run_state: 1`, y `sprd-sensor: CM4 Version:20250616` (igual que GammaOS). `sensor_hub/version` = 20250616, `mcu_mode`=2.
- `/dev/iio:device0` da datos. Activacion igual que Android (dmesg de GammaOS): `echo "H 0 20000 0" > sensor_hub/batch; echo "H 1" > sensor_hub/enable`; trigger `sprd-sensor-dev0`; scan_elements accel x/y/z + timestamp a 1; `buffer/enable=1`; leer `/dev/iio:device0`.
- Formato real: registros de **40 bytes** (no 8 por canal): `0x81, handle, 0`, 3 x float32 LE desde el byte 3 (sin alinear), u64 timestamp ns en el byte 27.
- Handle 1 (100 muestras, Slide quieta sobre la mesa): x=-0.076 y=-0.103 z=9.616 m/s2 (sd 0,005), |a|=9.617, dt=20,0 ms. Es acelerometro en m/s2; z positivo con la pantalla hacia arriba.
- Handle 10: x,y~0.0013/0.0019 y z=-0.168 constantes a 20 ms (tipo sin identificar). Handle 127: una muestra `x=-1 y=0 z=0` y luego nada.
- Sin cambios medidos fuera de lo listado: sin panic, pstore igual que antes (el panic previo `sipa_recv_skb` a 901 s es de un arranque anterior, sin relacion).
- No funciona aun: `raw_data_als`/`raw_data_ps` (`read RegMapR_Get*RawData failed`, ETIMEDOUT) y `in_illuminance_raw`: luz y proximidad no responden (puede que haga falta habilitar su handle antes; no probado).

## SUPUESTO (no medido)
- Handle 10 = giroscopio (valores pequenos), 127 = evento tipo hall/orientacion/"device orientation" (x=-1). Ejes/orientacion respecto al chasis sin verificar girando la Slide (nadie la movio).
- Luz y proximidad: handles distintos, formato desconocido.
- Para que sea permanente al arrancar: sustituir el `ExecStart` de `rgslide-cm4` por `modem_control_ch` (el PM tambien lo gestiona; no probado en frio) + `chmod o+r` de sda30/31 en `rgslide-cm4-prep`. NO se ha tocado el overlay.

## Lector minimo
- `scripts/agent-sensors-read.py [handle=1] [n=50]` (a la Slide: `ssh ... 'sudo python3 - 1 20' < scripts/agent-sensors-read.py`). Verificado con datos reales (handles 1, 10, 127). Sale solo a los 5 s si no hay muestras.
- `scripts/agent-ch-test.sh start|undo`: parchea y despliega `modem_control_ch` y lo lanza como unidad transitoria (`rgslide-ch-test`).

## Estado dejado en la Slide y como deshacer
- Parados: `rgslide-cm4` (inactive). Corriendo: `rgslide-ch-test2.service` (transitoria, `modem_control_ch`, mantiene PM+CH; log `/var/log/modem_control_ch.log`). Tambien `/tmp/*.strace`, `/tmp/acc1.bin`, `/tmp/h*.bin`, `/tmp/sh.ko`.
- Anadido: `/opt/android/vendor/bin/modem_control_ch`, `/opt/android/vendor/etc/modem_ch_info.xml` + enlace `/vendor/etc/modem_ch_info.xml`, `chmod o+r /dev/sda30 /dev/sda31`.
- Deshacer sin reiniciar: `sudo systemctl stop rgslide-ch-test2; sudo rm -f /opt/android/vendor/bin/modem_control_ch /opt/android/vendor/etc/modem_ch_info.xml /vendor/etc/modem_ch_info.xml; sudo chmod o-r /dev/sda30 /dev/sda31; sudo systemctl restart rgslide-cm4` (el CH ya cargado sigue vivo en el CM4 hasta reiniciar; reiniciar la Slide lo deja todo como antes, nada es persistente salvo `/opt/android/...` y los scripts).
- Aviso: tras parar `rgslide-ch-test2` sin relanzar `rgslide-cm4`, "sin el CM4 pm_sys la Slide se reinicia a ~284 s": usa siempre el comando de arriba. `suspend deep` cuelga en sprd-sensorhub (ver PROGRESS): con el CH vivo no se ha probado suspender.
