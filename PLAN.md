# Plan: Linux en la Anbernic RG Slide (T820, ums9620_2h10)

Solo Linux, sin microSD. Slot A = pruebas, slot B = último arranque bueno. La vuelta a Android de fábrica es el volcado de la fase 0 restaurado por BROM.

Dos etapas: **etapa 1** con el kernel de serie de Anbernic (rápida, da panel/Wi-Fi/carga), **etapa 2** con mainline (GPU Panfrost, mantenible). Lo marcado (por confirmar) no está medido.

## Fase previa
Repo, `.gitignore`, `AGENTS.md`, `PROGRESS.md`, clones en `third_party/`, herramientas (`dtc`, `libsparse-utils`, `capstone`, `mkbootimg`/`unpack_bootimg`/`avbtool`/`lpunpack` desde AOSP), `spd_dump` compilado y `.pac` de la Slide con FDL1/FDL2.

## Fase 0 — Red de seguridad (Android aún instalado)
1. Sacar datos personales. 2. GPT real (`/dev/block/by-name`), tamaños, UFS o eMMC. 3. BROM (Back mantenido al conectar USB, por confirmar). 4. Volcar todas las particiones x2 con md5. 5. Aparte, duplicado: `miscdata`, `prodnv`, calibración, MAC, batería.
Checkpoint: restaurar `dtbo_b` y Android sigue arrancando. Apuntar los combos de fastboot por botones y de BROM desde apagado.

## Fase 1 — Desbloqueo y observación del LK
Desbloquear (`Bootloader_Unlock_Anbernic_T820`, borra `userdata`), `getvar all`, `vbmeta_a` con verificación desactivada, `boot_a` de serie repaquetado sin firmar. Romper el `boot_a` a propósito y contar arranques; comparar `misc` antes/después para saber si el control de slots es AOSP o propio de Unisoc. Salida: `docs/quirks.md`.

## Fase 2 — Arranque de serie por dentro (lectura)
Desempaquetar `boot`/`vendor_boot`/`init_boot`, `lpunpack` de `super`, DTB y `dtbo` a DTS, orden de módulos, demonios de Wi-Fi/audio/carga y su firmware. Salida: `docs/stock-boot.md`, `docs/hardware.md`.

## Fase 3 — Initramfs propio con el kernel de serie
Se deja `vendor_boot` de serie; se cambia solo la ramdisk genérica (`init_boot` si existe, si no `boot`). `/init` carga módulos, monta configfs, consola por **ACM en configfs** (no `g_serial`). Logs de respaldo en `userdata` y `pstore`. Checkpoint: shell por `picocom`.

## Fase 4 — Rootfs Debian en `userdata`
ext4 con `mkfs.ext4 -d`, `img2simg`, `fastboot flash userdata`, `resize2fs` al arrancar. Montaje por `PARTNAME=userdata` desde `/sys/class/block/*/uevent`, no por PARTUUID. Checkpoint: SSH sobre USB y systemd sin fallos críticos.

## Fase 5 — Control de slots desde Linux
Servicio que marca el arranque bueno en `misc` con el formato averiguado en la fase 1, más `promote-a-to-b`. Checkpoint: 10 reinicios sin que baje `tries_remaining`; un `boot_a` roto cae en B.

## Fase 6 — Periféricos, en orden
Panel → táctil → mando → Wi-Fi/BT → carga → térmico pasivo → audio → suspensión con wake por power y hall. Sin GPU: con kernel de serie solo hay llvmpipe. Salida: `DEVICE-BRINGUP.md`.

## Fase 7 — Utilizable
Frontend en KMS, arranque automático, botón de encendido, pantalla "Connect to PC" de **base618** mientras no haya frontend.

## Etapa 2 — Mainline
M1 kernel `mu300-linux` sin módem/SIPA. M2 DTS propio partiendo del de la RG556, corregido con el de serie (panel, táctil, mando `singleadc-joypad`+`gpio-keys`, hall, sin `pwm-fan`, UFS/eMMC). M3 `vendor_boot` v4 con `mkbstub`/`build_vendor_boot_img.sh`/`mkdtimg.py`. M4 panel `panel-generic-dsi`, **Panfrost**, térmicas pasivas a mano, audio, carga, suspensión.

## Opcional
Parchear el LK (`UMS9620_PORT.md`, `magic_pack_ums9620.py`, `patch_uboot_unlock_ums9620.py`), offsets recalculados con objdump+capstone y `--dry-run`. Antes: cómo acepta el SPL un `uboot` modificado y si hay `uboot_a`/`uboot_b`.

## No se toma
`flash-spl.sh`, fork de kernel y `make-card.sh` de base618; scan `extlinux` y árbol de kernel de rg-rotate-linux; `patch_spl_ums9620.py`; `flasher` y offsets viejos de pmOS; `super` y PARTUUID generados.
