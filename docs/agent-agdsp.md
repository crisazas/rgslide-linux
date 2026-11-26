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
