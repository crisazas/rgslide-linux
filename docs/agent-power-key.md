# Boton de encendido, deslizador Hall y suspension (Slide, kernel 5.4.233) - 2026-11-25

Solo lectura/estatico en la Slide; no se probo ninguna suspension ni se pulso nada.

## Cronologia medida (`journalctl --list-boots`, `-o short-monotonic`, cmdline)
- Arranque -2 acaba 23:19:23 UTC: panic de `sipa_recv_skb` por la prueba de wakeup (ver `power-sensors.md`); NO es el boton. El arranque -1 empieza 23:20:41.
- Arranque -1 (23:20:41 -> 23:28:44). Ultimas lineas del journal, literales:
  - `[506.184091] systemd-logind[2170]: Power key pressed short.`
  - `[506.187594] systemd-logind[2170]: Suspending...`
  - `[506.243026] systemd-sleep[3036]: Performing sleep operation 'suspend'...`
  - `[506.248057] kernel: PM: suspend entry (deep)` / `Filesystems sync: 0.006 seconds`
  - `[506.265423] sprd-sensor sprd-sensorhub: status=3` / `[506.268295] ... mcu_mode == CW_BOOT!` y **nada mas** (ni panic ni apagado limpio).
- Arranque 0 (23:37:38): cmdline `bootcause="7s reset" pwroffcause="7s pwroff"` = reinicio duro por pulsacion larga (~7 s) del boton, hecho por el usuario tras el cuelgue.
  No hay pstore nuevo de ese cuelgue (`/var/lib/systemd/pstore` solo trae lo del panic de 23:19): un cuelgue duro no deja oops.
- Conclusion medida: **el boton (short press) lanzo `systemd-suspend` con `HandlePowerKey=suspend`, `PM: suspend entry (deep)` y la consola se colgo sin volver.** El reinicio fue manual (7s reset), no espontaneo.

## Entrada: que genera cada cosa (medido)
- `/dev/input/event0` = `gpio-keys`, codigos: KEY_F9 (67), KEY_VOLUMEDOWN (114), KEY_VOLUMEUP (115), KEY_POWER (116), KEY_WAKEUP (143). **Sin SW_LID** (`capabilities/sw` = 0).
- DT `/proc/device-tree/gpio-keys`: `key-power` (code 0x74, gpio 0x5f/1, debounce 2, `wakeup-source`); `key-hall` (label `GPIO KEY HALL`, code 0x8f=KEY_WAKEUP, gpio 0x162/27, debounce 10, `wakeup-source`, `wakeup-event-action = 2`).
  `/sys/kernel/debug/gpio`: `gpio-49 Power Key in hi IRQ ACTIVE LOW`, `gpio-491 GPIO KEY HALL in hi IRQ`.
- logind vigila `/dev/input/event0 (gpio-keys)` y `event2 (fts_ts)` ("Watching system buttons"). Sin inhibidores (`systemd-inhibit --list`).
- SUPUESTO (de memoria del codigo de logind, no verificado): logind no trata KEY_WAKEUP, asi que el deslizador no dispara ninguna accion de logind, solo despierta (wakeup-source) y llega a quien lea event0 (EmulationStation/RetroArch no lo usan como lid). Como no es SW_LID, `HandleLidSwitch*=ignore` no le afecta.
  El deslizador NO aparece como causa del reinicio: en el journal -1 no hay nada entre las 23:20 y la pulsacion de power; logind no registra el Hall, asi que no se puede **descartar** que el usuario lo moviera, solo que no se ve efecto.

## Por que cuelga: analisis estatico de `dump/boot/rd_vendor/lib/modules/sensorhub.ko` (objdump aarch64)
- El modulo NO tiene `dev_pm_ops`/callbacks suspend/resume: solo `register_pm_notifier(shub_notifier_fn)` (+ `register_reboot_notifier`). `shub_notifier_fn` (0xfbc):
  - evento 3 (PM_SUSPEND_PREPARE): `cancel_delayed_work_sync`, `shub_synctimestamp()` (solo actua si `mcu_mode == 2`), `dev_info("status=%d")`, y si `mcu_mode <= 1` hace `dev_info("mcu_mode == CW_BOOT!\n")` y **retorna 0**; solo si `mcu_mode > 1` manda un comando por sbuf al CM4 (`shub_send_command`, con `msleep`/`sbuf_write`).
  - evento 4 (POST_SUSPEND): igual con `status=2` y re-encola el trabajo.
- En la Slide `mcu_mode = 0` (`sensors-time.md`), asi que esa es exactamente la rama de las dos lineas que vimos: **`status=3` + `mcu_mode == CW_BOOT!` es la salida normal y rapida del notifier, no un bloqueo dentro de el.** Mi lectura corrige la del coordinador: el sensorhub es la ULTIMA linea visible, no la culpable demostrada.
- Donde se cuelga entonces: DESPUES de ese notifier (otros notifiers, congelado de procesos, `dpm_suspend`, syscore o el PSCI/firmware de `deep`). Con userspace congelado journald no vuelca mas lineas, por eso el log se corta ahi. Con lo disponible NO se puede localizar mas.
- Contraste: `s2idle` (`rtcwake -m freeze`) entra y sale (con el mismo notifier) y `mem_sleep` por defecto es `deep`; el cuelgue es del camino `deep` (apagado real de dominios, con CM4/modem `ch` sin arrancar y `charger_manager_wakelock`/`dwc3-sprd` pendientes en los `SPRD_PDBG`). Hipotesis NO probada: algun dominio del SoC no tiene el firmware/estado que espera la secuencia de sueno profundo.

## Mitigacion desplegada
- `rootfs/overlay/etc/systemd/logind.conf.d/rgslide.conf` (ya estaba): `HandlePowerKey=ignore`, `HandleSuspendKey=ignore`, lid/hibernate ignorados. Sigue `HandlePowerKeyLongPress=poweroff` (apagado limpio con pulsacion larga).
- NUEVO: enmascarados (`/etc/systemd/system/X -> /dev/null`, en `rootfs/overlay` y en la Slide con `systemctl mask`): `sleep.target`, `suspend.target`, `hibernate.target`, `hybrid-sleep.target`, `suspend-then-hibernate.target`, `systemd-suspend.service`, `systemd-hibernate.service`, `systemd-hybrid-sleep.service`, `systemd-suspend-then-hibernate.service`.
  Recargado con `systemctl kill -s HUP systemd-logind`. Comprobado: `busctl ... CanSuspend` = `"no"`, `CanHibernate` = `"na"`, `systemctl is-enabled suspend.target` = `masked`. La Slide siguio arriba (uptime 3 min, sin reinicios).
- LIMITE: la mascara solo cubre systemd. `echo mem > /sys/power/state`, `rtcwake`, `pm-suspend` o una app con acceso root siguen pudiendo colgarla. Para quitarlo: `sudo systemctl unmask ...` (o borrar los symlinks del overlay y volver a empujar).
- Efecto secundario: el boton de encendido (corto) ya no hace nada ni apaga la pantalla; solo la pulsacion larga apaga. No hay forma de "bloquear" la consola con el boton.

## Que debe probar el usuario con las manos (nada de esto se ha probado)
1. Pulsar el boton de encendido una vez (corto): debe no hacer nada y la Slide seguir viva (ssh responde). `journalctl -b -u systemd-logind` debe decir `Power key pressed short.` sin `Suspending...`.
2. Deslizar para cerrar y abrir (deslizador): comprobar que la Slide sigue viva. Con la sesion ssh: `sudo evtest /dev/input/event0` y mover el deslizador para ver el codigo/valor real (esperado KEY_WAKEUP 143, press/release) y en que posicion es 1.
3. Pulsacion larga (>5 s): esperado `poweroff` limpio (no 7s reset). Decidir si se quiere eso o `ignore` (tambien `HandlePowerKeyLongPress=ignore`).
4. NO probar suspension (ni `freeze` ni `mem`) hasta tener a alguien con acceso a los botones de reinicio y un cable UART/pstore; cuando se intente, probar primero `s2idle` con `rtcwake -m freeze -s 20` y mirar `dmesg` por `Freezing`/`PM: Syncing`, para ver en que paso se corta (los logs de `deep` no sobreviven).
