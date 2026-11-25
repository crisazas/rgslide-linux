# Panfrost en la Slide: informe de viabilidad (2026-11-25)

Solo lectura: no se compilo ni se flasheo nada. Lo MEDIDO esta marcado; lo DEDUCIDO o SUPUESTO, tambien.

## Pregunta
Si la Mali-G57 del T820 (`gpu_id 0x9091`) la soporta Panfrost, ¿por que no ir por Mesa estandar en vez de blob de Arm + libhybris?

## Que sabemos que funciona en OTRO SoC Unisoc (evidencia externa, no nuestra)
- `sprd-linux-devs/e5-linux` (Rongyue E5, UMS9621, kernel 5.15): `GL renderer: Mali-G57 (Panfrost)` y Phosh sobre la GPU (su `docs/FINDINGS.md` 20.7). Misma GPU, mismo `0x9091`, `shader_present=0x5`.
- Su parche `kernel/patches/0005-panfrost-valhall-g57.patch` (776 lineas, 12 ficheros) hace tres cosas: (1) lleva a G57 el soporte Valhall JM de upstream 6.0 (`GPU_MODEL(g57, 0x9001)`, `hw_features_g57`, `hw_issues_g57`, errata); (2) anade `panfrost_sprd.c` (305 lineas), el encendido de la GPU por syscons (no hay power domain); (3) busca las IRQ `"JOB"/"MMU"/"GPU"` sin distinguir mayusculas.
- Mesa 25.0.7 de Debian (la de la Slide) ya trae `panfrost_dri.so`.

## Lo MEDIDO en la Slide (kernel 5.4.233, `/proc/config.gz` extraido de `dump/boot/boot_a.unp/kernel`)
- `# CONFIG_DRM_PANFROST is not set`; `DRM_SCHED` no existe; `DRM_GEM_SHMEM_HELPER` no existe; `CONFIG_DRM_GEM_CMA_HELPER=y`. `CONFIG_MODVERSIONS=y`, `CONFIG_CFI_CLANG=y`, `CONFIG_LTO_CLANG=y`, `CONFIG_SHADOW_CALL_STACK=y`, sin firma de modulos. Compilador del fabricante: clang 12.0.5 (r416183b).
- En la Slide en ejecucion, `drm_sched_init`, `drm_sched_entity_init` y `drm_gem_shmem_*` NO estan en `/proc/kallsyms` (no compilados). El arbol 5.4.254 de ZTE (otro equipo) SI trae las fuentes de `drivers/gpu/drm/panfrost` (modelos hasta g31/Bifrost, UAPI 1.1), `drm_sched` y `drm_gem_shmem_helper`; no sabemos si el arbol de la Slide es igual.
- DT de la GPU (`/proc/device-tree/soc/mm/gpu@23140000`, compat `sprd,mali-natt`): estan TODAS las propiedades que usa `panfrost_sprd.c` de e5 (`top_dvfs_cfg`, `gpu_sw_dvfs_ctrl`, `dcdc_gpu_pd`, `top_force_shutdown`, `gpll_cfg_frc_off/on`, `clk_core_gpu_eb`, `gpu_top_state`, `sprd,gpu-dvfs-apb-syscon`), `interrupt-names = JOB MMU GPU` (mayusculas, como en e5), `resets`, `system-coherency`. reg `0x23140000 0x4000`.
- `mali_kbase` (r40p0 en esta Slide) es un MODULO (`1200128 B`, en uso por 4 clientes); `sprd_drm` tambien.
- Core mask real `0x50005` y `4 cores` (kbase) frente a `shader_present=0x5` (2 shaders) en e5: posible diferencia de binning, NO medida con panfrost.
- No hay nodo `sprd,ump962x-syscon` en el DT en ejecucion (la plataforma de kbase lo busca por compatible; `panfrost_sprd.c` de e5 avisa y sigue sin tocar el DCDC). Pendiente de comprobar como lo resuelve kbase aqui.
- Exportaciones del kernel de fabrica (cadena `__ksymtab_strings`, 113 nombres consultados): FALTAN `dma_resv_get_excl_rcu`, `drm_syncobj_put`, `drm_get_unmapped_area`, `devm_reset_control_get_optional_exclusive`, `dma_map_sg_attrs`, `dma_unmap_sg_attrs`, `drm_gem_shmem_free_object`. Alguno puede ser inline o macro (no es prueba de ausencia). Esto IMPORTA solo si se compila `panfrost.ko` fuera de arbol.
- Mesa 25.0.7 exige UAPI de kernel >= 1.1 (`panfrost_kmod.c`: "kernel driver is too old (requires at least 1.1)"): el panfrost de 5.4 declara 1.1, cumple.

## Lo que NO existe / lo que bloquea
1. **Fuentes del kernel 5.4.233 de la Slide**: NO las tenemos y no encontre una publicacion GPL de Anbernic para este equipo (busqueda web, no exhaustiva; GammaSqueeze dijo en 2024 tener "fuentes ums9620 algo mas antiguas", sin confirmar). El unico arbol cercano es el 5.4.254 de ZTE para UMS9620 (`Enceka/android_kernel_zte_ums9620_mifi_u30air`, 201 MB, defconfig `sprd_qogirn6pro_defconfig`), de OTRO dispositivo (un MiFi sin DSI, sin mando, sin MCU). Ese arbol NO contiene `singleadcjoy`, `spi_mcu`, `retrogame_joypad` ni el panel de la Slide (busqueda en su arbol: 0 ficheros). Esos modulos solo existen como binarios `.ko` 5.4.233 en el volcado (`singleadcjoy`, `aw32257_charger`, `pwm-fan`, `sc27xx-vibra`, ...), y los modulos se cargan por `vermagic 5.4.233 modversions` + CRC de simbolos: mezclar con un kernel compilado de otro arbol rompe los CRC y la ABI de CFI.
2. **Camino A, modulos fuera de arbol contra el kernel de fabrica** (no cambia el kernel, no se pierde nada): requiere `Module.symvers`/cabeceras del kernel 5.4.233 exacto, que tampoco tenemos, y un `drm_sched` + `drm_gem_shmem_helper` que el kernel no trae. Viable en teoria si se compilan esos dos como parte de un modulo grande y se pueden resolver CRC a mano (genksyms desde las cabeceras del 5.4.254 de ZTE da CRC distintos). Alto riesgo, no probado por nadie.
3. **Camino B, kernel propio sobre el arbol ZTE 5.4.254 + panfrost + sprd_gpu**: ya existe la receta de otro proyecto (`mu300-linux`, mismo SoC, mismo arbol) para arrancar Linux con ese kernel en un UMS9620. Habria que (a) portar los drivers de la Slide que faltan (mando, MCU, panel) desde binarios sin fuentes: **no se puede** salvo reescribirlos, (b) aplicar el backport Valhall de e5 (hecho para 5.15, hay ~100 commits de panfrost y 55 de scheduler entre v5.4 y v5.15), (c) escribir/portar el DTS de la Slide (hoy usamos el del fabricante, que no es compilable aqui).
4. **Camino C, copiar el enfoque de e5 literal (5.15 GKI)**: la Slide es android13-5.4, no 5.15; no hay arbol 5.15 para este SoC.

## Veredicto
- **Panfrost en G57 es real** (e5 lo demuestra), pero **en este equipo hoy NO es viable sin las fuentes del kernel de la Slide**.
- El camino A (modulos externos) es el UNICO que conserva todo lo que ya funciona (mando, MCU, panel, sensores), y esta bloqueado por falta de `Module.symvers`/cabeceras 5.4.233. Cabe pedir al fabricante/GammaSqueeze (que tiene fuentes ums9620 "algo mas antiguas") el arbol GPL de la Slide: **la accion util es pedirlo**.
- Mientras tanto, el camino blob+hybris da 59 fps en `hybristest`, GBA/NDS a velocidad completa y ES estable. No hay perdida que justifique arriesgar el arranque.

## Rama
NO se ha creado: la condicion era "de ser viable" y hoy no lo es. `master` sigue siendo el camino blob+hybris. Si se consiguen las fuentes del kernel de la Slide, se crea `panfrost-etapa2` desde `master` y el trabajo empieza ahi sin tocar la via actual.
