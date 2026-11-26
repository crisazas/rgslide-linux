# Panic de arranque en apsys_dvfs: causa y arreglo (analisis estatico, 2026-11-26)

## Causa (MEDIDA en pstore/journal + desensamblado; la parte de "cuando ocurre" es inferida)
1. **Colision de nombre entre dos modulos de fabrica.** `apsys-dvfs.ko` y `mmdvfs.ko` registran los dos un gobernador devfreq llamado `vdsp_dvfs`
   (`strings` de ambos; en apsys es `vdsp_devfreq_gov`, tabla `.rodata+0x1d00`; en mmdvfs `vdsp_dvfs_init`). Solo uno puede tenerlo: el segundo en llegar
   recibe `-22` ("governor vdsp_dvfs already registered").
2. **Si el perdedor es mmdvfs, es inocuo** (sigue cargado, `sprd_vdsp` prueba bien; es lo que pasa tambien con el orden de fabrica, apsys-dvfs en la linea 20 de
   `dump/live/dlkm/modules.load` y mmdvfs en la 28). Aparece en TODOS los arranques (boot 0, -1, -4, -5, -6: 1 vez cada uno, mmdvfs, 26-28 s).
3. **Si el perdedor es apsys_dvfs, su `init_module` no deshace nada.** Desensamblado de `init_module` (`dump/boot/rd_vendor/lib/modules/apsys-dvfs.ko`):
   - `+0x28` `platform_driver_register(apsys-dvfs)`: el retorno se **ignora**;
   - bucle de 4 gobernadores (dpu, gsp, vdsp, vsp): `+0x5c bl devfreq_add_governor`, `+0x60 cbnz w0 -> +0xc4`;
   - `+0xc4..+0xe0`: solo `printk("apsys_dvfs_register: failed to add governor: %d")` y vuelve con el error. **No** hay `devfreq_remove_governor` ni
     `platform_driver_unregister` en esa ruta (solo en el fallo del registro de cada driver hijo, `+0x7c`).
   Resultado: el modulo se libera (init fallido) pero se quedan en las listas del kernel el driver de plataforma `apsys-dvfs` y los gobernadores dpu/gsp ya
   anadidos, todos apuntando a memoria del modulo YA LIBERADA.
4. **El panic es el reintento.** Un segundo `modprobe apsys_dvfs` (otro `udev-worker`, alias `of:...hwdvfs-*`) imprime `Driver 'apsys-dvfs' is already
   registered, aborting...` (retorno ignorado) y sigue a `devfreq_add_governor`, que recorre `devfreq_governor_list` y hace `strncmp` contra nombres de
   gobernadores liberados: `x0=0x10`, `pc=__pi_strncmp+0x24`, `lr=devfreq_add_governor+0xb4`, `init_module+0x60` (= la instruccion tras `bl devfreq_add_governor`
   de arriba). Es un **use-after-free**, no un puntero NULL propio: el `0x10` es lo que hay en la memoria reutilizada.
5. **Prueba de la carrera (pstore `dmesg-ramoops-0/1`):** T1848 (mmdvfs) registra `vdsp_dvfs` con `ret: 0` a los 26.135526 y T1847 (apsys_dvfs) falla con `-22`
   a los 26.136541, en paralelo en dos CPUs; el segundo intento (T1839) revienta a los 26.649-26.677. En el arranque actual (boot 0) apsys_dvfs fue insertado antes
   por `systemd-modules-load` (24.37-24.50 s) y mmdvfs llego a los 27.6 s por udev: apsys gana, mmdvfs pierde (inocuo).
- SUPUESTO (no medido): por que antes de mi serializado mmdvfs/apsys corrian a la vez es que `systemd-modules-load` (apsys en modules-load.d) y el coldplug de udev
  (alias `of:N*T*Csprd,hwdvfs-*` -> `mmdvfs`/`apsys_dvfs`, y la cadena `sprd_drm`/`sprd_gsp`/`vpu` -> apsys-dvfs) arrancaban a la vez. No hay evidencia de que el
  init (`initramfs/init`) los cargue: `rgslide-factory.load` y `dump/boot/rd_vendor/.../modules.load` NO contienen apsys-dvfs ni mmdvfs (solo `sprd_soc_id`);
  `mmdvfs.ko` ni siquiera esta en el rd_vendor (solo en `dump/live/dlkm` y en `/lib/modules/5.4.233`).
- `modules.dep` (rootfs): `mmdvfs.ko: sprd_soc_id sprd_camsys_pw_domain` (NO depende de apsys-dvfs); `sprd_vdsp.ko: mmdvfs ...`. Por eso `modprobe sprd_vdsp`/`mmdvfs`
  podia cargar mmdvfs sin apsys-dvfs: era la via libre para ganar la carrera.

## Arreglo desplegado
`/etc/modprobe.d/rgslide-dvfs.conf` (y copia en `rootfs/overlay/etc/modprobe.d/rgslide-dvfs.conf`, nombre nuevo):

    softdep mmdvfs pre: apsys_dvfs

Efecto: cualquier `modprobe` que arrastre mmdvfs (alias del DT `hwdvfs-*`, o `sprd_vdsp` por dependencia) inserta antes `apsys_dvfs` y espera a que termine su init
(`finit_module` es sincrono y el kernel hace esperar a un segundo insert del mismo modulo en estado COMING). Asi apsys_dvfs siempre gana `vdsp_dvfs` y mmdvfs
pierde en la forma inocua. Complementa (no sustituye) el serializado existente (`00-rgslide-dvfs.conf` + `rgslide-serial.conf`). No bloquea ningun modulo: solo anade
una dependencia previa; `insmod` (init, `gpu-prep` usa modprobe por nombre de apsys-dvfs) no cambia.
- Comprobado SIN reiniciar con `modprobe -D <mod>` (dry-run, solo lectura de modules.dep/softdep), antes y despues:
  `mmdvfs` y `sprd_vdsp` ahora insertan `apsys-dvfs.ko` justo antes de `mmdvfs.ko`; `sprd_drm`, `sprd_gsp`, `vpu`, `apsys_dvfs` y `jpg` quedan con la misma
  lista que antes (`apsys-dvfs` ya iba antes de `sprd-gsp`/`sprd-drm`/`vpu`; `jpg` no depende de ninguno). Nada desaparece de las cadenas de GPU/audio.
- Deshacer: `sudo rm /etc/modprobe.d/rgslide-dvfs.conf` (y `rm rootfs/overlay/etc/modprobe.d/rgslide-dvfs.conf` para que no vuelva a la proxima imagen).

## Lo que NO se hizo / limites
- No hay reinicios: el arreglo es estatico y no esta probado en frio. La tasa original era 2 de 14; hacen falta varios arranques en frio para decir nada.
- No se parcheo `apsys-dvfs.ko` (el bug de fondo, falta de limpieza en el error, sigue ahi): si apsys_dvfs vuelve a fallar por otro motivo, el reintento puede
  volver a hacer panic. La unica defensa real es que nunca pierda `vdsp_dvfs`.
- NO se puso `blacklist`/`install ... /bin/true` para apsys_dvfs: lo necesitan sprd_drm/gsp/vpu y la modules-load; seria mas riesgoso que el softdep.

## Como medir tras reiniciar (coordinador)
    sudo journalctl -k -b 0 --no-pager | grep -E "Oops|Kernel panic|Driver 'apsys-dvfs' is already|apsys_dvfs_register|failed to add governor"
- Esperado: SIN `Oops`, SIN `Driver 'apsys-dvfs' is already registered` y SIN `apsys_dvfs_register: failed to add governor`.
- SI saldra (es inocuo, es el orden de fabrica): `mmsys-dvfs: vdsp_dvfs_gover ... failed to add governor: -22` + `devfreq_add_governor: governor vdsp_dvfs already registered`.
  Ya no es senal de fallo: lo que importa es que apsys_dvfs NO sea quien pierde.
- Orden: `sudo journalctl -k -b 0 -o short-monotonic --no-pager | grep -E "apsys module registered|Succeeded to register a vsp dvfs|mmsys_module_init : enter"`
  debe mostrar apsys antes que `mmsys_module_init : enter`. Tras un arranque que cae a ~26 s: `sudo ls /var/lib/systemd/pstore/`.
