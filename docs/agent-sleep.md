# Suspensión s2idle por botón de bloqueo y tapa (2026-11-26)

Objetivo del usuario: suspensión **NO profunda** con el botón de bloqueo y al cerrar la consola. Nada de `deep`: el intento del 25-11 con `deep` colgó la consola en `sprd-sensorhub` y hubo que reiniciar a mano (docs/agent-power-key.md).

## Piezas
- `usr/local/sbin/rgslide-sleep`: fija `s2idle` como único modo en `/sys/power/mem_sleep` (se ejecuta en `ExecStartPre` de la unidad y antes de cada ciclo).
- `usr/local/sbin/rgslide-sleepd`: lee `gpio-keys` (event0) sin `EVIOCGRAB`. `KEY_POWER` (116) pulso corto → suspende; `KEY_WAKEUP` (143, key-hall del DT) → cerrar suspende, abrir despierta. Al volver escribe `1` en `.../3-0038/refresh` (relée el firmware del panel táctil).
- `etc/systemd/system/rgslide-sleep.service`: arranca lo anterior. Despliegue y vuelta atrás: `scripts/install-sleep.sh install|undo`.

## Inhibidores
No suspende si `/run/rgslide/session` dice `game` o `panel` (lo escribe `hotkey-launch.py`; un juego en marcha no debe irse a dormir por un roce del botón), ni si `systemd-inhibit --list` muestra un bloqueo `:sleep`. La tapa cerrada con juego tampoco suspende: se queda la pantalla encendida hasta que se sale del juego.

## Estado
- El instalador está escrito y el demonio compila (`py_compile`), pero **la suspensión no está probada en frío**: el primer intento con s2idle en caliente dejó la consola con ping y sin sshd (puerto 22 sin aceptar), sin llegar a despertar. Hace falta reinicio físico (Vol-down + Power) y repetir la prueba.
- Mientras no haya un ciclo suspend/despertar bueno, el botón power de logind sigue con `HandlePowerKey=suspend` y `deep` es el modo por defecto de la ROM: la unidad es la que cambia a `s2idle`.

## Vuelta atrás
`scripts/install-sleep.sh undo` (deshabilita y borra la unidad y los dos scripts). El ajuste de `/sys/power/mem_sleep` no persiste al reiniciar.
