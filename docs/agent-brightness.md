# Brillo (backlight)

## Medido (Slide, arranque en frío 00:25, kernel 5.4.233)
- El nodo ya existe sin cargar nada extra: `/sys/class/backlight/sprd_backlight` (type `raw`, `max_brightness`=255, `bl_power`=0), creado por el DT (`/sys/devices/platform/backlight`) con `sprd_drm` cargado. Escribe `brightness` (root) y `actual_brightness` lo sigue.
- Respaldo por PWM: `/sys/kernel/debug/pwm` muestra `pwm-0 (backlight)` de `64030000.pwm` (módulo `pwm_sprd`), periodo 40000 ns. El duty NO es lineal: raw 13→664 ns, 62→4457, 74→5552, 77→5786, 87→6725. `pwm-3` es el ventilador. `leds_sc27xx_bltc` solo da el LED RGB `sc27xx:{red,green,blue}`, no el panel.
- Valor al empezar: raw 62 (≈24 %). Al acabar: 62.

## Herramienta
- `/usr/local/sbin/rgslide-brightness get|raw|max_brightness|set N|up|down|restore` (sh; N = porcentaje 0..100; paso up/down = 5 %; se auto-eleva con `sudo -n` si no es root).
- Suelo del 5 % (raw 13): `set 0`, `set 1` y `down` nunca bajan de ahí. Entrada no numérica → rc 2.
- Persistencia: `/var/lib/rgslide/brightness` (porcentaje) la escribe cada `set/up/down`; `rgslide-brightness.service` (oneshot, RemainAfterExit, WantedBy=multi-user.target; espera hasta 30 s al nodo) ejecuta `restore`.
- Fuentes: `rootfs/overlay/usr/local/sbin/rgslide-brightness`, `rootfs/overlay/etc/systemd/system/rgslide-brightness.service`.

## Verificado
- set/up/down: sysfs `brightness` y `actual_brightness` cambian y el duty de pwm-0 con ellos (24 %→29→34→29→30→5 %(suelo)→24 %). `rgslide-es` activo antes y después.
- Unidad habilitada y arrancada: activa, valor tras `restore` = 61 (24 %; el redondeo da 61, no 62).
- NO probado: restore tras un reinicio real (prohibido reiniciar), ni que el panel se vea más/menos brillante (nadie delante; solo sysfs y duty PWM).

## Atajo propuesto (sin aplicar)
`RG_Slide.cfg` ya usa 20 como hotkey (`input_enable_hotkey_btn`). RetroArch no tiene acción de brillo, así que la opción realista es un demonio pequeño sobre `/dev/input` (junto a rgslide-input) que, con 20 pulsado, convierta `h0up/h0down` en `rgslide-brightness up/down`. Ojo: 20+dpad ya puede estar asignado en RetroArch a otras acciones; revisar antes. En ES no hay hotkey equivalente.

## Deshacer
`sudo systemctl disable --now rgslide-brightness; sudo rm /etc/systemd/system/rgslide-brightness.service /usr/local/sbin/rgslide-brightness /var/lib/rgslide/brightness`; valor manual: `echo 62 | sudo tee /sys/class/backlight/sprd_backlight/brightness`.
