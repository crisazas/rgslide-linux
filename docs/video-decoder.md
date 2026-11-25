# Decodificador/codificador de video por hardware (investigacion, 2026-11-25)

Solo lectura: no se cargo ni descargo ningun modulo, no se escribio en /sys ni /dev. Todo lo marcado **(medido)** sale de un comando de esta sesion; **(general)** es conocimiento previo sin medir aqui; **(supuesto)** es inferencia.

## Resumen
- La Slide TIENE bloques de video por hardware (SoC Unisoc T820): 2 codificadores, 1 decodificador y 1 codec JPEG. **(medido, DT + modulos)**
- Pero su interfaz es **propia de Unisoc** (nodos misc con ioctls, sin V4L2). Con el kernel de fabrica no hay `/dev/video*` de codec ni V4L2 M2M. **(medido)**
- Los drivers (`vpu.ko`, `jpg.ko`) estan en `/lib/modules/5.4.233/` pero **no cargados**, y los 4 dispositivos de plataforma no tienen driver enlazado. **(medido)**
- Efecto en emulacion: practicamente nulo. Los nucleos libretro decodifican por software y ninguno de los instalados enlaza libav*/V4L2. **(medido: enlaces; general: diseno)**

## 1. Que son `vpu.ko` y `jpg.ko` (medido)
Ficheros identicos en `dump/live/dlkm/` y `dump/boot/rd_vendor/lib/modules/` (md5 `vpu.ko` 375c8231..., `jpg.ko` 873561a1...; 97448 y 80088 B). Tambien estan en la Slide: `/lib/modules/5.4.233/{vpu,jpg}.ko`.

```
$ llvm-readelf -p .modinfo vpu.ko
author=UNISOC Codec driver team  description=UNISOC VPU Driver  license=GPL
depends=unisoc-iommu,apsys-dvfs  vermagic=5.4.233 SMP preempt mod_unload modversions aarch64
$ llvm-readelf -p .modinfo jpg.ko
description=SPRD JPG Driver  license=GPL v2  depends=unisoc-iommu,sprd_camsys_pw_domain
```
- `modules.dep` (rd_vendor): `vpu.ko: apsys-dvfs sprd_soc_id unisoc-iommu`; `jpg.ko: sprd_camsys_pw_domain unisoc-iommu`. `modules.load.recovery` los carga en este orden: `jpg`, `apsys-dvfs`, `sprd-vpu-pw-domain`, `sprd-drm`, `sprd-gsp`, `vpu`.
- En la Slide ya estan cargados todos los dependientes (`lsmod`): `unisoc_iommu`, `apsys_dvfs`, `sprd_soc_id`, `sprd_camsys_pw_domain`, `sprd_vpu_pw_domain`. Solo faltan `vpu` y `jpg`.
- Fuente (ruta de compilacion en strings): `bsp/modules/kernel5.4/video/sprd-vpu/{vpu_drv.c,sprd_vpu.c}` y `.../sprd-jpg/{sprd_jpg.c,sprd_jpg_common.c}`.

### Interfaz de usuario
- **No es V4L2**: simbolos importados = `misc_register`/`misc_deregister`, `remap_pfn_range` (mmap de registros), `dma_buf_*`, `sprd_iommu_map/unmap/attach_device`, `clk_*`, `regmap_*`. Ni `video_register_device` ni `v4l2_*`. (`llvm-nm -u`)
- `vpu.ko` registra un dispositivo misc por cada uno de los 3 nodos DT (`sprd,vpu-enc-core0`, `sprd,vpu-enc-core1`, `sprd,vpu-dec-core0`); los nombres en strings son `sprd_vsp` (decodificador), `vpu_enc0`, `vpu_enc1`.
- `jpg.ko`: un misc `sprd_jpg`; compatibles que acepta incluyen `sprd,qogirn6pro-jpg` (el de esta Slide).
- Nodos que espera Android (de `vendor/etc/ueventd.rc`): `/dev/sprd_vsp`, `/dev/vpu_enc0`, `/dev/vpu_enc1`, `/dev/sprd_jpg` (0666), `/dev/sprd_jpg1`, `/dev/vdma`, `/dev/sprd_vpp`, `/dev/sprd_vsp_enc`.
- ioctls: magia `'m'` (0x6d). Del desensamblado de `vpu_ioctl`/`jpg_ioctl` aparecen los codigos 0x6d00-0x6d0f (p. ej. `0x40046d00`, `0x80046d02`, `0xc0206d0b`, `0x40206d0c`, `0xc0046d0f`, `0x40046d0e`; en jpg `0x80046d09`, `0xc0186d0b`, `0x40186d0c`). Nombres por strings: VPU `ACQUAIRE, COMPLETE, RELEASE, ENABLE, DISABLE, RESET, CONFIG_FREQ, GET_FREQ, HW_INFO, GET_SCENE_MODE` + map/unmap de dmabuf a IOVA; JPG `START, ENABLE, DISABLE, RESET, RELEASE, CONFIG_FREQ, GET_FREQ, ACQUAIRE_MBIO_DONE, GET_IOVA, FREE_IOVA`. **No se casaron nombre<->numero** (no se averiguo).
- Modelo: el driver NO decodifica H.264; solo da acceso a registros (mmap), IRQ (`complete` espera interrupcion), reloj/DVFS e IOMMU. **La logica de bitstream (parseo de SPS/PPS, tablas CABAC, comandos al bloque) esta en userspace**, en las librerias OMX de Android (ver 2). Por eso no hay formatos/limites en el .ko: los fija la libreria.

## 2. Componentes en Android (medido)
Montado `dump/pac/ex/lp/vendor_a.img` en `/tmp/opencode/vend4` (EROFS ro). Rutas dentro de vendor:
- Servicios (`etc/init/`): `android.hardware.media.c2@1.1-unisoc-service.rc` (servicio `media.unisoc.codec2`, binario `/vendor/bin/hw/android.hardware.media.c2@1.1-unisoc-service`, **ELF32 ARM**) y `android.hardware.media.omx@1.0-service.rc` (`vendor.media.omx`, `/vendor/bin/hw/android.hardware.media.omx@1.0-service`, **ELF32**). Manifest: `etc/vintf/manifest/manifest_media_c2_V1_1_unisoc.xml` (HIDL `android.hardware.media.c2@1.1` `IComponentStore/default`, hwbinder). En el `getprop` de GammaOS ambos servicios estaban `running`.
- Codec2 (`lib/`, **solo 32 bits**; en `lib64/` solo hay `libcodec2_unisoc_{common,deintl,imaadpcm,store}.so` y `libcodec2_vndk.so`): `libcodec2_unisoc_{avcdec,avcenc,hevcdec,hevcenc,vp9dec,vp8dec,av1dec,mpeg4dec,mpeg4enc,h263dec,h263enc,mp3dec,mp3enc,...}.so`, `libcodec2_hidl@1.0/1.1.so`, `android.hardware.media.c2@1.0/1.1.so`. `libcodec2_unisoc_avcdec.so` necesita `libcodec2_unisoc_common/store`, `libmemion`, `libion`, `libdmabufheap`, `libvideo_share_memory`, `libui`, `libcodec2*`, `libstagefright_foundation`.
- OMX (`lib64/` y `lib/`): `libomx_{avcdec,hevcdec,vp9dec,vpxdec}_hw_sprd.so`, `libomx_{avcenc,hevcenc,vpxenc}_hw_sprd.so`, `libomx_deintl_hw_sprd.so`, `libomx_*_sw_sprd.so` (avc, hevc, av1, m4vh263, vpx), `libsprd_omx_core.so`, `libstagefrighthw.so`, `libmemion.so`, `libvsp_ca.so` (cliente Trusty para DRM, `/dev/trusty-ipc-dev0`), `libjpeg_hw_sprd.so` (`sprd_jpg_encode/decode/get_iova`).
- Las `libomx_*_hw_sprd.so` son **autonomas**: solo dependen de libc/libutils/libcutils/liblog/libc++ y `property_get`; abren `/dev/sprd_vsp` + `/dev/vdma` (decoders) o `/dev/vpu_enc0`/`/dev/vpu_enc1` (codificadores) con `open/ioctl/mmap`. Exportan simbolos del tipo `H264DecInit/H264DecDecode/H264DecMemInit`, `H265DecInit/H265DecDecode`, `VSP_ACQUIRE_Dev`, `VSP_Free_IOVA`... **(medido: llvm-nm -D)**. Es decir, tienen una API C utilizable sin pasar por binder (**supuesto**: habria que conocer las estructuras; no hay cabeceras aqui).
- XML (`vendor/etc/`): `media_codecs_c2.xml` (+ `_v1`, `_v2`), `media_codecs.xml` (solo `OMX.google.mjpg.decoder`, software), `media_codecs_performance_c2*.xml`. Limites de `media_codecs_c2.xml`:

| Codec | Tipo | Tamano max | Otros |
|---|---|---|---|
| `c2.unisoc.avc.decoder` | H.264 | 4096x4096 | 864000 bloques/s, 50 Mbps |
| `c2.unisoc.hevc.decoder` | H.265 | 4096x4096 | 1728000 bloques/s, 50 Mbps |
| `c2.unisoc.vp9.decoder` | VP9 | 4096x4096 | 864000 bloques/s, 40 Mbps |
| `c2.unisoc.vp8.decoder` | VP8 | 1920x1088 | |
| `c2.unisoc.mpeg4.decoder` / `h263.decoder` | MPEG-4 / H.263 | 1920x1152 / 1408x1152 | |
| `c2.unisoc.av1.decoder` | AV1 | 1920x1088 | atributo `software-codec` (NO es HW) |
| `c2.unisoc.avc.encoder` | H.264 | 4096x4096 (min 176x144) | 40 Mbps |
| `c2.unisoc.hevc.encoder` | H.265 | 4096x4096 | 25 Mbps |

  Los `c2.android.*` (software de Google) quedan limitados a 1280x1280. (La tabla refleja lo que declara Android; no es medicion de rendimiento.)
- MJPEG: no hay decodificador de video MJPEG por hardware declarado; `jpg.ko`/`libjpeg_hw_sprd.so` es un codec de imagenes JPEG fijas (**supuesto**: se podria usar cuadro a cuadro para MJPEG, no probado).
- Capturas Android: `dump/android/dev-nodes.txt` esta casi vacio (7 lineas); sin ls de /dev de GammaOS. `dump/live/dmesg-gamma-now.txt` (GammaOS) muestra Android abriendo `sprd_vsp`, `vpu_enc0`, `vpu_enc1` desde t=17.6 s, con `vpu_pw_on OK`/`vpu_pw_off OK` (el stack de codec funciona en el hardware de serie).

## 3. Device tree (medido, `/tmp/opencode/slide.dts`, de la Slide viva; `/sys/firmware/devicetree/base/soc/mm` coincide)
Todos bajo `/soc/mm`, `status="okay"`:
- `video-codec@32000000` `sprd,vpu-enc-core0`, IRQ 0x95, `sprd,video_ip_version=<0x15>`, iommu `iommu@32000000` (`unisoc,iommuvaul6p-vsp1`), power-domain `vpu_pd_enc0`.
- `enc-core1@32100000` `sprd,vpu-enc-core1`, IRQ 0x94, iommu `@32100000` (`-vsp2`), `vpu_pd_enc1`.
- `video-codec@32200000` `sprd,vpu-dec-core0`, IRQ 0x93, iommu `@32200000` (`-vsp`, iova 0x20000000+1 GiB), `vpu_pd_dec`.
- `jpg-codec@36000000` `sprd,qogirn6pro-jpg`, IRQ 0x6f, iommu `iommu@36000300`, **power-domain = `mm_domain`** (el de `sprd_camsys_pw_domain`, phandle 0x37, no el de `sprd-vpu-pw-domain`).
- Power domains `power-domain@0..3` `sprd,vpu-pd`, labels `vpu_pd_top`, `vpu_pd_enc0`, `vpu_pd_enc1`, `vpu_pd_dec` (@1..@3 cuelgan de @0). Relojes: `clk_domain_eb, clk_dev_eb, clk_ckg_eb, clk_ahb_vsp(+parent), clk_src_256m/307m2/384m/512m(/680m dec), clk_vsp`; JPG: `jpg_domain_eb, jpg_dev_eb, jpg_ckg_eb, jpg_clk, clk_src_153m6..512m`. Sin reguladores propios en esos nodos.
- DVFS: `vpudec-dvfs`, `vpuenc-dvfs`, `jpg-dvfs` (compatibles `sprd,hwdvfs-*`); `vsp_dvfs_notifier_call_chain` lo exporta `apsys-dvfs.ko`.
- ¿`sprd_vpu_pw_domain` los alimenta? SI en cuanto al DT: los nodos del codec referencian `vpu_pd_*` que provee ese modulo, y esta cargado: `lsmod` -> `sprd_vpu_pw_domain 20480 0`; `/sys/bus/platform/drivers/sprd-vpu-pd/` lista `soc:mm:power-domain@0..3`; `pm_genpd_summary`: `power-domain@1/@2/@3 suspended`, el top con 3 esclavos en `on`. El jpg depende de `mm_domain` (cargado por `sprd_camsys_pw_domain`, usado por `sprd_vdsp`/`mmdvfs`).

## 4. Estado en la Slide viva (medido, ssh, 55 min de uptime)
```
/sys/bus/platform/devices/{32000000.video-codec,32100000.enc-core1,32200000.video-codec,36000000.jpg-codec}: driver= (vacio), runtime_status=unsupported
32000000.iommu, 36000000.iommu: driver sprd_iommu_drv, suspended
lsmod | grep -E 'vpu|jpg'  -> solo sprd_vpu_pw_domain
/proc/misc: sin sprd_vsp/vpu_enc*/sprd_jpg ; /dev: sin vdma*, sin video*; /sys/class/video4linux vacio
/dev/ion existe (misc 10:127); /dev/dma_heap NO existe
.config: CONFIG_V4L_MEM2MEM_DRIVERS=y pero sin ningun driver de codec; DMABUF_HEAPS=y con HEAPS_SYSTEM/CMA no definidos; ION_SYSTEM_HEAP=y, ION_CMA_HEAP=m
```
- No hay ffmpeg/ffprobe/mpv en la Slide; si `libavcodec.so.61` (Debian, sw) y `/usr/lib/aarch64-linux-gnu/gstreamer-1.0`.

## 5. Que NO existe
- Driver V4L2 (stateful/stateless/M2M) para este codec: ni en el kernel de fabrica 5.4.233 ni upstream para Unisoc **(medido en el kernel; general para upstream)**.
- Decodificador MJPEG de video, AV1 HW, VP8 de mas de 1080p: no declarados.
- Binarios Codec2/OMX de 64 bits: los servicios HIDL y `libcodec2_unisoc_*` de `lib/` son ELF32 ARM. Las `libomx_*_hw_sprd.so` de `lib64/` si son aarch64.
- `/dev/vdma`, `/dev/sprd_vsp`, etc. en Debian: sin driver cargado no hay nodos (ni devtmpfs automatico, ver `docs/quirks.md`: nodos a mano con mknod).

## 6. Lo que NO se pudo averiguar
- Mapa exacto numero-de-ioctl <-> nombre y estructuras (`struct vsp_*`): no hay cabeceras; habria que desensamblar o sacar de AOSP/Unisoc.
- Que modulo crea `/dev/vdma` (la cadena aparece en `mmdvfs.ko` y `unisoc-iommu.ko` como DVFS/IOMMU de VDMA; no es del `vpu.ko`). Las libs OMX lo abren, asi que sin el el decoder no funciona.
- Si cargar `vpu.ko`/`jpg.ko` hace probe limpio en Debian (no se cargo). Tampoco si el probe toca registros con el power-domain apagado (bus hang/panic).
- Si `/dev/ion` de este kernel basta para los buffers (las libs usan `libmemion` + ion/dmabufheap Android; con `CONFIG_ION=y` legacy es plausible, no verificado).
- Rendimiento real del HW y si Debian soporta DRM (Trusty `libvsp_ca`) en el camino; irrelevante para video sin DRM.
- Rutas `dump/android/`: sin mas contenido de codec que `getprop.txt`; `/vendor/bin/hw/*` solo se listo con sudo (permisos 0750 en el EROFS).

## 7. Efecto sobre la emulacion
**Medido aqui:** ninguno de los 7 nucleos instalados (`/opt/retroarch/lib/libretro/*.so`) enlaza libav*/libswscale/v4l/gst/va (`readelf -d | grep NEEDED`, salida vacia), ni `retroarch` (que enlaza solo GLESv2/EGL/GLdispatch; ldd). El build se hizo con `--disable-ffmpeg` (`scripts/build-retroarch.sh:24`).

**Conocimiento general (no medido aqui):**
- Los nucleos libretro entregan frames ya renderizados (`retro_video_refresh`); lo unico por HW que RetroArch expone a un nucleo es OpenGL/Vulkan via `RETRO_ENVIRONMENT_SET_HW_RENDER`. No existe un API de libretro para decodificacion de video HW.
- Los nucleos de consolas antiguas (gb/gba/nes/snes/ds/vb/ws) no tienen video comprimido; no necesitan decodificador.
- FMV de PSX (MDEC: es una unidad de la consola, se emula en software con el mismo codigo del nucleo), Saturn (Cinepak/Sofdec, software), Dreamcast (Sofdec/PVR, software), N64 (casi sin FMV): todo software dentro del nucleo, en la CPU Cortex-A7xx/A5x de la Slide. El coste es pequeno frente a emular la CPU/GPU de la consola.
- PSP (PPSSPP-libretro): decodifica FMV (H.264 de PMF) y audio (Atrac3) con **libavcodec software** embebido; no usa MediaCodec de Android en el nucleo libretro (el port Android de PPSSPP si puede usar MediaCodec, no el nucleo para Linux). Es el unico caso de los deseados donde un decodificador HW mejoraria algo, y solo si se parcheara el nucleo.
- Solo el reproductor de medios de RetroArch y la grabacion (`--enable-ffmpeg`) usarian video; a 1280x960 (panel de la Slide) la decodificacion software H.264 con libavcodec 61 NEON seria viable para 720p-ish (**supuesto**, no medido).
- Conclusion: **la ausencia de decodificador HW NO afecta a las consolas clasicas ni a PSX/N64/DC/Saturn; solo importa para ver videos o grabar, y marginalmente para PSP.** Lo que si limita es CPU/GPU (Mali-G57 por hybris), no el codec.

## 8. Caminos de integracion posibles (solo descripcion, NO implementado)
| Opcion | Descripcion | Dificultad | Riesgo |
|---|---|---|---|
| A. libavcodec/GStreamer + wrapper propio sobre las `libomx_*_hw_sprd.so` | Cargar (glibc o bionic via hybris) la API C `H264DecInit/Decode` y alimentar `/dev/sprd_vsp`; habria que adivinar las structs. Salida ya en dma-buf/IOVA | Alta (estructuras sin documentar; las libs son bionic y hoy hybris solo se usa para GL) | Medio-alto: carga de `vpu.ko` en arranque temprano ya dio panics; ioctl mal formado puede colgar el bus |
| B. libhybris + Codec2/OMX de Android | Arrancar `media.unisoc.codec2`/`omx` (ELF32: haria falta un hybris 32-bit) o enlazar `libstagefright` aarch64 | Muy alta (binder, hwbinder, 32 bits, gralloc, ion) | Alto: arrastra mucho Android; choca con lo ya hecho en hybris (no tocar) |
| C. Driver V4L2 M2M propio | Reescribir un driver stateless/stateful para el bloque VSP; 5.4 con cargas de modulos de fabrica | Muy alta (solo si hubiera hoja de datos) | Alto; inviable sin documentacion |
| D. No hacerlo (recomendado hoy) | Usar libavcodec software; revisar si hace falta solo si se quiere reproductor de video | - | Nulo |

Si se quisiera probar A o B, el orden prudente seria: (1) en una sesion sin riesgo de bucle, cargar `jpg.ko` (depende de `mm_domain`, ya cargado) y mirar dmesg; (2) cargar `vpu.ko` solo a mano tras un arranque estable, nunca desde el init, con `sprd_vpu_pw_domain` presente; (3) crear nodos con mknod desde `/proc/misc`; (4) probar un `open`+`ioctl HW_INFO` antes de cualquier decode. Comprobar antes `docs/quirks.md` (INCIDENTE de panics y NO `rmmod`).
