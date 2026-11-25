# tools — empaquetado de imágenes Android

Herramientas para construir y desenvolver `boot.img` y `super.img` de la RG Slide.
Todas se invocan desde la raíz del repo (`tools/…`); los artefactos van a `out/`.

## Procedencia y versiones

| Herramienta | Origen | Versión / commit |
|---|---|---|
| `mkbootimg.py` | AOSP `platform/system/tools/mkbootimg` (rama `main`) | descargado 2026-11-24 |
| `unpack_bootimg.py` | ídem | ídem |
| `gki/generate_gki_certificate.py` | ídem, subdir `gki/` | ídem (dependencia de `mkbootimg.py`) |
| `avbtool.py` | AOSP `platform/external/avb` (rama `main`) | se autoinforma `avbtool 1.3.0` |
| `lpunpack` | https://github.com/unix3dgforce/lpunpack (`master`, commit `c59b8f3`) | script Python puro; se le añadió shebang |

Los ficheros de AOSP se bajaron con `?format=TEXT` + `base64 -d` desde
android.googlesource.com. `mkbootimg` y `unpack_bootimg` son envoltorios `sh` que
hacen `exec python3` sobre el `.py` correspondiente.

## Dependencias del sistema

- `device-tree-compiler` 1.7.0 (`dtc`, para DTB).
- `android-sdk-libsparse-utils` 1:34.0.4 (`img2simg`, `simg2img`).
- `capstone` 5.0.9 vía `pip --user` (instalado con `--break-system-packages` por PEP 668).

## Invocación

```
tools/mkbootimg --kernel out/Image --ramdisk out/initramfs.cpio.gz \
    --cmdline "console=ttyS0" --header_version 4 --pagesize 4096 -o out/boot.img
tools/unpack_bootimg --boot_img out/boot.img --out out/unpacked
tools/lpunpack out/super.img out/particiones          # extrae todas
tools/lpunpack -p system_a out/super.img out/particiones
tools/lpunpack --info out/super.img                   # metadatos en texto
tools/avbtool.py add_hash_footer --image out/boot.img \
    --partition_size <bytes> --partition_name boot
tools/avbtool.py verify_image --image out/boot.img --key <clave.pem>
```

Cada herramienta acepta `--help`. `avbtool.py` se invoca con la extensión; no hay
envoltorio para él.

## Verificado (2026-11-24)

- `mkbootimg --help`, `unpack_bootimg --help`, `lpunpack --help`, `avbtool.py --help`: OK.
- Ida y vuelta real: `mkbootimg` con kernel/ramdisk falsos → `unpack_bootimg` devuelve
  ambos ficheros idénticos byte a byte.
- `lpunpack` extrae una partición `system_a` de un `super.img` de prueba con datos
  correctos.

## Limitaciones

- `lpunpack` no implementa el selector de slot (`-S`); extrae del slot 0.
- Compilar `lpunpack` en C no es posible ahora mismo: `libc6-dev` no está instalado y
  choca con el `libc6` 2.43 retenido en el sistema. Por eso se usa el script Python.
- `avbtool.py` está sin envoltorio y sin probar contra claves reales.
