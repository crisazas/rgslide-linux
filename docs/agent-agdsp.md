# AGDSP: arranque del DSP de audio — conclusión (2026-11-26) — SIN RESOLVER

Alcance: lo medido en la Slide con `scripts/agdsp-probe.sh` (`RGSLIDE_OK=1`: dd + stop + start, sin abrir ningún PCM) y el análisis estático de `docs/agent-audio.md`. 0 panics.

## MEDIDO
- La tarjeta `sprdphonesc2730` se registra y `/sys/devices/platform/audiocp_boot/start` devuelve rc=0.
- Tras `start` cambia AON `0x140` (bootvector): las escrituras llegan al bloque AON.
- **No cambian**: PMU `0x544` (`00070700`, sys=7 core=7) ni AON `0x78` (`80009620`).
- **No aparece** ninguna línea `aud_send_cmd` ni `wait dsp` en dmesg tras `start`.
- Los relojes `audcp-*` siguen en enable_count 0.
- `start_store` ignora el retorno de `regmap_update_bits_base`: rc=0 no prueba nada.
- El `Failed to open channel 0,dst=0,rval=-19` sale por diseño (dst 0 = AP) y no indica DSP parado.

## CONCLUSIÓN
El DSP no arranca con la receta de `start`. El mejor candidato sigue siendo `bootprotect` (AON `0x78`, bit 31 puesto por el LK) ignorando la escritura de `0x9620`, o que el bloque AUDCP no tenga alimentación/reloj raíz hasta que lo pida otro agente (Trusty/pm_sys). Ninguno de los dos está probado: las dos son hipótesis (SUPUESTO).

## Contraste con otros proyectos (MEDIDO en sus DTS publicados)
- Los DTS de Android del RG556 y del RG-Cube (gist de TheGammaSqueeze, misma T820) tienen el mismo `audiocp_boot`: `corereset 0xb88`, `sysreset 0xb98`, `sysshutdown 0x3d0`, `coreshutdown 0x3d4`, `deepsleep 0x818`, `reset_sel 0xba8`, `corestatus/sysstatus 0x544`, `bootprotect 0x78`, `bootvector 0x140`, `bootaddress_sel 0x144`. Es decir, coincide con lo que escribimos: el DT no es la causa.
- `rg-rotate-linux` (ums512, otro SoC) sí arranca el AGDSP en mainline, pero su driver no está en `third_party` (el submódulo `vendor/` está vacío) y los registros de ums512 son otros; no se puede comparar paso a paso.

## Cronología en GammaOS (MEDIDO, `dump/live/dmesg-gamma-now.txt`)
- 9,48 s arranca `vendor.audio-hal`; 9,63 s `aud_pipe_open`; 9,71 s arranca `vendor.modem_control` (carga `pm_sys`, el CM4); 10,07 s `modem_control modem run = 1`; 10,41 s primeros `aud_send_msg` (`dst=1`, canal 4).
- Entre el HAL y el primer mensaje no hay ningún log de kernel del `start`; el único dato intermedio es el `Skip verify chain_partition[l_agdsp]` de Trusty a 10,07 s mientras verifica `pm_sys`.
- No se puede decir si el DSP exige que el CM4 esté ya corriendo (SUPUESTO). En la Slide el CM4 sí corre (`mcu_mode=2`), y aun así `start` no mueve `0x544`.

## Qué NO se hizo
- No se leyó ni escribió ningún registro `0x56xxxxxx` (sin alimentación podría colgar el bus).
- No se probó a quitar `bootprotect` por SMC/Trusty, ni a replicar el orden de arranque del HAL de Android (en GammaOS el primer `aud_send_cmd` con respuesta es a 14,83 s y no deja log de kernel intermedio).
- No se abrió PCM de reproducción ni de captura.

## Consecuencia
Sin DSP no hay reproducción ni captura: **audio y micrófono quedan sin funcionar**. El micrófono interno tampoco está identificado (faltan `audio_route.xml`/`audio_params` del Android).

## Siguiente paso (con alguien delante, riesgo medio)
1. Trazar en Android (stock, si se vuelve a poder) qué escribe el HAL/Trusty antes del primer `aud_send_cmd`: SMC a Trusty o escrituras a `0x78`.
2. Solo entonces repetir `agdsp-probe.sh` con ese paso previo. Parar ante cualquier `Unable to handle kernel`.
