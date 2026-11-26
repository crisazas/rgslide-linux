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
