# Ventilador y vibración (Slide, kernel 5.4.233) - 2026-11-25

Todo medido por ssh en la Slide (`10.55.0.2`). Entregables en `rootfs/overlay/`:
`usr/local/sbin/rgslide-fan` (cambiado), `etc/rgslide-fan.conf` (nuevo), `usr/local/sbin/rgslide-rumble` (nuevo).
Los tres están desplegados en la Slide (no pasan por `push-overlay.sh`). No se cargó ni descargó ningún módulo, no se reinició nada a propósito.

## A. Ventilador

### ¿Gira de verdad? NO DEMOSTRADO (pendiente de persona)
Lo que SÍ se midió:
- El hwmon es `/sys/class/hwmon/hwmon0` (`name=pwmfan`), `pwm1` 0..255, sin `fan1_input` (no hay tacómetro). `gpio-185 (enable) out hi` siempre.
- `/sys/kernel/debug/pwm` sigue a `pwm1` (periodo 40000 ns, polaridad `inverse`): `pwm1=0 -> duty 39999 ns (enabled)`, `128 -> 19922 (enabled)`, `255 -> duty 0` y el PWM sale **sin `enabled`** (el driver lo apaga). Es decir, la orden llega al PWM.
- Corriente de batería (`sc27xx-fgu/current_now`): **inservible**. Con el mismo `pwm1=0` dio de 51 mA a 167 mA según el instante (6 rondas intercaladas 0/255, 15 muestras cada una), la `spi_mcu`/`joypad_spi_read` ocupa un 50 % de CPU y mueve el consumo. No prueba nada.
- Temperatura, carga de 8 procesos Python 60 s partiendo de ~41 C (soc-thmzone, °C a los 10/20/30/40/50/60 s):

  | pwm1 | prueba 1 | prueba 2 |
  |---|---|---|
  | 0   | 62 64 66 69 66 68 | 63 66 67 69 70 71 |
  | 255 | 64 65 66 66 67 66 | 65 66 66 67 68 68 |

  Con 255 acaba 2-3 C más fría en las dos parejas. Es coherente con que gire, pero la diferencia es pequeña y solo hay 2 repeticiones: **no concluyente**. Una primera tentativa (otro script) dio 70 C (pwm 0) frente a 49 C (255) en `soc_fin`, pero el máximo de zona con 255 fue 67,8 C y la medida salió con la carga ya terminada: la descarto.
- Semántica 255 = máximo: es la del hwmon. El driver guarda internamente `255-pwm1` y el PWM es `inverse`; no verificado que 255 sea realmente "más rápido" y no "parado".
- **Pendiente de persona**: oír/ver si el ventilador gira y suena distinto con `rgslide-fan mode off`, `mode 128` y `mode max` (cada uno 5 s). Si 255 fuese el parado, hay que invertir la curva.
- `cooling_device6` (`pwm-fan`, `max_state=0`, `cur_state=0`): el kernel nunca lo mueve; sin `rgslide-fan` no hay regulación. Ninguna zona llegó a mitigar (cpufreq de big7 siguió a 2,704 GHz hasta 70 C).

### Curva de control (`/etc/rgslide-fan.conf`, valores por defecto)
Temperatura = máximo de soc/big7/big7mid4/mid4/mid5/mid6/gpu/apcpu0/apcpu1/board, leída cada 3 s. Si no hay lectura o falla: 255.

| Sube al llegar a | pwm1 |
|---|---|
| < 45 C | 0 (parado) |
| 45 C | 90 |
| 52 C | 140 |
| 58 C | 200 |
| >= 64 C | 255 (70, 80 C... también 255; no hay nivel superior) |

Histéresis de 4 C: baja de nivel solo cuando `t+4` cae en un nivel menor. Simulado con la propia función `level_for`: bajando desde 255 se mantiene 255 hasta 60 C, 200 hasta 54 C, 140 hasta 48 C, 90 hasta 41 C y se para por debajo de 41 C. Con 45-64 C subiendo no hay saltos de más de un nivel por ciclo.

Cambio hecho en el script: **suelo de seguridad `FAN_SAFE` (58 C)**. Con tmax >= 58 C ningún modo (`off`, `N`) baja del nivel de la curva. Antes `FAN_MODE=off` o `N` podían dejar el ventilador parado con la CPU caliente.
Pruebas en la Slide:
- Carga de 8 procesos 60 s en modo `auto` (límite de aborto 75 C, no se alcanzó): soc 52->65 C, pwm1 0 -> 200 (a ~59 C) -> 255 (a ~64,5 C); máximo de cualquier zona 67 C. Parado nunca con la CPU caliente.
- Misma carga con `mode off`: arrancó con pwm1=0 a 57 C y en el primer ciclo (3 s) pasó a 200, luego 255 a 65 C. El suelo funciona.
- A 70/80 C NO se probó en hardware (límite 75 C; el máximo alcanzado fue ~71 C en soc durante la comparación con pwm 0, ventilador "parado"). Por la curva, >= 64 C es 255. Los trips del kernel (soc: 70 C passive, 85 C passive, 110 C critical) no llegaron a actuar.

### Control manual
```
rgslide-fan status            # modo, pwm1, temperaturas
rgslide-fan mode auto         # curva (por defecto)
rgslide-fan mode off|max|N    # parado / 255 / pwm fijo 0..255 (con el suelo de FAN_SAFE)
rgslide-fan set N|off|max     # escribe pwm1 una vez (el demonio lo pisa en <= 3 s)
systemctl stop rgslide-fan    # control total a mano con `echo N > /sys/class/hwmon/hwmon0/pwm1`
```
`rgslide-fan mode X` reescribe solo la línea `FAN_MODE=` de `/etc/rgslide-fan.conf`; el demonio relee el fichero cada ciclo. Probado: `128 -> pwm1=128`, `off -> 0`, `max -> 255`, `300 -> error uso (rc=1)`, `auto -> 90` (a 42 C, ver nota). Claves: `FAN_MODE`, `FAN_CURVE=t:pwm,...`, `FAN_HYST`, `FAN_PERIOD`, `FAN_SAFE`. `/etc/default/rgslide-fan` se sigue leyendo solo si no existe el nuevo.
- Nota: en `auto` a 42 C salió `pwm1=90` aunque la curva dice 0 bajo 45 C: es la histéresis (el servicio arrancó a 255 y baja a 90 hasta que t < 41 C).

## B. Vibración

### Funciona (medido)
- Dispositivo: `sc27xx:vibrator` (`sc27xx-vibrator`, DT `vibrator@2390`, compatible `sprd,ump9620-vibrator`), `event1` en este arranque (cambia: se busca por nombre). `EV=200001` (SYN+FF), `FF=107030000 0` (FF_RUMBLE, PERIODIC, SQUARE, TRIANGLE, SINE..., GAIN). `EVIOCGEFFECTS=16`; subí 4 efectos (ids 0-3) y los retiré sin error.
- No hay `/sys/class/leds/vibrator` ni `/sys/class/timed_output`. El único objeto sysfs observable es el regulador **`vddvib`** (`/sys/class/regulator/regulator.42`, 3,0 V fijo, `num_users 0`): `disabled` en reposo.
- Medición objetiva con `rgslide-rumble` y un muestreo cada 5 ms de `vddvib/state`:

  | llamada | `vddvib=enabled` durante |
  |---|---|
  | `-d 200 -i 100` | 32..228 ms |
  | `-d 200 -i 50`  | 35..231 ms |
  | `-d 200 -i 10`  | 42..233 ms |
  | `-d 200 -i 0`   | nunca (efecto de magnitud 0) |
  | `-d 600 -i 100` | 41..641 ms |

  Es decir: el efecto activa el regulador del motor exactamente durante la duración pedida (con ~35 ms de retardo) y no lo activa con magnitud 0. **La intensidad NO cambia nada medible**: el voltaje es siempre 3,0 V y `-i 10` y `-i 100` dan el mismo patrón. Si el motor modula (PWM interno del PMIC) o solo hace on/off no se sabe: **pendiente de persona** (sentir 10 % frente a 100 %).
- Corriente de batería no usada (ruido, ver A).

### `rgslide-rumble`
```
rgslide-rumble [-d MS] [-i 0..100] [-w 0..100] [-n VECES] [-p PAUSA_MS] [-e /dev/input/eventN] [-v]
rgslide-rumble                 # 200 ms al 100 %
rgslide-rumble -d 150 -i 30 -n 3 -p 80
rgslide-rumble -v -d 400       # imprime vddvib antes/durante/despues
```
Python 3 sin dependencias (ioctl EVIOCSFF/EVIOCRMFF + FF_GAIN + `write` EV_FF). Busca el `eventN` por el nombre `sc27xx:vibrator` en `/proc/bus/input/devices`. Probado: `antes: vddvib=disabled / durante: enabled / despues: disabled`, `rc=0`.
Aviso: el struct `ff_effect` está empaquetado a mano (48 bytes, aarch64/x86_64); el ioctl `0x40304580` asume ese tamaño.

### ¿Puede usarlo RetroArch? NO tal como está (solo documentado, sin cambios de kernel)
- El mando es `retrogame_joypad` (`event3`, `input42` en este arranque): `capabilities/ev = b` (SYN, KEY, ABS), **`capabilities/ff = 0`: no anuncia FF** (medido con `/sys/class/input/input*/capabilities/ff`; el único con FF es `sc27xx:vibrator`). El vibrador es otro input (`input39`, `event1`) que udev etiqueta solo `ID_INPUT=1` (ni joystick ni key).
- En `udev_joypad.c` de RetroArch (master; la instalada en `/opt/retroarch` no se ha comprobado) `udev_add_pad` hace `EVIOCGBIT(EV_FF)` sobre el **mismo fd del mando** y solo atiende rumble si `FF_RUMBLE` está en ese nodo y `EVIOCGEFFECTS >= 2`. Con `retrogame_joypad` no se cumple, así que RetroArch no vibraría nada aunque el core lo pida. El vibrador por sí solo cumpliría (`16` efectos, FF_RUMBLE) pero RetroArch nunca lo abre porque no es un joystick (exige `EV_KEY` y `ID_INPUT_JOYSTICK`).
- Opciones (ninguna aplicada):
  1. Parche a RetroArch (`third_party/RetroArch`, driver `udev`): ruta del vibrador fija o por nombre para rumble; es lo más limpio sin tocar el kernel.
  2. Dispositivo virtual por `uinput` (`/dev/uinput` existe, `CONFIG_INPUT_UINPUT=y`, `INPUT_FF_MEMLESS=y`): un demonio que cree un pad con las mismas teclas/ejes, `EV_FF` con FF_RUMBLE y reenvíe los eventos de `event3`; sus efectos FF se traducen a `rgslide-rumble`. Hay que ignorar el pad real en RetroArch para no duplicar. Más piezas móviles.
  3. En kernel: que `singleadc-joypad` llame a `input_ff_create_memless` con un callback que active el vibrador (lo más directo, pero requiere recompilar un módulo del kernel de fábrica: descartado por reglas).
  4. Sin parche: un script que lea `/dev/input/event3` y vibre en eventos (p. ej. al pulsar) no es rumble del core.

## Estado dejado en la Slide
- `/usr/local/sbin/rgslide-fan` (versión nueva), `/etc/rgslide-fan.conf` (nuevo), `/usr/local/sbin/rgslide-rumble` (nuevo).
- `rgslide-fan.service` reiniciado con la versión nueva, **modo `auto`** y activo (pwm1 90 a 42 C). Sin cambios en el `.service`.
- Borrados mis temporales (`/root/cmp2.*`). Nada más tocado (audio, wifi/BT, IIO, hybris).
- Incidente ajeno: durante mi primera prueba la Slide se reinició (limpio, `systemd-reboot`, sin panic ni pstore, otro agente); mi carga no fue la causa (pico 70 C, sin mensajes térmicos).

## Pendiente de persona
1. Con `rgslide-fan mode off|128|max` (5 s cada uno): ¿gira? ¿suena? ¿255 es el máximo y no el parado?
2. `rgslide-rumble -i 10` frente a `-i 100`, `-w 0`: ¿se siente?, ¿varía la intensidad o solo on/off?
3. Comprobar a mano la curva real con carga sostenida (> 70 C) en una sesión larga; en esta sesión el máximo fue 71 C.

## Intensidad de la vibracion: SOLO ON/OFF (2026-11-25, verificado con el usuario y con el codigo fuente)
- Prueba con persona: rampa 0/10/25/50/75/100 % con `rgslide-rumble -d 1200 -i N -v`; el usuario: "todos se sienten igual". El regulador `vddvib` pasa a `enabled` desde el 10 % y `disabled` al 0 %, igual de ancho en todos.
- Codigo mainline de `drivers/input/misc/sc27xx-vibra.c` (Linux 6.6, tambien en 5.4): `sc27xx_vibra_play` guarda `weak_magnitude` en `info->strength`; `sc27xx_vibra_play_work` solo distingue `strength == 0` de `!= 0` y enciende/apaga el LDO (`LDO_VIBR_PD`, `SLP_LDOVIBR_PD_EN`). No hay PWM ni tabla de niveles. NO se ha podido comparar con el `.ko` de fabrica de la Slide (no esta en `dump/`): la conclusion se apoya en el codigo mainline y en lo medido.
- Consecuencia: `-i` y `-w` de `rgslide-rumble` solo importan como 0 / distinto de 0. Lo que se puede variar es duracion y patron. Una "intensidad" aproximada se podria simular con pulsos cortos repetidos (ej. 20 ms on / 20 ms off): NO probado.
