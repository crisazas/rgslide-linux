# AGDSP: análisis estático y lecturas (2026-11-25) - SIN RESOLVER

Alcance: solo lectura. No se abrió ningún PCM, no se escribió ningún registro, no se ejecutó `start`/`stop`. Lo único que se ejecutó en la Slide fue `rgslide-audio-prep` (sin `RGSLIDE_AGDSP_BOOT`), que solo carga módulos. Uptime 23 min al terminar, sin reinicios.

## MEDIDO
- **`Failed to open channel 0,dst=0,rval=-19` NO prueba que el DSP esté parado.** `agdsp_access_initialize` (agdsp_access.ko @0x10ac) deja `dst`/`channel` (bytes en `+36/+37` de su struct) a 0 y el hilo llama `smsg_ch_open(0,0)`; en `sipc-core.ko` (@0x26ec) eso da `-ENODEV` si `smsg_ipcs[dst]` es NULL, y el destino 0 (el AP) no existe. Con el mismo DT saldrá igual en GammaOS; el log de GammaOS empieza a 4.2 s y esos módulos se cargaron antes, así que no hay forma de compararlo ahí.
- **`agdsp_access_enable` SÍ se ejecuta en Debian**, igual que en GammaOS (4.222 s allí): en cada carga del card salen decenas de `ap_access_ena_reg (wake up dsp)` / `(wake up done) val = 0x20` (14 en esta carga; 40 pares en el boot de las 20:03). No hay `power up timeout`. Es decir, el handshake AP -> PMU (bit 5 de AON `0x14c`) funciona.
- Registros en reposo (debugfs regmap `dummy-syscon@64910000` = PMU, `@64900000` = AON), idénticos antes y después de cargar todos los módulos de audio:
  PMU `0x3d0=01010103 0x3d4=01010301 0x818=00010024 0x850=0001032e 0x860=00060006 0xb88=0 0xb98=0 0xba8=ffbfffff 0x544=00070700`; AON `0x78=80009620 0x140=2b410000 0x144=00000001 0x14c=0`.
  Los bits de shutdown (`0x3d0` b25, `0x3d4` b25, `0x818` b3) ya están a 0; `0x544` da sys=7 y core=7, igual que el `status` (`07 00 00 00 07 00 00 00 ...`) que se leyó tras `start`.
- Mapa de `start_store` (sprd_audcp_boot.ko @0x810; campos por orden de `.rodata+0x660`: sysshutdown, coreshutdown, deepsleep, corereset, sysreset, reset_sel, sysstatus, corestatus, sleepstatus, bootprotect, bootvector, bootaddress_sel): memset_io de 3 regiones (IRAM AON `0x65006400`, ring smsg `0xaf980400`, params `0xaf980000`; la de IRAM solo si `dsp-reboot-mode`=0, que es el caso: "dsp reboot by DDR!") y luego
  1) reset_sel(PMU 0xba8) &= ~0xffffffff; 2) corereset(0xb88) |= 1; 3) sysreset(0xb98) |= 0x400000; 4) sysshutdown(0x3d0) &= ~0x2000000; 5) coreshutdown(0x3d4) &= ~0x2000000; 6) deepsleep(0x818) &= ~8; 7) bootprotect(AON 0x78) = 0x9620 en [15:0]; 8) bootvector(AON 0x140) = valor derivado de `0xafa00000`; 9) bootaddress_sel(AON 0x144) |= 1; 10) suelta corereset y sysreset. Todo vía `regmap_update_bits_base` **sin mirar el retorno ni esperar al estado**: no hay ningún fallo visible aunque nada surta efecto (explica `rc=0`).
- `audiocp_boot` no expone clk/regulator/power-domain: el DT solo da syscons PMU/AON. El DT del bloque (`agcp`) tiene `clock-controller@56200000` (audcpglb-gate) y `@56390000` (audcpapb-gate) con padre `ext-26m`; los 28 `audcp-*` quedan en enable_count 0 porque nadie los toma hasta que haya un cliente (no es en sí un fallo).
- `sprd_audcp_boot` imprime `base_addr_phy` y `dsp reboot by DDR!` en el **probe**, no en `start`; en GammaOS no salen porque el módulo carga antes de 4.2 s. Tampoco se ve en GammaOS el `start` (el HAL arranca a 9.48 s y el primer `aud_send_cmd` con respuesta del DSP es a 14.83 s, sin log de kernel intermedio).

## SUPUESTO (no probado)
- En Unisoc, estado de PMU 7 = apagado y 0 = encendido: el bloque se enciende bajo demanda con `ap_access_ena` y vuelve a 7 al soltarlo. Por tanto "status = 7" en reposo es normal y **no** distingue DSP corriendo de parado: hay que leerlo *durante* el acceso o tras `start`.
- Candidatos para que `start` no surta efecto: (a) `bootprotect` AON 0x78 ya vale `0x80009620` (bit 31 puesto por el LK, posible bloqueo que ignore escrituras posteriores); (b) el bloque AUDCP no tiene alimentación/reloj raíz hasta que algo (Trusty/pm_sys/HAL) lo pida: los accesos a registros 0x56xxxxxx no se pueden comprobar sin riesgo; (c) la imagen `l_agdsp` ha de estar descifrada/validada (el log de Trusty dice `Skip verify chain_partition[l_agdsp]`).
- La imagen `agdsp` ya se copió bien (6291456 B); no hay indicio de fallo en esa parte.

## Siguiente paso propuesto (NO ejecutado)
`scripts/agdsp-probe.sh` (nuevo, solo lectura de debugfs salvo el `start` que el propio script pide confirmar con `RGSLIDE_OK=1`): lee los registros de arriba justo antes, 0.2 s, 2 s y 8 s después de `dd`+`stop`+`start` y guarda el resultado en `/tmp/agdsp-probe.out`. Responde a: ¿cambian 0xb88/0xb98/0xba8/0x140/0x544 tras `start`? Si no cambian, las escrituras se ignoran (hipótesis a). Hacerlo con alguien delante: el mismo `start` ya se hizo dos veces sin panic, pero no se ha repetido en esta sesión.

## Cómo deshacer
Nada que deshacer en la Slide (no se desplegó nada; solo `/tmp/ap.out`). Ficheros nuevos en el repo: este documento y `scripts/agdsp-probe.sh`. Temporales en el PC: `/tmp/opencode/aud/` (dtb, desensamblados).
