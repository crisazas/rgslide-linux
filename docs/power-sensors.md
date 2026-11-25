# Ventilador, sensores, suspension y bateria (Slide, kernel 5.4.233) - 2026-11-25

Todo probado a mano en la Slide por ssh. Entregables en `rootfs/overlay/`:
`etc/modules-load.d/rgslide-power.conf`, `usr/local/sbin/rgslide-fan`,
`etc/systemd/system/rgslide-fan.service`, `rgslide-fgu.service`,
`etc/systemd/logind.conf.d/rgslide.conf`. Desplegados y habilitados en la Slide (no pasan por `push-overlay.sh`).

## Termica y ventilador
- Tras el arranque solo estaban `pwm_sprd`, `pwm_fan`, `hw_mon`: las 25 `thermal_zone*` salian `mode=disabled` y `temp` daba EINVAL.
- Faltaban `sc27xx_adc sprd_ump96xx_tsensor sprd_thermal sprd_thermal_r5p0 sprd_soc_thm sprd_cpu_cooling`. Cargados (`rgslide-power.conf`):
  `thermal_zone3 soc-thmzone enabled 35162`, `big7 37340`, `gpu 35077`, `board 34988` (mC). Siguen `disabled`: `osctsen`, `outtsen`
  (dmesg: `ump96xx-tsensor: no calibration mode, don't register`; no son criticas).
- Politica `step_wise`; soc-thmzone: trip0 passive 70000, trip1 passive 85000, trip2 critical 110000. Cooling devices: cpufreq-0/1/2, devfreq-0/1, `thermal-cpu-0/1/2` y `pwm-fan` (**max_state=0**: el kernel NUNCA lo mueve).
- Ventilador: `/sys/class/hwmon/hwmon0/pwm1` (hwmon `pwmfan`, 0..255; `pwm1=255` = maximo). Sin `fan1_input` (no hay tacometro).
  `/sys/kernel/debug/pwm`: `pwm-3 (fan) period 40000 ns polarity inverse`; `pwm1=0 -> duty 39999`, `100 -> 24314`, `255 -> duty 0` (inversa).
  Con `pwm1=255` el PWM aparece sin `enabled` (el driver lo apaga: `pwm_apply_state` con duty 0). Enable-gpio = gpio-185 (`out hi`).
  El driver guarda internamente 255-pwm1 (visto en `pwm_show/pwm_store` del .ko). Que 255 sea "maximo" es la semantica del hwmon, NO verificada con el oido/tacometro.
- `rgslide-fan` (python3, servicio `rgslide-fan.service`): lee max de soc/big7/mid/gpu/apcpu/board cada 3 s; curva 45C->90, 52C->140, 58C->200, 64C->255,
  histeresis 4 C; sin lectura -> 255. Config opcional `/etc/default/rgslide-fan` (`FAN_MODE=auto|off|max|N`, `FAN_CURVE=45:90,...`). `rgslide-fan status|set N`.
- Prueba con 8x `yes`: el SoC subio de 41 a 70 C con el ventilador al maximo (pwm=255 a >=64 C); al parar la carga bajo a 47 C y la curva paso a pwm1=90.
- NO comprobado: que el ventilador gire de verdad / ruido (no hay tacometro ni oido); el efecto de pwm1 en la temperatura (la comparativa 255 vs 0 se corto, ver abajo).
- OJO: durante una prueba larga la Slide hizo kernel panic en `__memset <- snd_pcm_common_ioctl` (audio, otro agente), NO en termica; reinicio a los ~3,5 min de carga.
  Tras el reinicio `/tmp` y el RTC (vuelve a abril 2026) se pierden. Un panic cae al CM4/watchdog y reinicia sin avisar.

## Sensores
- `/sys/bus/iio/devices/iio:device0` = `sprd-sensor` (sensorhub, `/sys/devices/platform/sprd-sensorhub`), `/dev/iio:device0`, `trigger0 = sprd-sensor-dev0`.
  Canales: `in_illuminance_raw` (read -> EINVAL), scan_elements `in_accel_x/y/z`, `in_accel_x&y`, `in_timestamp` (todos `_en=1`, tipo `le:u64/64>>0`).
  Con trigger y `buffer/enable=1` el buffer queda vacio (`data_available=0`, `head -c` no devuelve nada en 3 s).
  Causa: el sensorhub habla por SIPC con un coprocesador (GammaOS: `sprd-sensor: nwrite=-19` = ENODEV) cuyo firmware no esta cargado. Sin ese firmware no hay IMU/luz/proximidad/brujula.
- `iio:device1` = `sc27xx-adc` (32 canales `in_voltageN_input`, `in_voltage20_raw`=4080): ADC del PMIC, no son sensores de movimiento.
- Hall: `key-hall` (gpio-keys, gpio 27/0x1b en gpiochip de 0x160, code 0x8f=143 KEY_WAKEUP-like) existe en el DT, pero `/proc/bus/input/devices` solo lista
  `gpio-keys` (event0), `sc27xx:vibrator` (event1), `fts_ts` (event2), `retrogame_joypad` (event3), `virtual_gamepad` (event4). No he accionado el hall.
- Si hubiera sensores vendria de `vendor.sensors-hal-multihal` de Android; en Debian no hay HAL. NO comprobado: giroscopio, brujula, proximidad (no hay nodo).

## Suspension
- Kernel: `CONFIG_SUSPEND=y`, `PM_SLEEP=y`, `PM_WAKELOCKS=y`, `HIBERNATION` no. `/sys/power/state = freeze mem`, `mem_sleep = s2idle [deep]`.
- `rtcwake -m freeze -s 15`: entra (`PM: suspend entry (s2idle)`) y sale sin colgarse; `suspend_stats success=1 fail=0`. PERO despierta casi al instante:
  1) `pm_system_irq_wakeup: 66 triggered sprd,multi-sipa-0` (el IRQ de red del modem; `25220000.sipa` acumula 100+ eventos).
  2) segunda prueba: `Device alarmtimer.0.auto failed to suspend: error -16` (EBUSY) -> "early wake event".
  Tambien `sprd_charger_manager: Charger Manager wakeup timer: 3000 ms` y `charger_manager_wakelock` pendiente: nunca duerme profundo.
- NO probado: `mem` (deep) -> riesgo de colgar sin nadie delante; `systemctl suspend`; que la tecla de power despierte.
- `logind.conf.d/rgslide.conf`: `HandlePowerKey=suspend`, `HandlePowerKeyLongPress=poweroff`, lid ignorado. Desplegado pero logind NO reiniciado: aplica al proximo arranque.
  Dado que s2idle vuelve al instante, el efecto de la tecla sera "pantalla/CPU un segundo en reposo"; si se prefiere, cambiar a `poweroff`.
- Wakeup sources (`/sys/class/wakeup`): `gpio-keys`, `sc27xx-rtc`, `alarmtimer`, `25100000.usb3`, `fts_ws`, `25220000.sipa`, `aw32257`, ...

## Bateria / cargador
- Tras el arranque solo `aw32257` y `sprd-tcpm-source-psy-sc27xx-pd`. `sc27xx-fgu` no hace probe solo (dmesg: `Cannot find power supply "sc27xx-fgu"` en bucle, `aw32257: Failed to get psy of sc27xx_fgu`).
  Con `echo sc27xx-fgu > /sys/bus/platform/drivers/sc27xx-fgu/bind` aparece (es lo que hace `rgslide-fgu.service`); requiere `sc27xx_adc` cargado.
- Lecturas (con el fgu enlazado): `battery`: capacity=59, status=Charging, voltage_now=3904000, current_now=-745000 (cargando), temp=302 (0,1 C),
  charge_full_design=5000000, health=Good, technology=Li-ion, cycle_count=27427 (valor basura), `time_to_full_now` 14733. `sc27xx-fgu`: voltage_ocv=4010000, `capacity=589` (decimas).
  `aw32257` (chip `bq24158`): online=1, status="Not charging", current_limit=100, termination_current=120; varias propiedades dan EINVAL (`charge_type`, ...) y llenan dmesg.
  `usb` online=1 (SDP 500 mA); `sprd-tcpm-source-psy-sc27xx-pd`: 5 V 3 A (PD).
- Se reinicia el estado tras el reinicio: tras el panic, `rgslide-fgu` lo vuelve a enlazar (probado con `systemctl start`; NO probado en un arranque en frio completo).

## No comprobado
Giro fisico, IMU real, hall al cerrar, ruido del ventilador, `systemctl suspend`, power key, deep sleep, arranque en frio con los servicios nuevos.

## Prueba de suspension que acabo en panic (2026-11-25, ~17:03 hora del PC)
- Lanzada SIN avisar al usuario: `power/wakeup=disabled` en `/sys/devices/platform/soc/soc:ipa-apb/25220000.sipa` y luego `echo freeze > /sys/power/state` con `wakealarm` a +25 s (script `/tmp/susp-test.sh`, su log se perdio al reiniciar).
- Resultado MEDIDO (`/var/lib/systemd/pstore/dmesg-ramoops-0/1`): a los 900 s de uptime `Unable to handle kernel NULL pointer dereference at virtual address 0x40`, `pc : sipa_recv_skb+0x7c [sipa_core]`, `Kernel panic - not syncing: Fatal exception in interrupt`; la Slide reinicio sola (arranque limpio, ES y wifi activos).
- NO se sabe: si el panic ocurrio al suspender (el console-ramoops no tiene lineas `PM: suspend entry`) ni si lo causo desactivar el wakeup de sipa. Hipotesis sin comprobar.
- Regla: no repetir pruebas de suspension sin que el usuario este delante y lo apruebe; la suspension sigue SIN funcionar.
