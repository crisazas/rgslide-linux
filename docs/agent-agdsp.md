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
