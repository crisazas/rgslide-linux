# PROGRESS

## Ahora
- Slide en GammaOS, slot A activo, misc restaurado (md5 534e94b7). boot_b contiene out/boot-min.img (prueba); boot_b original en dump/boot/boot_b.img.
- Hecho: volcado minimo (dump/min), GPT, arranque de serie medido, init de rescate, imagenes boot-min/boot-rescue. El LK SI carga y arranca un boot_b propio (uboot_log).

## Siguiente
- Saber si /init llega a ejecutarse: dejar una marca en una particion no critica (blackbox/sysdumpdb), no en pstore. Cada set_active b = ~7 intentos y vuelve solo a A.
- Probar init completo (RNDIS por modulo vendor + gser) con la misma via boot_b.
- Rootfs Debian: out/rootfs.ext4 listo (sparse 937 MB); aun sin flashear.

## No olvidar
- fastboot boot NO prueba imagenes en este LK: hay que escribir boot_b.
- Entrada a fastboot: adb reboot bootloader. BROM (Back+USB) y spd_dump sin probar; FDL: 0x65000800 / 0xb4fffe00.
- Subagentes no commitean. Dump/out no se versionan.
