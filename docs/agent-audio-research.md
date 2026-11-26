# Audio y micrófono: investigación (2026-11-25) - SOLO LECTURA, NADA EJECUTADO EN LA SLIDE

Etiquetas: **MEDIDA** (leída/medida en este PC o en la Slide, sin tocar nada), **CITADA** (URL), **SUPUESTA** (inferencia).
Alcance: no se escribió en /sys, no se abrió PCM, no se ejecutó start/stop, no se suspendió ni reinició. Lo único leído de la Slide: `uptime`, `/proc/interrupts`, `lsmod`, `/proc/asound/cards`, `ls /sys/devices/platform/audiocp_boot`, `dmesg` filtrado.
Fuentes locales: `third_party/rg-rotate-linux`, `third_party/base618`, `third_party/mu300-linux`, `dump/live/dlkm/*.ko`, `/tmp/opencode/aud/live.dts` (DTB vivo de la Slide). Copia de los documentos externos en `/tmp/opencode/res/` (e5-FINDINGS.md, e5_e5-audio-dsp, e5_HiFi.conf, parches 0010/0011).

## 0. Resumen (los 3 hallazgos útiles)

1. **Hay un proyecto con el MISMO diseño de audio que la Slide y con altavoz y micrófono verificados: `sprd-linux-devs/e5-linux`** (Rongyue E5, UMS9621 = misma familia qogirn6pro, mismo codec `ump9620`, misma tarjeta `sprdphone-sc2730`, mismos `audiocp_boot`/`audio-sipc`/`audio-mem-mgr`, mismas direcciones `0xafa00000`/`0xaf700000`/`0xaf980000`). CITADA: https://github.com/sprd-linux-devs/e5-linux (README, tabla Status: "Audio: speaker, microphone and receiver are verified"; `docs/FINDINGS.md` §24, §33, §34). Su `e5-audio-dsp` (https://raw.githubusercontent.com/sprd-linux-devs/e5-linux/main/rootfs/overlay/opt/e5/e5-audio-dsp) es la receta completa y hace **exactamente lo que ya hicimos** (parar, volcar `l_agdsp_a` a `agdsp`, `start`). No hay ioctl ni clk ni regulador extra para encender el DSP: **no existe un paso oculto de "alimentación" en `audiocp_boot`**.
2. **Lo que cambia el resultado no es el `start`, sino lo que hay alrededor** (CITADA, e5 FINDINGS §24.1/§24.8, §29 y parches 0010/0016): (a) la tarjeta debe estar registrada ANTES del `start` (si no, el dominio AGDSP no está alimentado y escribir los registros de reset reinicia el equipo); (b) el codec tiene que cargar DESPUÉS de `-power` y `-power-dev` (si no, calibración AO `cal_done failed -110`); (c) con el DSP corriendo sus interrupciones DMA/VBC **siguen sin llegar a ALSA** en e5 (`/proc/interrupts` `sprd_dma` a 0): lo resolvieron con un hrtimer que llama a `snd_pcm_period_elapsed()` (parche 0013, no publicado en este informe) o abriendo el PCM con `NO_PERIOD_WAKEUP`. En nuestra Slide `sprd_dma` también está a 0 (MEDIDA, líneas 20 y 88 de `/proc/interrupts`), así que **el síntoma "hw_ptr no avanza" que vimos NO prueba por sí solo que el DSP esté parado**.
3. **El `rval=-19` y el `status=7` son normales** (CITADA e5 §29: "`agdsp_pd_probe()` initialises `dst = 0, channel = 0`... SIPC target 0 is the AP itself. The vendor code logs one `Failed to open channel 0,dst=0,rval=-19` and lets the thread end; audio works without it"; §24.7: "`audiocp_boot/status` reads powered-down while idle (`core=7 sys=7`)"). Coincide con nuestro análisis estático de `docs/agent-audio.md` (MEDIDA). Lo que SÍ distingue "DSP vivo" es que `aud_send_cmd out,cmd` aparezca en dmesg sin `failed to get command` (CITADA, comentario de `dsp_alive()` en e5-audio-dsp).

## 1. Cómo se enciende el AGDSP (resumen contrastado)

| Paso | Qué | Etiqueta |
|---|---|---|
| 1 | Cargar módulos de audio; la tarjeta `sprdphone-sc2730` debe registrarse antes de tocar el DSP | CITADA (e5 `e5-audio-dsp`, cabecera) + MEDIDA (la Slide ya la registra, `/proc/asound/cards` la muestra tras unos segundos) |
| 2 | Imagen `l_agdsp_a` (6 MiB, cabecera `SharkL5_AUDCP_20xxY_VER_xxxx`) = la región reservada `audiodsp-mem@afa00000`; `ldinfo` = `agdsp`, 0xafa00000, 0x600000 | MEDIDA (docs/audio-wifi.md §1.3) + CITADA (e5 §24.7, mismo valor de ldinfo) |
| 3 | `echo 1 > stop`; `dd if=l_agdsp_a of=.../agdsp` (UN solo `dd`: el driver lleva su propio offset); `echo 1 > start`. Nada de `request_firmware` | CITADA (e5-audio-dsp) + MEDIDA (desensamblado `agdsp_store`/`stop_store` en `/tmp/opencode/aud/sprd_audcp_boot.s`: `agdsp_store` hace memcpy de páginas de 4 KiB acumulando offset en `+416/+424`, y `stop_store` los pone a 0) |
| 4 | El alimentador del dominio es `agdsp_access_enable` (AON `0x14c` bit 5 = `ap_access_ena`), no `audiocp_boot`. En la Slide ya se ejecuta (MEDIDA, 14 pares "wake up dsp/done val = 0x20") | MEDIDA + CITADA (e5 §24.2: "`agdsp_access_en` = REG_AON_APB_AUDCP_CTRL (0x6490014c) bit 5") |
| 5 | Tras `start` el handshake es por **mailbox** (`audio-sipc` con `mboxes = <&mailbox 5 0>`, canal `AGDSP`) y smsg en `0xaf980400`; no hay IRQ propia del AGDSP en el DT | MEDIDA (DT vivo líneas 5168-5170; `/proc/interrupts` 81-83 `64600000.mailbox`, el 82 con 45 IRQ y el 83 con 4) |
| 6 | Perfiles DSP (opcionales para reproducir): `Audio Structure Profile Select`=0 y `DSP VBC Profile Select`=0x404B0000 (clamp de amixer a 0x0fffffff → hay que escribir el valor directo al elemento) | CITADA (e5 §24.8) |

Diferencia importante Slide vs E5: la Slide usa el kernel **5.4.233** con `agdsp_access.ko` + `sipc-core.ko`; el E5 usa **5.15** con `agdsp_pd.ko` (genpd). Parches 0010/0016 del E5 (módulo `agdsp_pd`, `agdsp_can_access()`) **no se pueden portar tal cual** (no tenemos fuentes del kernel 5.4 aquí): SUPUESTA la equivalencia funcional con `agdsp_access`.

### 1.1 Lo que NO apoya ninguna fuente
- Ningún proyecto describe un reloj ni regulador adicional para el AUDCP antes del `start`. Los 28 `audcp-*` gates (DT `clock-controller@56200000`/`@56390000`, padre `ext-26m`) los toman los clientes al abrir PCM (CITADA e5 §24.2: "the `audcp-{vbc,aud,dma-ap,mcdt,icu,tmr-26m,dvfs-aspb,intc}-eb` clocks match Android's" tras activar `agdsp_access_en`). SUPUESTA: por tanto "enable_count 0" en reposo es normal (ya anotado en agent-audio.md).
- rg-rotate-linux (UMS512, kernel 7.1 mainline) afirma "AGDSP boots on mainline (ported `sprd-audcp-boot`...; both AGDSP power domains reach full wake-lock)" pero **no incluye las fuentes en el clon** (`src/linux-7-1-sprd` es un submódulo vacío): CITADA https://github.com/beebono/rg-rotate-linux/blob/main/DEVICE-BRINGUP.md (líneas 40-44). Útil de ellos: `DSP VBC Profile Select = 961216512` (0x39580000, UMS512) y `asound.state` (docs/stock_mixer_*.txt).
- base618 (T618, 7.2): el audio funciona y detalla que hay que **dormir el AGDSP cooperativamente** para suspender (`docs/deep-sleep.md`: "release AON 0x14c b5 and wait for 0xb8 == 7") y que "first PCM open after the sound server starts stalls (warm it up); HPL/HPR volume is inverted ... re-seeded on every PCM open; mic = mono on channel 1 (channel 0 is an unfed ADC pinned at -32768)" (`docs/hardware.md`, local). CITADA https://github.com/thwonp/base618. Confirma el mismo patrón del micrófono que e5.
- mu300-linux (UMS9620, U30 Air): **retiró el audio** ("Android disables audio entirely (no sound cards)"; test `tests/test_mainline_audio.py`; docs/FINDINGS.md línea 1484). Aunque e5 cita su flujo `mu300-audio-dsp` y que este "proved the whole load-and-boot sequence", esa versión ya no está en el árbol (MEDIDA: `grep` solo da el test que confirma que no hay `mu300-audio-dsp`). No hay nada utilizable ahí.
- No se localizó ninguna fuente pública de `sprd_audcp_boot.c` (el repo `archanaberry/advan-vxtux-6.18.52` la lista pero la ruta devolvió 404). Búsquedas sobre Halium/Droidian/postmarketOS/Ubuntu Touch en Unisoc: sin resultados útiles (SUPUESTA que no hay trabajo publicado para T820 más allá de lo anterior). El HAL `audio.primary.whale.so` ya fue leído (docs/audio-wifi.md §1.6).

## 2. Qué sabemos de nuestra Slide (MEDIDA, esta sesión)
- Kernel 5.4.233; módulos cargados: `agdsp_access`, `audio_sipc`, `audio_mem`, `sipc_core`, `unisoc_mailbox`, `sprd_audcp_dvfs`, `snd_soc_sprd_{card,codec_ump9620,vbc_v4,pdm_r2p0,tdm,dai}`. **Ni `sprd_audcp_boot` ni `audiocp_boot` con atributos**: en el momento de leer (5 min de uptime) `/proc/asound/cards` = "no soundcards" y `/sys/devices/platform/audiocp_boot/` solo tiene `driver_override modalias of_node power subsystem uevent waiting_for_supplier` (sin `agdsp ldinfo start stop status`, sin enlace `driver`). Es decir, tras este arranque en frío **`rgslide-audio-prep` aún no se ha ejecutado** (no es un fallo nuevo; coherente con que el servicio no está habilitado).
- `waiting_for_supplier` = 0.
- `/dev/block/by-name/l_agdsp_a|b` existen. No hay `alsa-utils`, ni `alsaucm`, ni `/usr/share/alsa/ucm2` en la Slide.
- `sprd_dma` (GIC 251 y 87) en 0 interrupciones, y mailbox 64600000 con IRQ en 82/83 (45 y 4). MEDIDA, pero es un estado sin audio, no comparable con e5.
- DT vivo, `sound@0`: `compatible = "unisoc,vbc-v4-codec-sc2730"`, nombre `sprdphone-sc2730`, `sprd,spk-ext-pa-gpio = <&gpio 0x70 0>` (=112), PA `ucp1301@58` en i2c con `reset-gpios = <&gpio 0x78>`; **no** el aw87xxx del E5 (la Slide usa el módulo `snd-soc-sprd-pa-ucp1301`). Nota: el log de GammaOS (`dump/live/dmesg-gamma-now.txt:183`) dice `hook_general_spk ... gpio: 176` (0xb0), distinto del `0x70` del DT; SUPUESTA la diferencia por el índice del controlador gpio (el 0x160 es el phandle del chip, no se ha resuelto cuál). **No fiarse del 176 sin comprobarlo.**
- dai-links del card en el DT (MEDIDA): `FE_NORMAL_AP01`, `FE_NORMAL_AP23`, `FE_CAPTURE_DSP` (**be-id 0**, cpu `<&fe-dai 2>`), `FE_FAST`, `FE_OFFLOAD`, `FE_VOICE`, `FE_VOIP`, `FE_FM`..., BE `BE_CAPTURE_DSP_CODEC`, etc. (mismos nombres que el E5, mismos módulos `snd-soc-sprd-*`). El mic va por `FE_CAPTURE_DSP` → `BE_CAPTURE_DSP_CODEC`.
- Existe un nodo `pdm-dmic@56370000` (`unisoc,pdm-ums9620`, módulo `snd-soc-sprd-pdm-r2p0`): hay micrófono digital PDM posible. SUPUESTA: el micro interno de la Slide es analógico (MIC1) o PDM; **no se ha determinado**.
- `sprd-audio-card,routing` del DT: `HPMIC Pin → HP Mic Jack`, `MIC Pin → Mic Jack`, `MIC2 Pin → Aux Mic Jack`, `HeadPhone Jack ← HP Pin`, `Ext Spk ← HP Pin / SPK Pin`, `Ext Ear ← EAR Pin / SPK Pin / HP Pin`.

## 3. Controles ALSA para el micrófono (nombres de la Slide: ver §3.1)

Tabla: control de e5 (CITADA, `HiFi.conf` Mic + verbo) y si el nombre existe como cadena en los `.ko` de la Slide (MEDIDA, `strings`/`grep -a` en `dump/live/dlkm`).

| Control (e5) | Valor e5 | ¿Existe la cadena en la Slide? | Módulo |
|---|---|---|---|
| `S_CAPTURE_DSP_CODEC SWITCH` | on | sí | sprd-platform-pcm-routing |
| `S_NORMAL_AP01_C_CODEC SWITCH` / `AP23` | on | sí | sprd-platform-pcm-routing |
| `ag_iis1_ext_sel_v2` | `aud_4ad_iis0_ad0` | sí | snd-soc-sprd-vbc-v4 |
| `VBC_MUX_ADC0_IIS_PORT_SEL` (ADC1, ADC2 análogos) | `VBC_IIS_PORT_IIS1` | sí | snd-soc-sprd-vbc-v4 |
| `VBC_IIS_RX0/1/2_WD_SEL` | `WD_24BIT` | no comprobado individualmente (`IIS` y `WD_SEL` aparecen en vbc-v4) | vbc-v4 |
| `VBC ADC0 DG Set` / `ADC1` | `17,17` | sí (ADC0) | vbc-v4 |
| `ADD0 DATA SEL` | `ADD0_DATA_MIC13` | sí (`ADD0_DATA_MIC12/13/23`) | snd-soc-sprd-codec-ump9620 |
| `ADC LRCLK Select`, `ADC1 LRCLK Select` | `invert` | sí | codec-ump9620 |
| `ADC_1 Gain ADC_1 Capture Volume` | 6 | sí (`ADC_1 Gain`, `ADC_1 Capture Volume`) | codec-ump9620 |
| `ADC_1 Mixer MIC1PGA_1 Switch` | on | sí (`ADC_1 Mixer`, `MIC1PGA_1 Switch`) | codec-ump9620 |
| `AUD ADC0L Switch` / `AUD ADC0R Switch` | on | sí (`AUD ADC0L`, `AUD ADC0R`) | codec-ump9620 |
| `Mic Function` | on | sí | snd-soc-sprd-card |
| `agdsp_access_en` | 1 | sí | agdsp_access |
| `SYS_IIS0` | `vbc_iis3` | sí | vbc-v4 |

Pistas en nuestro codec para otros micros (cadenas MEDIDAS en `snd-soc-sprd-codec-ump9620.ko`): `HPMIC Pin`, `HPMICPGA_1..3 Switch`, `MIC Pin`, `MIC1PGA_1`, `MICBIAS1..3`, `HEADMICBIAS`, `ADC_2/ADC_3 Switch`, `Digital ADC1L/ADC1R`, `DMIC Pin`; en card: `Mic Function`, `HP Mic Function`, `Aux Mic Function`, `Aux Mic2 Function`, `DMic Function`, `DMic1 Function`.

### 3.1 Qué NO se puede afirmar
- **SUPUESTA** que la Slide use MIC1 (`ADD0_DATA_MIC13`) como el E5: son placas distintas. El micrófono interno de la Slide puede ser MIC2/MIC3, HPMIC (jack) o PDM. El orden de dominancia debe salir de `/odm/etc/audio_route.xml`, que **no está en el volcado** (ya anotado en audio-wifi.md §1.3). Sin él, hay que probar combinaciones, con riesgo.
- **SUPUESTA** la dirección del flujo de captura DMA/IIS1 igual a e5; su captura del PCM `AP` (hw:N,0) "stalls after one period" y solo el DSP-capture (hw:N,2) entrega (CITADA e5 §33.1, §24.8 "Capture on FE_NORMAL_AP01 is IRAM-backed and capped at 3840 frames / 3 periods"). En la Slide, el PCM equivalente es el `FE_CAPTURE_DSP` (índice por nombre; no asumir `hw:0,2`: en la Slide `00-02` es `FE_ST_NORMAL_AP01`, MEDIDA en audio-wifi.md §1.1).
- Los valores exactos de gain/DG de la Slide (e5: ADC 6, DG 0x11) vienen de sus XML `audio_params`; los nuestros no los tenemos.

## 4. Riesgos conocidos (todos MEDIDOS salvo indicación)
1. **Buffer > 131072 B en `FE_NORMAL_AP01` playback → kernel panic** (`__memset` en `snd_pcm_hw_params`): `sprd_pcm_preallocate_dma_ddr32_buffer` reserva 0x20000 y el HW anuncia hasta 233440. Confirmado como bug del driver y **corregido en el E5** por parche 0011 (`runtime->hw.buffer_bytes_max = min(..., dma_buffer.bytes)`), CITADA https://raw.githubusercontent.com/sprd-linux-devs/e5-linux/main/kernel/patches/0011-audio-pcm-buffer-and-shutdown.patch. **La captura es PEOR**: el buffer de captura normal está en IRAM (`sprd,normal-captue-data = <0x56806000 0x1e00>` = 7680 B en el DT de la Slide, MEDIDA) y el E5 cuenta que PipeWire abriendo captura con 19200 B causó el mismo fallo en cada arranque. **Regla: buffer de captura ≤ 7680 B (p. ej. 2 periodos de 3840 B = 960 frames S16 mono) y nunca el valor por defecto de `arecord`.**
2. **Periodos aceptados por el driver**: en playback `FE_NORMAL_AP01` solo 320, 38400, 48000, 116720 B (pcmprobe, audio-wifi.md §1.5); en e5 "Period 1024 is refused; 960 (20 ms) works" en frames. Cada FE tiene su tabla: **hay que usar HW_REFINE (pcmprobe) antes de HW_PARAMS**.
3. **Leer/escribir registros del AGCP (0x56xxxxxx) con el dominio sin acceso = synchronous external abort → reinicio** (CITADA e5 §34 "Trap": `busybox devmem` en MCDT/DMA; §32 PipeWire leyendo `sprd_pcm_pointer` → panic tras 5 h 50 min). En la Slide nada de `devmem`/`/dev/mem` en 0x56xxxxxx; solo `/proc/asound/card0/{vbc,sprd-codec}` con un stream corriendo.
4. **`IMPD ENABLE` oops si se escribe antes de que el headset termine su probe** (CITADA e5 §24.8, parche 0014); no tocar `IMPD`/`HP_*` controles del headset sin que el jack esté registrado. En la Slide el jack ya está registrado ("sprdphone-sc2730 Headset Jack", MEDIDA en GammaOS dmesg).
5. **Cargar el card antes de `start`** y no hacer `rmmod`/reinicio de módulos de audio con el DSP en marcha (CITADA e5 y MEDIDA en tu AGENTS.md: "NO rmmod sprd_drm").
6. **Suspender cuelga la consola; `sipa` power/wakeup da panic** (MEDIDA por el proyecto): no se prueba nada del audio con `rtcwake`/`systemctl suspend`; mantener `RuntimeIdle` fuera.
7. **Deferred probe**: el kernel de la Slide es 5.4 y su `driver_deferred_probe_timeout` puede ser 0 (SUPUESTA; en e5 5.15 sí lo es, CITADA §24.8): si la tarjeta falla con "platform component (null) not found", hacer `bind_pd_consumers` (volver a sondear dispositivos en `/sys/bus/platform/drivers_probe`). La Slide ya registra la tarjeta sin eso (MEDIDA en audio-wifi.md §1.1), pero lo haría en frío tras un reinicio.
8. **Formato**: la captura DSP escribe SIEMPRE S16 aunque hw_params pida S24 (CITADA e5 §33.1, parche 0017); pedir **S16_LE** y NO PipeWire/Pulse (S24/planar/pro-audio abre 19 PCM y fallan 840 PREPAREs en 15 s, e5 §24.8).
9. **Lo que ya vimos** en la Slide (MEDIDA): `PREPARE` da EINVAL con "no backend DAIs enabled" salvo que `S_NORMAL_AP01_P_CODEC SWITCH` esté on; con eso, `RUNNING` pero `hw_ptr` no avanza (audio-wifi.md §1.6). En e5 el `start` hecho **antes** con la tarjeta registrada es lo que cambia el comportamiento (e5 §24.2 describe que, sin DSP, el FIFO sí avanza hasta que `VBC DAC0 DG Set` != 0; con DSP corriendo siguen sin llegar IRQ de período y hay que usar el hrtimer del parche 0013 o `NO_PERIOD_WAKEUP`). SUPUESTA la aplicabilidad a la Slide.

## 5. Plan ordenado de pruebas (NADA ejecutado)

Cada paso: qué, qué esperas, riesgo, criterio de parada. **Una Slide de pruebas con consola SSH abierta, `dmesg -w` en otra sesión, y asumiendo que puede reiniciar.** Hacerlo con alguien delante.

**P0 (lectura, sin riesgo): estado base en frío.**
`rgslide-audio-prep` (solo modprobe; docs/audio-wifi.md §4 y agent-audio.md lo ejecutaron sin panic) y esperar a `cat /proc/asound/cards` ≠ "no soundcards". Anotar `ls /sys/devices/platform/audiocp_boot` (deben aparecer `agdsp ldinfo start stop status`), `od -An -tu4 -N12 status` y `dmesg | grep -c 'wake up dsp'`.
Parada: si no aparecen los atributos, el problema es del probe de `sprd_audcp_boot`, no del DSP.

**P1 (lectura): confirmar `ldinfo` y tamaño.** `od -An -tu4 -j32 -N8 ldinfo` debe dar `0xafa00000` / `6291456` (ya medido por GammaOS). Comprobar `wc -c` de `l_agdsp_a` ≤ room. Riesgo nulo.

**P2 (riesgo bajo-medio, único paso que toca el DSP): `start` con la receta de e5 y observar.** Es lo que `scripts/agdsp-probe.sh` ya hace con `RGSLIDE_OK=1`. Condición previa: tarjeta registrada. Secuencia exacta: `echo 1 > stop` → UN `dd bs=4096` a `agdsp` → `echo 1 > start`. Medir **a 0, 0,2, 2 y 8 s** (lectura de debugfs regmap solo, sin tocar 0x56xxxxxx): PMU `0x3d0 0x3d4 0x818 0xb88 0xb98 0xba8 0x544 0x850 0x860` y AON `0x78 0x140 0x144 0x14c` (los mismos de agent-audio.md). **Criterio de éxito (SUPUESTO, razonado por el código de `start_store`)**: tras `start` cambia al menos `0xb88`/`0xb98`/`0xba8`/`0x140` respecto a los valores en reposo. Si no cambian: hipótesis (a) de agent-audio.md (bootprotect AON `0x78 = 0x80009620` bloquea). Riesgo: e5 y mu300 observan que escribir los registros de reset con el dominio sin alimentar reinicia el equipo (por eso P0 antes). Una vez hecho, **no repetir `start` sin reiniciar** salvo que P2 haya salido bien.

**P3 (riesgo bajo): ¿responde el DSP?** Sin abrir PCM de reproducción. Mirar `dmesg` por `aud_send_cmd out,cmd` y por `failed to get command` y `aud_smsg_recv cache is empty`. Si el DSP responde, el tráfico de comandos al abrir el control (p. ej. leer `amixer cget name='DSP VBC Profile Select'` con `agdsp_access_en`=on) debería producir `aud_send_cmd`. Esta es la prueba más barata de "DSP vivo"; el kernel de la Slide es el mismo `audio-sipc` que el E5 (misma cadena `aud_send_cmd`; SUPUESTA que el texto coincide en 5.4, comprobar con `strings audio_sipc.ko | grep aud_send_cmd`).

**P4 (riesgo medio): playback acotado con el programa propio (`tools/pcmprobe/sine.c`), no `aplay`.**
- Elegir PCM por nombre (no por índice): `FE_NORMAL_AP01` o `FE_FAST`.
- Antes: `HW_REFINE` para la tabla de periodos del FE elegido.
- Controles mínimos (e5 `HiFi.conf` verbo, con valores del E5; los de la Slide pueden diferir): `S_NORMAL_AP01_P_CODEC SWITCH`=on, `agdsp_access_en`=1, `VBC DAC0 DG Set`=39,39 (por defecto es 0 = silencio digital, CITADA e5 §24.2), `VBC_IIS_MST_WIDTH_SET`=`MST_WD_16BIT`, `ag_iis0_ext_sel_v2`=`aud_4ad_iis0_da0`, `Inter PA Config`=303 (el valor es del aw87xxx/placa del E5; **no copiar**: dejar el de GammaOS), codec: `DA AOR Switch`, `AO Mixer AOL/AOR Switch`; altavoz: `Speaker Function`=on, `Speaker Mute`=off (MEDIDO: el log de GammaOS indica PA externo `ucp1301` y gpio; no sabemos si hace falta `Speaker Function` solo).
- Buffer ≤ 96000 B y periodo 48000 B (ya validado sin panic en audio-wifi.md §1.5). Usar `NO_PERIOD_WAKEUP` (flag `SNDRV_PCM_HW_PARAMS_NO_PERIOD_WAKEUP` en hw_params) y alimentar a tasa fija, igual que el HAL de Android (CITADA e5 §24.3, "Android never sees this because its HAL opens the PCM with `PCM_NOIRQ`"). **Esta es la prueba más importante: separa "DSP parado" de "IRQ de período no llega".** Si con `NO_PERIOD_WAKEUP` el `hw_ptr` (leído por `SNDRV_PCM_IOCTL_STATUS`/`SYNC_PTR`) avanza, el DSP corre y falta solo un tick de período.
- Parada: cualquier `Unable to handle kernel` o consola colgada → reiniciar y NO insistir. Duración máxima 3 s por intento.

**P5 (riesgo medio): sonido.** Si P4 avanza: tono de 440 Hz, volumen mínimo (DG 10,10) y subir; comprobar el amplificador `ucp1301` (i2c `0x58`, reset-gpio 0x78) con `i2cdetect` solo lectura antes (SUPUESTA que el módulo `snd-soc-sprd-pa-ucp1301` lo configura al activar `Speaker Function`; no está verificado).

**P6 (riesgo medio-alto): captura.** Solo si P4 funcionó.
1. `FE_CAPTURE_DSP` (por nombre), mono, S16_LE, 48000 Hz.
2. **Buffer ≤ 7680 B (3840 B × 2 periodos)**; `HW_REFINE` antes. Nunca más (§4.1).
3. Controles (lista de §3, valores del E5 como punto de partida): `S_CAPTURE_DSP_CODEC SWITCH`=on, `ag_iis1_ext_sel_v2`=`aud_4ad_iis0_ad0`, `VBC_MUX_ADC0_IIS_PORT_SEL`=`VBC_IIS_PORT_IIS1`, `VBC ADC0 DG Set`=17,17, `ADD0 DATA SEL`=`ADD0_DATA_MIC13` (**probar MIC13, luego MIC12/MIC23**; ver §3.1), `ADC LRCLK Select`=`invert`, `ADC_1 Gain ADC_1 Capture Volume`=6, `ADC_1 Mixer MIC1PGA_1 Switch`=on, `AUD ADC0L/R Switch`=on, `Mic Function`=on.
4. Leer 1 s y calcular RMS por canal. En e5 el canal sin alimentar queda clavado en −32768 (CITADA §33.1): no confundirlo con "mic muerto".
5. Si el DMA no se mueve (`hw_ptr`=0, EIO): faltan `ag_iis1_ext_sel_v2` o el `VBC_MUX_ADC*` (e5 §33.1: "With the codec side alone both capture front ends streamed and returned EIO: the DMA never moved"). No es motivo de pánico.
6. Probar con un tono emitido por el altavoz (e5 usa esto como prueba de oído-libre: un tono de 440 Hz y análisis de Goertzel de la captura, e5 §34).
- Parada: si `FE_CAPTURE_DSP` da panic al HW_PARAMS, no reintentar sin parchear `sprd-dmaengine-pcm` (no tenemos fuentes de 5.4: ver "Siguiente").

**P7 (solo con mic jack): `HPMIC`** si el mic interno no existe: `HP Mic Function`=on, `HPMICPGA_1 Switch`=on, `HEADMICBIAS`; hacerlo con un headset conectado (el jack ya está registrado).

## 6. Alternativa que no depende del AGDSP: audio USB
La Slide tiene `CONFIG_SND_USB_AUDIO=y` en el kernel de fábrica (MEDIDA, `dump/live/config.txt:3571`). Un DAC/micro USB por el USB-C funciona como audio clase estándar; base618 y mu300 lo usan (CITADA `mu300-mainline.config` `CONFIG_SND_USB_AUDIO=m`; base618 `docs/hardware.md` "USB DAC / headsets"). SUPUESTA: da sonido y mic sin tocar el AGDSP (hay que comprobar el modo host del USB-C en la Slide y que no se cuelgue al desconectar: base618 anota un `BUG_ON` de `sprd_dma` en musb al parar un stream USB; no aplica al `dwc3` de la Slide, que es otro controlador: SUPUESTA). Es la vía de menor riesgo para tener micrófono "ya".

## 7. Lo que no se pudo cerrar
- No hay acceso a las fuentes del kernel 5.4.233 de la Slide (solo `.ko` desensamblados). El parche 0013 del E5 (hrtimer para `snd_pcm_period_elapsed`) y 0011/0016/0017/0019/0020 requieren fuentes; **SUPUESTA** la portabilidad.
- Falta `audio_route.xml`/`audio_params` de la Slide (no están en vendor_a): para saber qué micro es el interno y los valores de ganancia haría falta leer la partición ODM/product por Android (GammaOS), o `tinymix` durante grabación en GammaOS. Es la información que más ahorra pruebas arriesgadas; **recomendada antes de P6** (de solo lectura: `tinymix` en GammaOS mientras graba, como hizo e5, FINDINGS §24.2/§33).
- No se verificó que `agdsp_access` (5.4) haga lo mismo que `agdsp_pd` (5.15).
- No se pudo descargar `sprd_audcp_boot.c` ni `audio-sipc.c`.

## 8. Referencias (todas leídas)
- https://github.com/sprd-linux-devs/e5-linux (README, `docs/FINDINGS.md` §24-§34, `rootfs/overlay/opt/e5/e5-audio-dsp`, `rootfs/overlay/usr/share/alsa/ucm2/sprdphone-sc273/HiFi.conf`, `kernel/patches/0010`, `0011`, `0016`)
- https://github.com/beebono/rg-rotate-linux/blob/main/DEVICE-BRINGUP.md (UMS512)
- https://github.com/thwonp/base618 (`docs/hardware.md`, `docs/deep-sleep.md`, `docs/firmware.md`)
- https://github.com/dikeckaan/mu300-linux (UMS9620; audio retirado)
- https://gist.github.com/macromorgan/a1ba2fefc0f63ad6d6557d0cd819778e y https://gist.github.com/TheGammaSqueeze/b531bdb68fea9ec9fbf85475db1cb7d4 (DTS Android del RG556 = misma familia UMS9620, mismas claves `audiocp_boot`)
- https://github.com/archanaberry/advan-vxtux-6.18.52 (lista `sprd_audcp_boot`; fuente no accesible)
