# GPT real de la Slide (medido por adb+su, 2026-11-24, solo lectura)

- UFS Samsung `KM8L9001JM-B624`, `/dev/block/sda`, **sectores de 4096 B**, 119.2 GiB, 73 particiones. `spl_a`/`spl_b` son `sdb`/`sdc` (LUN de boot).
- Equipo de partida: **GammaOS** (GSI Lineage, `tdgsi_arm64_ab`), kernel 5.4.233, bootloader desbloqueado (`orange`), slot activo `_a`. No es el Android de fabrica.
- Existen `init_boot_a/b`, `vendor_boot_a/b`, `dtb_a/b`, `dtbo_a/b`, `uboot_a/b`, `sml`, `trustos`, `teecfg`, `hypervsior`.
- Mapa: boot_a sda36 (64 MiB), vendor_boot_a sda38 (100), init_boot_a sda40 (8), dtb_a sda42, dtbo_a sda44, super sda46 (5600), metadata sda51 (64), userdata sda73 (114596 MiB), uboot_a sda8 (3), misc sda3 (1).

# misc (confirmado, no es una variante propia)
`bootloader_control` estandar de AOSP en `misc+0x800`: suffix `_a`, magic `BCAB`, version 1, nb_slot 2, crc32 de los primeros 0x1c bytes en `+0x1c` (**coincide**).
Slot byte: priority(4 bits) | tries_remaining(3 bits)<<4 | successful_boot<<7, y un segundo byte con verity_corrupted.
Estado medido: a = prio 15, tries 1, ok 1; b = prio 14, tries 1, ok 0.
