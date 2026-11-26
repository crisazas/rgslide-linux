# AGDSP: qué hacen start/stop, qué es PMU 0x544 y si el DSP corre (2026-11-26)

Etiquetas: **MEDIDO** (en el PC o leído de la Slide sin tocar nada), **CITADO** (fuente externa), **SUPUESTO** (inferencia).
Alcance: estático en el PC + lecturas de `ssh` (journal, `/proc/interrupts`, `/dev`). No se escribió nada en la Slide. Desensamblados en `/tmp/opencode/aud/{ab.s,aa.s,sipc.s,dd.s}` (`aarch64-linux-gnu-objdump -dr` sobre `dump/live/dlkm/*.ko`).

## 1. `stop_store` y `start_store` (sprd_audcp_boot.ko @0x6d0 / @0x810) - MEDIDO

`regmap_update_bits_base(map, reg, mask, val, ...)`: `w2`=mask, `w3`=val. `mvn w3,w2` = **limpiar** los bits de la máscara; `mov w3,w2` = **poner** los bits. Ningún retorno se comprueba y no hay esperas.

Campos (orden de `.rodata+0x660`, struct: regmap en `+0..+88`, reg en `+240..`, máscara en `+300..`) y DT vivo (`audiocp_boot`, syscon 0x04 = PMU APB `0x64910000`, 0x02 = AON APB `0x64900000`):

| campo | syscon / reg | máscara | significado |
|---|---|---|---|
| sysshutdown | PMU 0x3d0 | 0x2000000 (b25) | apagado automático del sistema AUDCP |
| coreshutdown | PMU 0x3d4 | 0x2000000 (b25) | idem del core DSP |
| deepsleep | PMU 0x818 | 0x8 (b3) | deep sleep forzado de AGCP |
| corereset | PMU 0xb88 | 0x1 | reset del core DSP |
| sysreset | PMU 0xb98 | 0x400000 | reset del sistema AUDCP |
| reset_sel | PMU 0xba8 | 0xffffffff | selector de reset (por software) |
| sysstatus / corestatus | PMU 0x544 | 0x1f00 / 0x1f0000 | estado de potencia (solo lectura) |
| sleepstatus | PMU 0x860 | 0xf000 | estado de sueño |
| bootprotect | AON 0x78 | 0xffff | **clave** de arranque, no un bit |
| bootvector | AON 0x140 | 0xffffffff | vector de arranque |
| bootaddress_sel | AON 0x144 | 0x1 | selecciona el vector anterior |

Fuera de `audiocp_boot`, de `agdsp-access`: PMU 0x850 b3 (`audcp_pmu_sleep_ctrl`) y AON 0x14c b5 (`ap_access_ena`).

**stop_store** (offset de `agdsp_store` a 0), en orden:
1. reset_sel &= ~0xffffffff (→ 0)
2. corereset limpiar; 3. sysreset limpiar (pulso bajo)
4. deepsleep poner; 5. sysshutdown poner; 6. coreshutdown poner
7. corereset poner; 8. sysreset poner

Estado final = DSP **parado y en reset**. Cuadra con lo medido tras stop: 3d0=03010103 (b25), 3d4=03010301, 818=0001002c (b3), b88=1, b98=0x400000, ba8=0.

**start_store**, en orden:
- `memset_io` a 0 de: IRAM AON `0x65006400` (solo si `dsp-reboot-mode`=0), ring smsg `0xaf980400` y params `0xaf980000`.
1. reset_sel → 0
2. corereset poner; 3. sysreset poner
4. sysshutdown limpiar; 5. coreshutdown limpiar; 6. deepsleep limpiar
7. bootprotect: `AON 0x78[15:0] = 0x9620`
8. (solo si modo=0) bootvector: `AON 0x140 = (0xafa00000 + 0x80) >> 1 = 0x57d00040`
9. bootaddress_sel poner (`AON 0x144 |= 1`)
10. corereset **limpiar**; 11. sysreset **limpiar** (se sueltan al final)

El propio probe imprime `dsp reboot by DDR!`: no hay propiedad `dsp-reboot-mode` → modo 0 → pasos 8 y la memset de IRAM sí se ejecutan.

**Respuesta a la pregunta 1**: `start` es la secuencia inversa de `stop` (suelta apagado, deep sleep y, al final, los dos resets), y deja los registros iguales a los de antes del stop. **No deja el reset asertado**; sobre el papel el DSP queda corriendo. Verificación con la medida de hoy:
- tras start: 3d0=01010103, 3d4=01010301, 818=00010024, b88=0, b98=0, ba8=0 (el LK dejaba ffbfffff; el driver escribe 0) - MEDIDO.
- **AON 0x140 = 0x57d00040 = (0xafa00000+0x80)>>1** exacto - MEDIDO. Es la prueba de que `start_store` se ejecutó entero y de que la escritura en AON **sí surte efecto**.
- AON 0x78 queda `0x80009620`: los 16 bits bajos ya valían 0x9620 (clave que el LK ya había puesto) y el bit 31 no está en la máscara (ver §3).

## 2. PMU 0x544 = 00070700 y `status` - MEDIDO (asm de agdsp_access.ko y sprd_audcp_boot.ko)

- 0x544 es el estado de potencia del dominio AUDCP. DT `agdsp-access`: `audcp_pmu_pwr_status4 = <PMU 0x544 0x1f00>` (campo sys = bits 12:8) y `audcp_pmu_pwr_status3 = <PMU 0x544 0x1f0000>` (campo core = bits 20:16). 0x00070700 → sys=7, core=7. `status_show` imprime esos mismos campos desplazados (más 0x860[15:12]) → `07 00 00 00 07 00 00 00 00 ...`. Por tanto `status` **es** PMU 0x544 y no un registro del DSP.
- 7 = dominio apagado, 0 = encendido (CITADO e5 FINDINGS §24.7 y `e5-audio-dsp`: "0 means power up finished, 7 means power off... a loaded, healthy, idle DSP reads 7 exactly like one that never started").
- `agdsp_access_enable` (@0x7b4): pone AON 0x14c b5, envía **un mensaje mailbox (valor 100)** al DSP, espera con `udelay` a que 0x544[12:8]==0 y, si no llega, imprime `wait agdsp power up timeout` (-16). **0 líneas de timeout en los 3 arranques del journal, 24 pares "wake up dsp / done"** → el PMU enciende AUDCP bajo demanda, haya o no firmware corriendo (CITADO e5: "NOT a test of the firmware"). Que 0x544 vuelva a 7 en reposo es lo esperado.
- `agdsp_can_access` (@0x5d4, solo lee PMU/AON, sin tocar AGCP): exige AON 0x14c b5=1, PMU 0x850 b3=0, 0x860[15:12]==6, y 0x544 sys==0 y core==0. En reposo falla en el primero (0x14c=0), por eso es seguro llamarlo siempre.
- **Conclusión §2**: 0x544=7 con `0x14c=0` es el estado normal "AUDCP durmiendo"; **no distingue DSP corriendo de parado**. Lo que sí es prueba de vida es tráfico SIPC con respuesta (§4).
- Cómo comprueban "el DSP vive" los demás: e5-linux (`dsp_alive`) busca en dmesg `aud_send_cmd out,cmd` sin `failed to get command` tras abrir un PCM (CITADO; la línea "out" solo se imprime si el DSP respondió). base618: espera `0xb8 == 7` (PMU, estado sys de UMS512) para dormir el AGDSP, no para probar vida (CITADO deep-sleep.md). rg-rotate-linux (UMS512): "AP↔AGDSP IPC round-trips" y "both AGDSP power domains reach full wake-lock" (CITADO DEVICE-BRINGUP.md). Ninguno lee un registro del DSP.

## 3. AON 0x78 bit 31 - MEDIDO / SUPUESTO

- DT: `bootprotect = <AON 0x78 0xffff>`: el driver solo toca [15:0] y escribe **0x9620** (el número del chip UMS9620: parece una clave de desbloqueo, no un flag). El bit 31 no está en la máscara: no es de este driver. SUPUESTO: lo pone el LK o el hardware; no hay ninguna cadena `bootprotect`/`protect` en el log del LK (`dump/live/test-*/uboot_log.txt`), ni en `dmesg-gamma-now.txt` (empieza a 4.2 s, con el DSP ya en marcha), ni en los `.ko`.
- **Hipótesis "el bit 31 hace ignorar escrituras de arranque": REFUTADA** por la propia medida de hoy: AON 0x140 pasó de 0x2b410000 a **0x57d00040** (= (0xafa00000+0x80)>>1, valor exacto que calcula el probe) con el bit 31 puesto, y PMU 0x3d0/0x3d4/0x818/0xb88/0xb98/0xba8 cambian en stop y vuelven en start. Las escrituras llegan.
- **Lo que revela 0x140**: antes de `start` valía 0x2b410000 → vector 0x56820000 (IRAM de AGCP, `iram_dsp_base=0x56800000`), distinto del que escribe el driver (0xafa00080, DDR). Es decir, **el LK ya había arrancado el AUDCP por su cuenta** (SUPUESTO sobre el mecanismo; MEDIDO el valor distinto).
- MEDIDO en el LK (`uboot_log.txt`, todos los arranques): `load l_agdsp to addr 0xafa00000, size = 0x600000`, `partion l_agdsp, verify_ret:0x0` y, justo antes de saltar al kernel, **`audio cp boot ok!`**. Se repite en 10/10 logs. Y en Android (`dump/live/dmesg-gamma-now.txt`) el primer `aud_send_cmd` con respuesta sale a 14.83 s sin ningún `start` previo visible en el kernel.

## 4. ¿Corre el DSP? Evidencia

**4.1 El DSP respondió por SMSG ANTES de que nadie ejecutase `start`** - MEDIDO (journal del arranque actual, `journalctl -k -b 0 -o short-monotonic`):
```
264.905960 aud_send_msg,cmd: 0x21, value0: 0x1, value1: 0xaf700000     (AP -> DSP, canal 4, SBLOCK_INIT)
264.910916 aud_recv_cmd, chan: 0x4, cmd: 0x21, value0: 0x1, value1: 0xaf700000   (DSP -> AP, respuesta)
264.911158 aud_send_cmd_no_param out,channel = 4 cmd =33 ret-value:0,repeat_count=1
264.922850 aud_send_cmd_no_param out,channel = 3 cmd =33 ret-value:0,repeat_count=1   (idem canal 3, 0xaf711000)
264.959675 sprd_audcp_boot: dsp reboot by DDR!    <- es el probe de audiocp_boot, aún sin start
```
`aud_send_cmd_no_param` (audio_sipc.ko @0xe08) solo imprime "out ... repeat_count=1" si `aud_recv_cmd` (lee el ring RX, donde escribe el DSP) devolvió un mensaje con el mismo canal y comando; e5 `dsp_alive` usa exactamente esa línea (CITADO). `aud_send_cmd_no_param` vacía el RX antes de enviar y reintenta 3 veces: aquí `repeat_count=1` = respondió a la primera, en ~5 ms. Es decir, **el AUDCP corría la imagen de `l_agdsp` y atendía el protocolo sblock a 264.9 s, sin `start` nuestro**. Cuadra con: LK "audio cp boot ok!" (§3) y AON 0x140 = vector de IRAM antes de `start` (§3). Ocurre en el boot actual (2 líneas "out"); los dos boots anteriores no cargaron audio (0 líneas) y no cuentan.
Salvedad (SUPUESTO): el ring RX podría tener escritor distinto del DSP; improbable (ring dedicado `0xaf980400`, destino dst=1 = AGDSP).

**4.2 Qué pasa con nuestro `stop`/`start` (283 s)** - MEDIDO + SUPUESTO:
- MEDIDO: `start_store` hace `memset_io(0)` del ring smsg (`0xaf980400`) y de params (`0xaf980000`), reinicia el core y suelta resets. Tras él, 0 líneas `aud_send_cmd`/`wait dsp` en dmesg y mailbox outbox (IRQ 82) sube de 60 a 69 en 1,5 h (otras fuentes también la usan; no atribuible).
- SUPUESTO (la causa más probable del "silencio"): **nadie envía tráfico**. Los canales sblock (3, 4) solo envían SBLOCK_INIT **una vez**, al cargar los módulos (264.9 s); tras reiniciar el core y borrar los rings, el DSP vuelve a esperar ese init y el AP no lo repite. Silencio de smsg ≠ DSP parado. Además el `start` borra los rings que el AP tenía ya inicializados (rd/wr ptr en `ipc->…`), así que tras él el AP y el DSP pueden estar desincronizados aunque el core corra.
- MEDIDO: el registro de potencia (0x544=7) y el bit AON 0x14c (=0) no dicen nada: están así en reposo hasta que alguien pide acceso (§2).
- No se puede afirmar que el DSP corra *tras* nuestro start: **no se sabe**. Solo se sabe que corría antes.

**4.3 Lo que NO es una prueba**: `status` (§2), `Failed to open channel 0,dst=0,rval=-19` (es el AP mismo, dst 0), ausencia de `aud_send_cmd` (nadie manda), `enable_count` 0 de los relojes `audcp-*` (nadie los pide hasta abrir PCM; CITADO e5 §24.2).

## 5. Pruebas (de más segura a menos)

Seguras / solo lectura, ya hechas o repetibles sin tocar nada:
1. `journalctl -k -b 0 -o short-monotonic | grep -E 'aud_send_cmd_no_param out|aud_recv_cmd, chan'` en un arranque frío con los módulos cargados y **sin start/stop**: dos líneas "out" = DSP vivo (HECHO, positivo).
2. `grep -E 'wait agdsp power up timeout|failed to get command|timeout and reset dsp|peter: dsp assert' ` en dmesg/journal: 0 coincidencias = sin señales de DSP muerto/asertado (HECHO, 0; "dsp assert" lo imprime `aud_smsg_irq_handler` cuando el DSP vuelca un assert por SMSG).
3. `cat /proc/interrupts | grep mailbox` antes/después: solo orientativo.
4. NO hay `/sys/kernel/debug/…audio-smsg-sipc`: `aud_smsg_init_debugfs` existe (audio_sipc.ko @0x3da4, muestra wr/rd de los rings, "channel[%d] states") pero no está referenciada en el probe (sin fichero en `/sys/kernel/debug`, MEDIDO por `find`). Tampoco `/dev/mem` (`# CONFIG_DEVMEM is not set`, MEDIDO): `devmem` no es posible.
5. `/dev/audio_dsp_log` NO es de solo lectura: `open`/`ioctl` envían cmd 0x24/0x20 al DSP (`aud_send_cmd_no_wait`). Nada de ello es una lectura pasiva; no usarlo sin decidir.

**Prueba de bajo riesgo recomendada (siguiente paso)**, una sola vez, en frío, sin `start`/`stop`, con consola abierta y alguien delante:
- Arranque frío, cargar módulos (`rgslide-audio-prep`, sin `RGSLIDE_AGDSP_BOOT`), comprobar §5.1 (2 líneas "out").
- Pedir acceso con el control ALSA `agdsp_access_en`=1 (lo que hace el HAL; solo toca AON 0x14c b5, manda un mensaje mailbox y sondea PMU 0x544) y **leer PMU 0x544 por debugfs regmap** (`dummy-syscon@64910000/registers`): debe pasar de 0x00070700 a sys=0/core=0 y `agdsp_can_access` dar 1. Sin `wait agdsp power up timeout` → dominio y DSP responden a la petición.
- Un `amixer cget`/`cset` de `DSP VBC Profile Select` provoca un `aud_send_cmd` con espera: aparece `aud_send_cmd out,cmd` si el DSP contesta (e5 P3; CITADO). No abrir PCM hasta que eso salga.
- Parada: cualquier `Unable to handle kernel`, `SError`, `failed to get command` repetido o consola colgada → reiniciar y no insistir.

Sigue sin recomendarse: repetir `stop`/`start` (borra los rings y deja al DSP esperando un init que nadie envía); si hace falta reiniciar el DSP, hacerlo **antes** de cargar `audio_sipc`/`audio_dsp_dump`/`audio_pipe` (SUPUESTO: así el init sblock llega a un DSP recién arrancado; no probado).

## 5b. Prueba P3 ejecutada con autorización del usuario (2026-11-26, arranque en frío, sin start/stop, sin abrir PCM) - MEDIDO
Herramienta: `/tmp/ctl2.py` en la Slide (ioctl crudos de `/dev/snd/controlC0`; no hay `amixer`). Resultado:
- Tras `rgslide-audio-prep`: tarjeta `sprdphonesc2730` registrada y 2 líneas `aud_send_cmd_no_param out` (canales 3 y 4) = el DSP contestó. 0 panics, 0 `power up timeout`.
- `agdsp_access_en`=1 (numid 88): **PMU 0x544 pasa de 00070700 a 00000000** (sys=0, core=0) y vuelve a 00070700 al soltarlo (=0). Es la prueba directa de que el handshake de acceso funciona y el dominio AUDCP se enciende. 15 pares `wake up dsp/done`, 0 timeouts.
- `DSP VBC Profile Select` (numid 98) = 0 al leer; escribir 961216512 (0x394b0000, el valor "load-bearing" de rg-rotate-linux) llega a `vbc_profile_put` sin error, pero no genera `aud_send_cmd out`.
- `DSP VBC Profile Update` (numid 94) lanza `vbc_profile_loading` y el kernel pide el **firmware `dsp_vbc`**, que NO está en `/lib/firmware` de la Slide: `Direct firmware load for dsp_vbc failed with error -2`.
- Conclusión corregida: **el DSP está vivo y accesible**. Lo que falta no es arrancarlo, sino el estado de mixer y los perfiles de audio (firmware `dsp_vbc` y compañía). `agdsp-probe.sh` (stop/start) NO hay que repetirlo.
- Encontrado el material de Android: `odm_a` extraído de `super.img` (lpunpack) en `/tmp/opencode/odm/odm_a.img`, con `etc/audio_route.xml` (micro principal: `ADD0 DATA SEL`=`ADD0_DATA_MIC13`, `ADC_1 Mixer MIC1PGA_1 Switch`, `Mic Function`) y `etc/audio_params/sprd/{dsp_vbc,audio_structure,codec,cvs,audio_pga}.xml`. `dsp_vbc.xml` es XML (689 KB, `num_mode=0x48`, `struct_size=0x6c4`); el kernel pide un binario `dsp_vbc`, que el HAL de Android genera a partir de ese XML.

## 5c. Firmware `dsp_vbc` generado y aceptado por el kernel (2026-11-26) - MEDIDO
- Formato deducido del desensamblado de `vbc_profile_loading`/`vbc_profile_put` (`snd-soc-sprd-vbc-v4.ko`): 16 B `audio_profile`+NUL (se compara con `strncmp(...,16)`), u32 `num_mode`, u32 `struct_size`, y `num_mode*struct_size` bytes. El XML de Android (`dsp_vbc.xml`: 72 modos x 1732 B) trae cada campo con su `id` = desplazamiento absoluto; los u32 con hijos son campos de bits.
- `tools/vbcprofile/xml2bin.py` lo genera (124704 B de datos + 24 de cabecera; cobertura exacta de los 72 modos) y `scripts/install-dsp-vbc.sh` lo instala en `/lib/firmware/dsp_vbc` de la Slide.
- Resultado: `DSP VBC Profile Update`=1 (con `agdsp_access_en`=1) -> `vbc_profile_loading, return 0`, datos reservados, sin errores ni panic; PMU 0x544 vuelve a 00070700 al soltar el acceso.
- SUPUESTO: el kernel no valida el contenido de los campos, solo cabecera y tamaño; si el DSP rechazara el contenido lo veriamos como `aud_send_cmd` fallido al enviar el perfil (`vbc_profile_put` -> `aud_send_block_param`), aun no probado.
- Falta: `audio_structure` (ídem, `Audio Structure Profile Update`), el estado de mixer de `audio_route.xml` y abrir el PCM de captura con buffer <= 7680 B.

## 5d. Captura del micrófono funcionando por el DSP (2026-11-26) - MEDIDO
- Recorrido: `rgslide-audio-prep` -> `agdsp_access_en`=1 -> `DSP VBC Profile Update` y `Audio Structure Profile Update` (firmware generado con `xml2bin.py`) -> mixer de `audio_route.xml` (main_mic: `ADD0 DATA SEL`=`ADD0_DATA_MIC13`, `ADC_1 Mixer MIC1PGA_1`, `Mic Function`, mas `S_CAPTURE_DSP_CODEC SWITCH`, `ag_iis1_ext_sel_v2`=`aud_4ad_iis0_ad0`, `VBC_MUX_ADC0_IIS_PORT_SEL`=`VBC_IIS_PORT_IIS1`, `VBC ADC0 DG Set`=17 17, `ADC LRCLK Select`=invert, ganancia 6) -> `FE_ST_CAPTURE_DSP` (`pcmC0D4c`), mono S16_LE 48 kHz, buffer 7680 B.
- El DSP contesta a los comandos de captura (`aud_send_cmd out` cmd 4, 6, 5 con `ret-value:0`).
- Con `NO_PERIOD_WAKEUP` el DMA avanza (`hw_ptr` +4800 B cada 100 ms) y `cap` lee por `STATUS` en vez de bloquear en `READI`. Sin ese flag no llegan las IRQ de periodo y `READI` da EIO o se cuelga (rc=124): mismo comportamiento que el HAL de Android (`PCM_NOIRQ`).
- Medido en 20 s con alguien hablando y dando palmadas: reposo RMS ~31; 11 de 200 ventanas de 100 ms > 5x el reposo; pico RMS 338, pico absoluto 2154; DC -30. 0 panics. PMU 0x544 baja a 0 durante la captura y vuelve a 00070700.
- SIN CONFIRMAR POR PERSONA: que lo grabado sea la voz (el RMS de reposo coincide con ruido electrico). El micro principal es MIC13; si no se oye, probar `ADD0_DATA_MIC12` (aux_mic1) y `ADD0_DATA_MIC23`.
- Uso: `scripts/install-audio-mic.sh` (desde el PC con el ODM montado) y `rgslide-audio-mic on|off|test [s]` en la Slide. No hay servicio ni integracion con el menu de Ajustes todavia, y el altavoz (reproduccion) no se ha probado.

## 5e. Reproducción por el altavoz: el DMA avanza (2026-11-26) - MEDIDO / SIN CONFIRMAR POR OIDO
- Ruta (`audio_route.xml` speaker + lo medido): `S_NORMAL_AP01_P_CODEC SWITCH`=1, `ag_iis0_ext_sel_v2`=`aud_4ad_iis0_da0`, `VBC_IIS_MST_WIDTH_SET` y `VBC_IIS_TX0_WD_SEL`=`WD_16BIT` (TX0 venia en 24 bit), `DSP VBC Profile Select`=961216512, `VBC DAC0 AUD MDG Set`=4096 4096, `VBC_MUX_AP01_DSP_PLY`=`AP01_TO_DSP_ENABLE`, mixers HP (`HPL/HPR Mixer`, `HPL EAR Sel`=HPL, ganancia 1), `DA AOR Switch`, `AO Mixer AOL/AOR`, `DAC Gain`=8 8, `Speaker Function` y `Speaker1 Function`=1.
- Con eso `hw_ptr` avanza (~4800 frames cada 100 ms) con `NO_PERIOD_WAKEUP`, buffer 96000 B, sin panic.
- Peculiaridad MEDIDA: el primer PCM tras activar la ruta deja `hw_ptr` clavado (160-640); el segundo funciona (`rgslide-audio-spk test` hace un pase de cebado a amplitud 0).
- Causa real de un fallo posterior (MEDIDO): `rgslide-volume apply` soltaba `agdsp_access_en` (=0) al terminar y el DMA se quedaba clavado en 160. Corregido: `apply` solo suelta el acceso si no estaba ya activo. Con eso 4 de 4 pruebas avanzan.
- CONFIRMADO POR EL USUARIO (2026-11-26): con la ruta de abajo **se oye el tono de 440 Hz** por el altavoz. Antes, con `DAC Gain`=0 y sin `HP Virt Switch`, solo se oian chasquidos breves (el amplificador se encendia, `hook_general_spk gpio 176 on:1`, pero no pasaba senal). Lo que cambio: `HP Virt Switch`=1 (esta en la ruta `speaker` de Android) y `DAC Gain DAC Playback Volume`=1 1 (rango 0-2; Android no lo toca). No se aisló cual de los dos lo arregla (SUPUESTO: HP Virt).
- Sin tocar: `Inter PA Config` (2 por defecto), `HPL/HPR Gain`=1.
- Herramientas: `tools/pcmprobe/play.c` (`rgslide-play`), `rootfs/overlay/usr/local/sbin/rgslide-audio-spk on|off|test`, instalacion con `scripts/install-audio-mic.sh`.

## 5f. Volumen (2026-11-26) - MEDIDO
- `rgslide-volume get|set N|up|down|apply` (0-100 %) guarda el valor en `/var/lib/rgslide/volume` y lo aplica a `VBC DAC0 DG Set` (0-127) como `N*64/100` (tope de prudencia, SUPUESTO: sin medir SPL). Sube `agdsp_access_en` solo mientras escribe (el dominio dormido da abort).
- Medido: `set 30` -> DG 19 19 leido por ioctl; `up/down` de 5 en 5; `set 150` -> 100; PMU 0x544 vuelve a 00070700; 0 panics. `rgslide-ctl volume` y el deslizador de Ajustes ya lo usan; `rgslide-ctl status` muestra `Volume: 20`.
- `rgslide-audio-spk on` aplica el volumen guardado (ya no fija DG a 10).

## 5g. Audio en juegos y siseo del altavoz (2026-11-26) - MEDIDO / PERCIBIDO
- Camino de audio: ALSA `default` (`/etc/asound.conf`, plugin `file`) -> FIFO `/run/rgslide/audio.fifo` -> `rgslide-audiod` (anillo, PCM `pcmC0D2p` de 96000 B con `NO_PERIOD_WAKEUP`, reabre si `hw_ptr` se clava, llama a `rgslide-audio-spk on/off`). `retroarch-es.cfg` usa `alsa`/`default`. Unidades `rgslide-audio` y `rgslide-audiod` habilitadas.
- Probado con Adjustris (GB) y Thwaite (NES): el usuario oye el juego; `in`/`out` del demonio coinciden (0 descartes, 6-10 vaciados), 0 panics.
- Siseo ("estatica") CONFIRMADO POR EL USUARIO incluso con silencio digital, mientras el amplificador esta encendido (GPIO 176 on). Aislamiento uno a uno (percepcion humana, sin instrumento): DAC Gain 1->0: igual. HPL/HPR Gain 1->0: bajo. Inter PA Config 2->1: bajo. Las dos juntas: igual. Resultado inconsistente; NO se concluyo la causa. Con el amplificador APAGADO (sin PCM abierto) no se oye siseo (prueba F: "nada").
- Cambio hecho: `rgslide-audiod` solo abre el PCM y enciende el amplificador cuando llega audio con amplitud > 64; con 4 s de silencio digital seguido lo cierra y apaga la ruta. Asi el siseo solo suena mientras hay sonido.
- Fallo conocido (MEDIDO en prueba F): abrir el PCM tarda ~3 s (`hw_ptr clavado, reabro`) y el demonio descarta lo anterior (`ring_trim`), asi que un sonido de ~1 s (clic de menu) se pierde. En juegos solo se pierde el primer instante.

## 5h. Microfono verificado con una medida objetiva (2026-11-26) - MEDIDO
- Bucle altavoz -> aire -> microfono: el altavoz emite 8 s de 1000 Hz (por ALSA default -> audiod) mientras `rgslide-cap` graba por `FE_ST_CAPTURE_DSP`. Analisis de Goertzel por segundos: a 1000 Hz la amplitud pasa de 0,2-2 (resto de ventanas) a **1281, 1460, 1511 y 762** durante el tono; a 800 y 1300 Hz se queda en 0,1-2,5. RMS de reposo 33-46, con el tono ~1000. El canal de captura ve el tono, pero eso NO prueba que el microfono capte sonido del aire: el altavoz esta pegado al micro y el tono puede llegar por la estructura o por acoplo electrico del codec (SUPUESTO sin descartar).
- Comparacion de los tres microfonos (frotando la carcasa): `main` (MIC13/ADC_1) pico 188; `aux1` (MIC12/ADC_2) y `aux2` (MIC13/ADC_3) dan 0 exacto (sin senal): no hay micro conectado en esos canales o no se activaron bien. El unico util es `main`.
- CORRECCION: con ganancia ADC DG=60 y 20 s de voz (pitido de aviso, usuario hablando), el espectro por bandas (150-400, 400-1000, 1-3k Hz) queda plano en ~1 salvo un golpe a los 9,5 s: **no entra voz**. Reposo 29-33 RMS. Hasta localizar el agujero del micro la captura de voz NO esta confirmada.
- La voz del usuario no se pudo confirmar a oido en tres intentos (la reproduccion sonaba a "ruido o golpes" con picos de 1622): el nivel de voz que llega es bajo frente al ruido de fondo (ganancia ADC 6 de 7, DG 17). SUPUESTO: subir `VBC ADC0 DG Set` mejoraria; no probado.

## 5i. Microfono localizado: agujero del borde inferior, ruta main/MIC13 (2026-11-26) - MEDIDO
- El usuario indica que el agujero del micro esta en el **borde inferior**. Soplando continuo a ~2 cm durante 12 s por ruta (ADC DG=40): `main` (MIC13, ADC_1, `Mic Function`) mediana 1187, p90 2691, **pico 15617**, reposo(p10) 55; `hpmic` (HPMICPGA_1) mediana 7, pico 61; `aux` (MIC23, ADC_2) mediana 1, pico 2. **Solo `main` oye**, y oye fuerte: es el micro real y la ruta de `rgslide-audio-mic` es la correcta.
- Explica los intentos de voz anteriores: hablar lejos del agujero deja la voz por debajo del ruido de fondo (RMS ~30). Hay que hablar a 2-5 cm del borde inferior.
- `rgslide-audio-mic on` pasa a `VBC ADC0 DG Set`=40 por defecto (antes 17).

## 5j. Siseo del altavoz: lo que SI y lo que NO se midio (2026-11-26)
- **Calibracion de Android localizada** (`odm_a`, `etc/audio_params/sprd/*.xml`): `codec.xml` modo 46 `Music/Handsfree/Playback` trae `ao_playback_volume` 4/0/0/6.., `spkl_playback_volume` 0/3/6.., `hpl/hpr_playback_volume` 6, `dacl/dacr_playback_volume` 3, `dacs` 0, `iner_pa_config` (DTRI_F 7, DEMI 1, mode 0, EMI_rate 1; en el mixer = 0x07|0x08|0x20 = 0x2F... no confirmado como valor de `Inter PA Config`), y `UCP130X_1_Parameters`: `pa_config` AGC_EN 0, Mode 3, PGA_Gain 0x58, Class_D_F 0x0f, `RL`=0x1f40, `Power_Limit_P2`=0x3e8. `audio_pga.xml` `Music/Handsfree/Playback`: `VBC DAC0 DG Set`=0x19 (25); `Record`: `VBC ADC0 DG Set`=0x19; `UnprocessRecord`=0x11.
- **El amplificador digital ucp1301 NO esta inicializado** (MEDIDO): el I2C `6-0058` (`sprd,ucp1301-spk`, reset en GPIO global 184 = pin 120 del chip 64-319, que es el `reset-gpios = <&gpio 0x78>` del DT) falla en el probe con `read chip id high fail -5` (EIO) incluso con la ruta de altavoz encendida; no hay driver enlazado. Las lecturas `UCP130X` de Android (Mode, PGA_Gain, Power_Limit) nunca se aplican. `hook_general_spk gpio 176` NO es el reset del ucp1301 (176 es `RFFE7_SCK`, sin reclamar). `i2c-dev` no esta en el kernel (no se puede sondear el bus desde espacio de usuario). CAUSA SIN RESOLVER: falta saber que alimentacion/secuencia deja a ese chip responder (GammaOS no imprime nada del ucp1301 en su `dmesg`, tampoco lo sondea).
- **Con silencio digital el microfono NO capta siseo** (MEDIDO, 2 repeticiones por ajuste): RMS 22-37 con el altavoz apagado (26, 22) y con la ruta encendida (36, 24, 23, 28); ningun cambio de `HPL/HPR`, `DAC Gain`, `Inter PA Config`, `HP Virt Switch` ni `Speaker1 Function` se separa del ruido del propio micro (picos aislados de 58 y 73 no se repiten). Mis barridos anteriores de "ruido" estaban mal: `rgslide-play` genera 440 Hz y los medi a 1000 Hz, y el micro oye al altavoz por acoplo estructural.
- **Lo unico que hay es percepcion humana**: el usuario oye siseo continuo con el amplificador encendido, y le bajan por separado `HPL/HPR`=0 e `Inter PA`=1; ambos a la vez no. NO se concluyo la causa. Una medida objetiva del siseo necesita otro microfono o loopback, no el interno.

## 5k. CAUSA del mal sonido: perfil del DSP equivocado (2026-11-26) - CONFIRMADO POR EL USUARIO
- `DSP VBC Profile Select` lleva el modo en los bits 31-24 (lo imprime el kernel: `current_offset`). Yo usaba 961216512 = 0x394b0000 = **modo 57**, que en `dsp_vbc.xml` es `Audio/Hac/WB2` (perfil de llamada para audifonos), copiado de rg-rotate-linux (otro SoC y otro XML).
- El perfil correcto para musica por el altavoz es **modo 64 = `Music/Handsfree/Playback`** -> 0x404b0000 = 1078657024 (coincide con el valor citado del E5). El kernel lo acepta (`current_offset=64, mode_max_offset=72`).
- Con el modo 64, Thwaite (NES) suena "mucho mejor" segun el usuario. Los ajustes analogicos (`Inter PA`, `HPL/HPR`, `DAC Gain`) no eran la causa.
- Nota: el demonio acumulo 40658 frames en el anillo en la ultima ventana (el juego produce un poco mas rapido que el DAC); `ring_trim` lo recorta a LAT al pasar de 3xLAT.

## 5l. Microfono con voz real (2026-11-26) - MEDIDO + PERCIBIDO
- Con `VBC ADC0 DG Set`=25 (Android) y la voz a 2-3 cm del agujero del borde inferior: reposo RMS 18, voz 133-1354 RMS, bandas 150-400/400-1000/1-3k Hz 20-80 veces sobre el fondo, pico 24648 sin saturar. La voz SI entra en el micro.
- Reproducida por el altavoz (perfil modo 64, ganancia x5,3 con limitador): el usuario dice "se intuye con ruido". Es decir: captura funcional, inteligibilidad baja. SUPUESTO: el DSP no aplica el perfil de grabacion (`Music/Handsfree/Record` no existe en `dsp_vbc.xml`/`audio_structure.xml` de este ODM; el `audio_structure` sigue en modo 0, de llamada) y el altavoz pequeno a ganancia alta degrada; no aislado.

## 5m. El ucp1301 no interviene: el altavoz usa el amplificador interno del codec (2026-11-26) - MEDIDO en el DT
- `codec@1000 { hp-use-inter-pa = <1>; ... }` (`live.dts` l.3287): el altavoz va por la ruta HP del codec con `Inter PA`. El phandle del `ucp1301` (0x24a) no se referencia desde ningun dai-link ni nodo `sound@0`. Que el chip no responda por I2C (`-5`) es irrelevante para el sonido.
- Banco de pruebas objetivo: `tools/pcmprobe/seqplay.c` (secuencias de tonos sobre un PCM abierto una vez) + `rgslide-cap` (micro) + Goertzel; respuesta altavoz->aire->micro con el perfil base: SNR 55 dB a 1 kHz, 45 dB a 2 y 5 kHz, 36 dB a 3 kHz, 32 dB a 600 Hz, **21 dB a 300 Hz**; THD 8 % a 1 kHz. (El micro y el altavoz estan pegados: es una medida relativa, no absoluta.)

## 5n. PELIGRO medido: DROP+PREPARE sin cerrar el PCM cuelga la Slide (2026-11-26)
- `tools/pcmprobe/openprobe.c` modo 1 (si `hw_ptr` se atasca tras `START`, hacer `SNDRV_PCM_IOCTL_DROP` + `PREPARE` sin cerrar el fd) **reinicio la Slide entera** (cuelgue sin panic en `pstore`; salio solo, 139 s de uptime despues; todos los servicios volvieron). NO repetir. Recuperar un PCM atascado se hace SOLO cerrando (`DROP` + `HW_FREE` + `close`) y reabriendo, que es lo que ya hace `rgslide-audiod`.
- La primera apertura del PCM tras `rgslide-audio-spk on` se atasca siempre (3 de 3; con espera de 0, 4 y 10 s tras el `on`: da igual) y la segunda arranca: el kernel registra lo mismo en las dos (solo cambian direcciones). Causa sin resolver; la mitigacion (cebar con una apertura de amplitud 0) funciona.

## 5o. Orden obligatorio: ruta del altavoz ANTES de abrir la captura (2026-11-26) - MEDIDO
- Con `seqplay` (tono 1 kHz) y la captura por `FE_ST_CAPTURE_DSP`: (1) ruta del altavoz encendida -> captura abierta -> sonido: la captura ve el tono (704-774 de amplitud a 1 kHz). (2) captura ya abierta y la ruta se enciende despues: la captura queda muda (0). (3) captura sin la ruta del altavoz: soplar en el micro si entra (RMS 400-3700 en la serie A de `blow.sh`), pero en cuanto se enciende la ruta con la captura ya abierta el RMS cae a ~25 (silencio). Con la ruta encendida desde antes, un soplo da RMS 40-60: **mucho menos** que sin ruta.
- Conclusion: encender la ruta del altavoz cambia el enrutado del DSP/ADC mientras hay una captura abierta y la deja sorda; ademas atenua el micro. Por eso `rgslide-audio-mic on` enciende primero la ruta del altavoz (variable `RGSLIDE_MIC_NOSPK=1` la omite) y el demonio ya no la apaga por silencio.
- Banco de medida de latencia: la deteccion automatica de tonos del microfono no es fiable cuando el micro y la reproduccion se pisan; se miden latencias con el registro del demonio (`audio abierto (anillo=..., N ms desde el primer sonido)`).

## 5p. Audio siempre encendido (ALWAYS_ON) y micro compatible (2026-11-26) - MEDIDO
- Idea del usuario: mantener el audio siempre encendido. Implementado en `rgslide-audiod` con `/etc/rgslide/audio.conf` (`ALWAYS_ON=1`, por defecto en el overlay): al arrancar enciende la ruta, hace el cebado (la 1a apertura se atasca, la 2a funciona), abre el PCM y lo mantiene abierto rellenando con silencio; ya no hay latencia de apertura ni clics perdidos. Coste conocido: el siseo del amplificador suena mientras esta encendido (el usuario lo percibia; no hay medida objetiva).
- Con `ALWAYS_ON=1` el micro NO debe reencender la ruta del altavoz (mata la captura abierta): `rgslide-audio-mic on` solo toca el mezclador de captura. Verificado: tono de 1 kHz por ALSA `default` -> audiod -> altavoz -> micro: amplitud 358-380 a 1 kHz durante el tono, 0 fuera; RMS 260-270 vs 18 de reposo.
- Con la reproduccion ya abierta, abrir la captura despues o antes da el mismo resultado (439 y 373 de pico): lo que mata la captura es REENCENDER la ruta (`rgslide-audio-spk on`) con ella abierta.
- `rgslide-agdsp-access` (get/put con etiquetas spk/mic/vol y flock) evita que un `off` suelte el acceso al DSP mientras otro componente lo usa.

## 6. Advertencia: registros AGCP (0x56xxxxxx)
No leer ni escribir `0x56xxxxxx` (MCDT, DMA 0x56650000, VBC 0x56510000, IRAM 0x568xxxxx) con devmem/mmap/`/dev/mem` ni con ningún truco: con el dominio sin acceso es un **abort síncrono externo → reinicio** (CITADO e5 FINDINGS §32 y §34 "Trap"; un `sprd_pcm_pointer` leyendo el DMA tumbó una Slide tras 5 h 50 min). Solo son seguros PMU APB `0x64910000` y AON APB `0x64900000` (vía debugfs regmap) y SOLO lectura. Esa regla es la que implementa `agdsp_can_access` (§2).

## 7. Resumen
| pregunta | respuesta | etiqueta |
|---|---|---|
| ¿start = secuencia inversa de stop? | sí; deja resets liberados, no asertados | MEDIDO (asm + registros) |
| ¿0x544=7 significa parado? | no, es "dominio durmiendo" en reposo | MEDIDO/CITADO |
| ¿bit 31 de AON 0x78 bloquea? | no; la escritura a AON 0x140 sí entra; 0x78[15:0]=0x9620 es una clave | MEDIDO |
| ¿DSP vivo? | **sí antes de nuestro start** (respuestas SBLOCK_INIT a 264.9 s, LK "audio cp boot ok!") | MEDIDO |
| ¿vivo tras nuestro start? | no se sabe; el silencio es esperable (rings borrados, nadie manda) | SUPUESTO |
| causa de "hw_ptr no avanza" | no está en el arranque del DSP; ver e5: DSP perfiles, `VBC DAC0 DG Set`, period timer | CITADO |

Cómo deshacer: nada cambiado en la Slide ni en el repo salvo este fichero.

## 5q. Siseo en reposo: `Speaker Mute` como interruptor del amplificador, y `VBC DAC0 DG` es una atenuación

- HAL (`audio.primary.whale.so`, REA/Ghidra): `codec_mute_all_pa_off(mute)` = `Virt Output Switch`=1 + 4× `set_mute_control` (Speaker/Speaker2/HeadPhone/Earpiece Mute); al desmutear, al revés. `get_ucp1301_type` falla siempre (el chip I2C `6-0058` no responde). MEDIDO.
- `Speaker Mute`=1 pone GPIO 176 a `lo` (amplificador apagado) en ~40 ms y =0 lo devuelve a `hi`; el PCM sigue abierto y el 1.er tono tras desmutear suena completo desde la 1.ª ventana de 50 ms (200 vs 5 de reposo, 3 repeticiones). MEDIDO.
- `rgslide-audiod` con `AMP_MUTE=1` (`/etc/rgslide/audio.conf`) activa el mute tras 1,2 s de silencio digital y lo quita al llegar sonido. Que el siseo desaparezca a oído: SUPUESTO hasta que el usuario lo confirme (el micro interno no lo capta).
- `VBC DAC0 DG Set` es ATENUACIÓN: amplitud 1049 con DG=3, 130 con DG=25, 9 con DG=64 (MEDIDO). `rgslide-volume` ahora mapea 100 % -> DG=20 y 0 % -> DG=64 (antes 100 % era DG=64, casi mudo). Android usa DG=25.
- El THD medido con el micro pegado al altavoz no sirve (92-137 % en todo): no usarlo para juzgar distorsión.

## 5r. Micrófono: suelo de ruido (MEDIDO)

- Ruido en reposo rms ≈ 148-175 (≈ -47 dBFS) con ganancia 6, banda ancha (pasa-bajos, autocorrelación 0,92 a 2 muestras); IGUAL con el amplificador encendido o apagado: no viene del altavoz.
- `ADC_1 Gain ADC_1 Capture Volume` guarda solo 3 bits (9 se lee 1, 12 se lee 4) y no es lineal: ruido rms 5 / 22 / 172 con 0 / 3 / 6. El tono de prueba escala igual (6 / 28 / 215), así que la relación señal/ruido no mejora bajando la ganancia: el ruido está antes del ADC.
- `VBC ADC0 DG Set` (10, 25, 50) no cambia ni el tono ni el ruido: no está en la ruta de captura.
- Conclusión: con 6 (valor de Android) la voz a 2-5 cm queda ~25 dB sobre el ruido; mejorar más exige filtrado por software o la cadena CVSREC del HAL (no replicada). Pendiente de oído.

## 5s. Ruido del micro y `Inter PA Config` (MEDIDO)

- El ruido del micro (rms ≈ 150, picos 410/516/826/1240 Hz, autocorrelación a 52 Hz) NO depende de: amplificador (Speaker Mute), ES, backlight, frecuencia de CPU, `Mic Function`. Sí sube con carga de CPU (rms 231) y con el altavoz y su demonio parados (rms 458 con `rgslide-audio-spk off`): el rail analógico del códec se ensucia con la actividad del SoC. No se ha encontrado palanca por software.
- `Inter PA Config` valía 2 (Android, Music/Handsfree/Playback: DTRI_F=7, DEMI=1, mode=0, EMI_rate=1 = 0x12F = 303). Ahora `rgslide-audio-spk on` lo fija a 303. Sin cambio medible en el tono; es el valor de Android (EMI/modulación del amplificador interno), mejora esperable a oído: SUPUESTO.

## 5t. Barrido de ganancias del altavoz (MEDIDO, sin conclusión)

- `HPL/HPR Gain` (1-6), `DAC Gain` (1-3) y `VBC DAC0 DG` (0-20) cambian el nivel del tono captado (p. ej. HPL=1/DG20 ≈ 480; HPL=3/DG4 ≈ 980; HPL=6/DG20 ≈ 150), pero la misma configuración (HPL=1, DG=20) dio 477, 486 y 711 en tres pasadas: la medida con el micro pegado al altavoz no es repetible, no sirve para elegir ganancias por SNR.
- Se deja la ruta como está (HPL/HPR=1, DAC=1, DG=20 al 100 %, `Inter PA Config`=303). Si a oído el altavoz sigue ruidoso en reproducción, probar HPL=3-4 con DG más bajo (más ganancia analógica, menos atenuación digital) comparando a oído, no con el micro.

## 5u. CAUSA DEL RUIDO DEL MICRÓFONO: el ventilador (MEDIDO)

- `/etc/rgslide-fan.conf` estaba en `FAN_MODE=max` (pwm 255) y el micro del borde inferior capta el ventilador. Ruido en reposo (rms): pwm 0 → 14, 50 → 18, 70 → 22, 90 → 170, 110 → 56, 130 → 70, 160 → 152, 255 → 230-300, curva automática antigua → 153. Con el ventilador parado el ruido es ~10× menor.
- Esto explica todo lo anterior: la banda ancha 400-1600 Hz, que subiera con carga de CPU (la curva subía el pwm), que no dependiera del amplificador, de ES ni del backlight, y que el micro "se intuyera con ruido".
- Curva nueva (`rootfs/overlay/etc/rgslide-fan.conf`): `40:50,52:70,62:110,68:160,74:255`, `FAN_SAFE=68`. Medido: pwm 70 con carga normal mantiene el SoC en 58-62 °C (150 s); los disparos pasivos del kernel están a 70/85 °C y el crítico a 110 °C. Con curva nueva, ruido del micro rms ≈ 20 (antes 150).
- Mientras se graba voz con carga alta el ventilador subirá y volverá a ensuciar la captura: el micro y el ventilador comparten chasis.
- Con el ventilador a pwm 50-70, `Speaker Mute` 1/0 (6 capturas alternas): rms 14/13/11/21/14/12, sin diferencia: el micro interno sigue sin captar el siseo del amplificador (el suelo es el propio del micro, ~-60 dBFS). Tono de 1 kHz a amp 12000: ~450 frente a ~14 de reposo (≈30 dB). Las medidas de 5s/5t con el ventilador a 255 estaban contaminadas por él (explica la falta de repetibilidad 477/486/711).

## 5v. Voz sintética por el micro con el ventilador callado (MEDIDO)

- Banco: 5 vocales sintéticas (formantes reales, f0 120 Hz, 4 s) reproducidas por `default` mientras se captura el micro interno (pwm 50). Es altavoz→micro dentro del mismo chasis, no voz humana a 2-5 cm: sirve para el SNR, no para inteligibilidad.
- Alineación por correlación cruzada (pico/mediana 23-60; corr. forma de onda 0,13-0,28: el camino altavoz→micro colorea mucho). SNR en banda 300-4000 Hz sobre el reposo: amp 1500 → 1-7 dB; amp 5000 → 5-12 dB; amp 14000 → 11-19 dB (a 19,4 dB, e 13,2, i 11,1, o 16,4, u 14,5). El micro capta señal de voz claramente por encima del ruido.
- Sin persona no se puede medir inteligibilidad; el siseo del amplificador sigue sin ser medible (el micro nunca lo capta): `AMP_MUTE` lo elimina por construcción (GPIO 176 a `lo`), pero que se oiga menos es SUPUESTO hasta que lo confirme el usuario.
- Rootfs: `/dev/sda73` ext4 rw; los 10 ficheros de audio/ventilador de la Slide son idénticos (sha256) a `rootfs/overlay/` y `audiod` coincide con el compilado del repo. El cambio sobrevivió a un reinicio en frío; solo hace falta reflashear si se regenera el rootfs desde cero.

## 5w. `AMP_MUTE` sin oscilar: cierre solo tras 15 s de silencio

- Problema medido: con el umbral corto (1,2 s) y la condición de cola, el relleno de ceros del propio demonio dejaba la cola en ~600-770 muestras y el mute oscilaba (43 cambios en 6 min, pitidos de relé y cortes en pausas musicales).
- Regla actual: cierra tras `IDLE_PCM_MS` (15 s) de silencio **y** con la cola ya consumida por el DAC; abre en cuanto llega sonido (<1,2 s). MEDIDO: reposo largo = 2 cambios en 30 s; pausa de 3 s entre tonos = mute 0 durante toda la pausa; ataques completos desde la primera ventana de 50 ms.
- El sensor de sonido no distingue el siseo del audio real (ambos superan el umbral de 64), así que con ALWAYS_ON el amplificador sigue abierto hasta 15 s tras el último sonido. Quitarlo del todo deja la estática siempre audible.

## 5w. `AMP_MUTE` sin oscilar: cierre solo tras 15 s de silencio

- Problema medido: con el umbral corto (1,2 s) y la condicion de cola, el relleno de ceros del propio demonio dejaba la cola en ~600-770 muestras y el mute oscilaba (43 cambios en 6 min: pitidos de rele y cortes en pausas musicales).
- Regla actual: cierra tras `IDLE_PCM_MS` (15 s) de silencio **y** con la cola ya consumida por el DAC; abre en cuanto llega sonido (<1,2 s). MEDIDO: reposo largo = 2 cambios en 30 s; pausa de 3 s entre tonos = mute 0 durante toda la pausa; ataques completos desde la primera ventana de 50 ms.
- El sensor de sonido no distingue el siseo del audio real (ambos superan el umbral de 64), asi que con ALWAYS_ON el amplificador sigue abierto hasta 15 s tras el ultimo sonido. Quitarlo del todo deja la estatica siempre audible.

## 5x. AMP_MUTE revertido y estado del reloj del I2S (2026-11-26)

- `AMP_MUTE=0` (commit `bd44a37`): el amplificador queda siempre encendido con la ruta abierta, para buscar la causa del siseo sin enmascararlo. `Speaker Mute` fijo a 0, 1 solo cambio en el registro.
- MEDIDO: `ap-iis0` (reloj base del I2S, 153,6 MHz de `tgpll-153m6`) tiene `enable=0` **con el PCM en `RUNNING`**, y `iis0-eb` / `audcp-iis0-eb` tambien a 0; el unico reloj de audio activo es `clk-26m-aud`. Comprobado con el demonio, con `rgslide-cap` y muestreando cada 0,5 s durante un tono.
- El nodo `agcp/i2s@56270800` (enlazado a `sprd-dai`, `sprd,id=0x125`, `hw-port=0`) **no declara `clocks` en el DT**; el unico nodo i2s con `clocks` es `ap-ahb/i2s@0x202b0000`, que esta `status=disable`. `snd-soc-sprd-dai.ko` calcula `iis_sck` desde `src_clk` (constante 24,576 MHz en el modulo) e imprime `iis_sck`/`iis_clkd` solo con `dev_dbg`; con `dynamic_debug` activado esas trazas no salen, asi que la configuracion del reloj ocurre por otra ruta (VBC: `vbc_put_iis_master_width`).
- `ucp1301` (amplificador I2C `6-0058`): el driver existe (`ucp1301`) pero el nodo no esta enlazado y `bind` falla con `read chip id high fail -5` (EIO), tambien con la ruta del altavoz encendida. Sin `i2c-dev` en el kernel no se puede sondear el bus desde espacio de usuario.

## 5y. Reloj del I2S: el VBC lo controla y dice "no need audio top" (MEDIDO)

- `snd-soc-sprd-vbc-v4.ko` controla el reloj del IIS (`vbc_put_aud_iis_clock`, controles `VBC_IIS_MASTER_ENALBE`=115, `VBC IIS Master Setting`=87 enum, `VBC_IIS_MST_SEL_0_TYPE`=116, `VBC_MUX_MST_IIS0_PORT_DO_SEL`=67) y decide si pide reloj al AP segun `sprd,need-aud-top-clk`.
- Ese parametro NO esta declarado en ningun nodo (`vbc@56510000`, `audio-codec@56360000`, `i2s@56270800`), asi que el driver imprime `vbc_put_aud_iis_clock: No need audio top to provide da clock` y nunca habilita `ap-iis0` (sigue a 0). El I2S se queda con el reloj interno del codec/DSP.
- Probado a mano: maestro=1 y tipo de reloj 1..4 se aceptan, pero `ap-iis0` sigue a 0 y el siseo no cambia (rms 16,5 / 17,2 con maestro 1 / 0, alternando). Es decir: el reloj del AP no es la causa del siseo con esta configuracion.
- `ucp1301` sigue sin enlazar (`bind` -> `read chip id high fail -5`).

## 5z. Ruido del DAC frente al del amplificador interno (MEDIDO)

- Banco: reposo con `Inter PA Config`=303 y =0 alternando, 5 rondas, 2 s de espera, mismo volumen. Resultado: 19,3/19,2, 22,7/20,9, 19,3/16,9, 18,6/18,1, 21,9/25,0 (rms PA303/PA0). Sin diferencia significativa.
- Conclusión: con el amplificador interno del codec apagado el siseo sigue igual, así que no lo mete el `Inter PA`; el ruido entra antes, en el DAC o en su referencia.
- El DT de Android (binario `dump/boot/dtb_a.img`) resultó estar **entero a ceros** (8 MiB), así que la comparación con Android se hace por `dump/live/dmesg-gamma-now.txt`: allí el GPIO del altavoz (`hook_general_spk`, gpio 176) aparece una sola vez con `on: 0` y el módulo `snd_soc_sprd_pa_ucp1301` está cargado con **0 usos**. En Debian el control `Speaker Function` mueve exactamente ese 176 (y no el 112 del `spk-ext-pa-gpio` que citaba la investigación previa).

## 5aa. ¿El siseo entra por el DAC o por el amplificador? (MEDIDO)

- Banco: mezclador DAC->HP apagado (`HPL/HPR Mixer DACL/RHPL Switch`=0, el amplificador y el DAC siguen encendidos) frente a encendido, 5 rondas, 3 s de espera. Resultado (rms apagado/encendido): 12,6/14,0, 12,1/13,9, 17,6/11,8, 13,8/15,9, 14,7/15,9.
- Con el mezclador cerrado el ruido solo baja ~1-2 rms (dentro del ruido de la medida): la mayor parte del siseo NO viene de la señal del DAC, entra en la etapa analogica (referencia/alimentacion del codec) o es del propio amplificador interno.
- Junto con 5z (PA on/off sin diferencia), la unica via que queda es la alimentacion/referencia analogica del codec: `avdd18`, `avdd12`, `vdd28` estan siempre encendidos y sin consumidores declarados.
