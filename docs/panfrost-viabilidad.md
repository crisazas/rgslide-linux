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

---

# Anexo: ¿tendria Panfrost mas o menos rendimiento que el blob de Arm? (teoria, 2026-11-25)

**Nada de esto esta medido en la Slide**: no tenemos Panfrost corriendo aqui. Las cifras ajenas vienen de otros chips y otras cargas, y se citan con su fuente. Lo medido por nosotros se marca MEDIDO.

## Lo que tenemos MEDIDO con el blob (r40p0, 850 MHz, 4 cores segun kbase)
- Sintetico offscreen 1280x960 (`tools/glbench`): ALU pesado 54,7 fps (201 Mpix/s); 1 M triangulos pequenos 18,4 fps (18,4 Mtri/s). x10-11 sobre llvmpipe.
- `hybristest`: 59 fps (limitado por el vsync del panel, 60 Hz); RetroArch menu ~57 fps; SDL2 59,2 fps.
- La presentacion YA es zero-copy: PRIME_FD_TO_HANDLE + ADDFB2 + page flip (`patches/libhybris/0002`), sin copia por CPU. (La nota antigua de `quirks.md` sobre "copia por CPU, sin rendimiento" era del prototipo `drmshow`; `fakewin` midio 4,0 ms de copia/conversion y el camino final ya no copia.)
- Emulacion: GBA/NDS a 60 fps estables, CPU del emulador 33-86 % de UN nucleo. **Esa carga no depende de la GPU**: los nucleos usan renderizado por software y la GPU solo pinta el cuadro final y el menu.

## Evidencia externa sobre Panfrost vs blob (todas de OTROS chips)
- Phoronix 2019 (Midgard/Bifrost, GLES 2 sin FBO): "convergiendo con (y a veces superando) el blob" tras activar una ruta rapida del tiler. Es muy antiguo y de otra arquitectura.
- Collabora, "Panfrost is now using Kraid", 2026-10-26 (RK3588, Valhall v10, Mesa 27): el compilador nuevo quita ~19 % de instrucciones y ~84 % de spills; en apps reales "el rendimiento es casi igual o unos pocos % mejor" (GLES: HalfLife 100 %, Manhattan 104 %, TRex 104 %, Aztec 111 %, CandyCrush 139 %). Dicen textualmente que las apps rara vez estan limitadas por la ejecucion de shaders en GPUs moviles, sino por ancho de banda de memoria. **Ojo**: Kraid va en Mesa 27.0; Debian trixie lleva Mesa 25.0.7 (la de la Slide), asi que ese avance no lo tendriamos sin Mesa propio.
- Nota de `e5-linux` (UMS9621, G57): corre Phosh con `GL renderer: Mali-G57 (Panfrost)` y clientes en GPU. **No publican fps**, solo que funciona.
- `panvk-kbase-android`: PanVK sobre kbase en G57 MC2 (Lenovo TB336FU) da PanPlay "1-9 pass" y DXVK a ~160-230 fps en escenas sencillas; es Vulkan sobre el MISMO kbase de Arm, no Panfrost sobre kernel libre.

## Teoria por componente (de mayor a menor peso)
1. **Aritmetica de shader (ALU)**: el hardware es el mismo (G57). El blob tiene un compilador maduro del propio fabricante; Panfrost con compilador "bifrost/valhall" de Mesa 25 estaba por debajo en instrucciones (Kraid recorta un 19 % mas). Estimacion: **igual o 5-20 % por debajo** en shaders pesados con Mesa 25; paridad con Mesa 27 (Kraid). Para ES (GLES 2, shaders triviales) y RetroArch (un quad con textura) esto es irrelevante.
2. **Ancho de banda y tiler**: dominan en movil y dependen del kernel/MMU y de como se alloca la memoria (AFBC, comprimir framebuffers, cache coherence). El blob usa `system-coherency` y AFBC (Arm Frame Buffer Compression) en gralloc; **Panfrost en 5.4/5.15 usa U-interleaved por defecto y AFBC solo en algunos casos** (Mesa lo implementa, pero la tuberia a la pantalla de Unisoc `sprd_drm` no consume AFBC): posible perdida **0-30 %** en cargas limitadas por ancho de banda. Aqui el dato clave: nuestro `sprd_drm` presenta `ABGR8888` lineal, lo mismo que haria Panfrost, asi que no cambia nada en el flip final.
3. **Reloj de la GPU (DVFS)**: el blob + kbase de Unisoc sube el reloj con carga (850 MHz medidos). `panfrost_sprd.c` de e5 **desactiva devfreq** (`no_devfreq`) y deja un indice fijo de DVFS ("DVFS index 3" en su arranque). Si nos quedamos con el indice por defecto, la GPU quedaria a **un reloj fijo y posiblemente menor que 850 MHz**: ahi puede estar la mayor diferencia, hasta **2x peor** en cargas de GPU, por una cuestion de configuracion y no de driver. Corregible, pero a mano (escribir el indice en el syscon).
4. **Nucleos activos**: kbase ve `core_mask 0x50005` (4 cores) y e5 ve `shader_present=0x5` (2 shaders) con panfrost. Si Panfrost solo enciende 2 de los 4 nucleos (la mascara `0x5` es "no contigua", segun e5, igual que un G57 MC2 independiente), el pico de GPU seria **la mitad**. NO medido; es una discrepancia sin explicar y la mas importante de verificar.
5. **Sobrecarga de CPU del driver**: Mesa/Panfrost corre en el mismo proceso, sin la capa de Android; el blob via libhybris pasa por la bionic parcheada y por binder/HIDL **solo al asignar buffers**, no por draw. Estimacion: **igual o ligeramente mejor con Panfrost** (menos capas), irrelevante para el emulador.
6. **Latencia de presentacion**: hoy hay una espera de fence y un flip bloqueante (`waitFlip`) con 3 buffers. Con Panfrost y Wayland/DRM nativo podria ser algo mejor (page flip asincrono, 2 buffers), **ganancia de 0-1 fotograma**, no de fps.

## Estimacion honesta para ESTE uso (frontend + emulacion 2D/GBA/NDS)
- **Rendimiento visible**: **sin diferencia perceptible** (60 fps fijos por vsync; la GPU esta muy por debajo del limite; el cuello de botella es la CPU del emulador).
- **Peor caso por config** (reloj fijo bajo, 2 de 4 nucleos): GPU ~25-50 % del blob. Cubre de sobra GBA/NDS/SNES y el menu. Se notaria en N64, PSP o juegos 3D reales.
- **Mejor caso** (Mesa 27 con Kraid, 4 nucleos, reloj alto, AFBC): paridad con el blob, +/- 5 %.
- **Ganancias que no son de fps**: sin libhybris/bionic/binder (5 servicios menos, ~100 MB menos de RAM, menos fallos de arranque), sin `libtlspad`, SDL2 y RetroArch estandar, Vulkan y GL ES 3.1 conformes, y juegos con contexto GL (N64) sin parches.

## Para decidir con datos, no con teoria (cuando haya kernel)
Medir en este orden, mismo `tools/glbench` que ya usamos (cambia solo el driver):
1. `shader_present` y `core_mask` reales con panfrost (2 o 4 nucleos).
2. Reloj de la GPU (`DVFS index` fijo) y si se puede subir al de kbase.
3. `glbench` A (ALU) y B (triangulos) vs blob: 54,7 fps y 18,4 fps.
4. Un nucleo con GL (N64) o `glmark2-es2` para ver texturas y ancho de banda.
