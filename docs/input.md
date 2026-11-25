# Entrada de la RG Slide en Debian (2026-11-25)

Fuentes: DT vivo (`/sys/firmware/fdt` -> dtc), dmesg de GammaOS (`dump/live/dmesg-gamma-now.txt`), `strings` de `singleadcjoy.ko`, pruebas en la Slide.

## Hardware
| Dispositivo evdev | Driver / módulo | Qué es |
|---|---|---|
| `retrogame_joypad` (bus 0x19, 484b:1101, v0100) | `singleadcjoy.ko` (platform `singleadc-joypad`) | **Gamepad entero**: 18 GPIO + 6 canales ADC por multiplexor (amux A/B/EN) |
| `virtual_gamepad` (bus 0x18 abcd:abcd) | mismo módulo | Pantalla táctil virtual de Android (ABS_MT 1280x960). **No es un mando**; udev la etiqueta TOUCHSCREEN |
| `gpio-keys` | gpio-keys (built-in) | Power (KEY_POWER), Vol+ (KEY_VOLUMEUP), Vol- (KEY_VOLUMEDOWN), hall (KEY_WAKEUP, sensor del deslizador); KEY_F9 lo anuncia el kernel de fábrica |
| `sc27xx:vibrator` | `sc27xx_vibra.ko` (ya cargado) | Vibración, FF_RUMBLE/FF_PERIODIC (EV_FF) |
| `fts_ts` | `focaltech_ts.ko` | Táctil I2C 3-0038 (0..1279 x 0..959) |

- MCU: `spi_mcu spi0.0` (DT `spi-mcu-joy@0`, SPI `spi@60000`, GPIO mcu-pwr/wakeup/reset), el mismo `.ko`. Sigue en el firmware de fábrica; `mcu_info` en sysfs devuelve `476c02`. Gestiona LEDs RGB (`led_mode`, `led_switch`, `custum_rgb_*`) y remapeo (`remapkey_*`). No genera eventos evdev propios.
- Sticks analógicos: ADC del PMIC (`sc27xx_adc`, canales `joypad_adc` y `trigger_adc`) multiplexados con GPIO amux. Gatillos L2/R2 y ABS_Z/RZ salen también del ADC.
- Botones GPIO del DT (`linux,code`): A=0x134 (BTN_WEST), B=0x131 (BTN_EAST), X=0x133 (BTN_NORTH), Y=0x130 (BTN_SOUTH), TL/TR=0x136/0x137, TL2/TR2=0x138/0x139, SELECT=0x13a, START=0x13b, THUMBL/R=0x13d/0x13e, dpad = ABS_HAT0X/Y (abs-value 1/2), **Back lado derecho = KEY_BACK (0x9e)**, F10 (0x44). BTN_MODE (0x13c) y BTN_C/BTN_Z los anuncia el driver pero no tienen GPIO en el DT.
- Teclado deslizable: no hay dispositivo de teclado; solo el hall (`KEY_WAKEUP`, `hall_status` en `/sys/devices/platform/gpio-keys`: 0 ahora).

## Causa de que faltara el gamepad
`singleadcjoy` se cargaba por alias del DT antes que `sc27xx_adc` (que no estaba en la carga). Su probe no devuelve `-EPROBE_DEFER`: `joypad iio channel get error`, `-22`, y queda sin enlazar para siempre. Cargar `sc27xx_adc` y hacer `echo singleadc-joypad > /sys/bus/platform/drivers/singleadc-joypad/bind` lo arregla (probado, `probe success`, aparece `event3`/`event4`).

## Entregables
- `rootfs/overlay/etc/modules-load.d/rgslide-input.conf`: `sc27xx_adc`.
- `rootfs/overlay/usr/local/sbin/rgslide-input-prep` + `rootfs/overlay/etc/systemd/system/rgslide-input.service`: espera al IIO `sc27xx-adc`, `modprobe singleadcjoy`, y hace `bind` si el probe previo falló. **El agente principal debe añadir `rgslide-input.service` al `systemctl enable` de `scripts/push-overlay.sh` y `chmod 755` del script** (en la Slide ya lo he instalado y habilitado a mano).
- `rootfs/overlay/usr/share/rgslide/autoconfig/RG_Slide.cfg`: perfil de RetroArch (udev, vid 18507 / pid 4353).
- mdev: no hacen falta reglas; `input/.* root:input 660` ya existe y `event3/4` aparecieron solos (`console` está en el grupo `input`).

## Comprobado en la Slide
- Tras el bind: `retrogame_joypad` en `event3`, EV=b (KEY, ABS), `ID_INPUT_JOYSTICK=1`.
- EVIOCGBIT teclas: KEY_F10, KEY_BACK, BTN_GAMEPAD(SOUTH), EAST, C, NORTH, WEST, Z, TL, TR, TL2, TR2, SELECT, START, MODE, THUMBL, THUMBR, DPAD_UP/DOWN/LEFT/RIGHT.
- Ejes: X, Y, RX, RY -16384..16384 (fuzz/flat 64); Z, RZ 0..16384; HAT0X/Y -1..1.
- GPIO de todos los botones en el estado de reposo (`/sys/kernel/debug/gpio`), todos `in hi`; ADC `amux_debug` lee 16216; `adc_*` a 0 (sticks centrados).
- Vibrador: efecto FF_RUMBLE subido y reproducido en `event1` sin error (EVIOCSFF id 0). No se percibe el movimiento sin persona.
- `rgslide-input.service` arrancado a mano: `active`, probe correcto.

## NO comprobado
- Ninguna pulsación real: dirección/valor de cada botón, signo del hat (abs-value 2 = izquierda/arriba, supuesto -1), polaridad de los ejes, nombre de cada botón físico frente al rotulado. El perfil de RetroArch es una deducción del DT y de `udev_joypad.c`.
- Arranque en frío con el servicio habilitado (la Slide **se reinició sola a las 21:09 por un panic de `speaker-test`/ALSA** hecho por otro agente, no por esta tarea; tras el reinicio el joypad volvió a no existir hasta lanzar `rgslide-input.service`, lo que confirma la causa).
- Orden de arranque entre `rgslide-input.service` y `rgslide-mdev.service` en un arranque limpio; si los nodos no aparecen, `mdev -s`.
- RetroArch instalado: no está en la Slide, `input_device_display_name` y los índices se validan al ejecutarlo.
- `virtual_gamepad`: puede confundir a RetroArch (aparece como ABS pero no es joystick, `ID_INPUT_TOUCHSCREEN`); si lo detecta, ignorarlo por nombre.
- MCU: LED RGB/remapeo, volumen/power/hall reales (los IRQ están registrados, sin pulsar no hay eventos), KEY_F9 y vibración sentida.
