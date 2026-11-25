# Comportamiento del LK y de los slots (medido en la Slide, 2026-11-24)

## Slots
- `misc` es `bootloader_control` estandar de AOSP (magic BCAB en misc+0x800); ver docs/slide-gpt.md.
- `fastboot set_active b` pone `slot_suffix=_b`, b prio 15, a prio 14.
- El LK **descuenta `tries_remaining` en cada intento** de un slot sin `successful_boot` (uboot_log: "Attempting slot b, tries_remaining 6/5/4/3/2") y, al agotarse, lo da por no arrancable ("unbootable slot ... tries_remaining: 0") y **cae al otro slot**. Es una red de seguridad propia: un `boot_b` que no marca exito vuelve solo a GammaOS tras ~7 intentos, sin BROM.
- La Slide volvio sola a GammaOS (slot A) tras ~15 min con mi boot_b de prueba. `misc` quedo con suffix `_b` y prioridades invertidas; restaurado desde Android con `dd` de dump/min/misc.img (md5 534e94b7 verificado).

## Lo que SI se probo con boot_b = out/boot-min.img (kernel de serie + initramfs minimo)
- El LK cargo mi imagen: uboot_log registra `generic ramdisk size is 1348509` (el de mi initramfs) y `boot_b kernel read OK`, y llego a `start_linux` (PC 0x80080000), 4 veces.
- NO se sabe si mi `/init` llego a ejecutarse: pstore vacio al volver (el reinicio forzado y/o el pmsg de Android lo borran). La pantalla se queda en el texto del LK porque el kernel no dibuja nada (loglevel=0, sin consola grafica): es normal.

## Aviso de pantalla del LK
`LOCK FLAG IS : UNLOCK / press power button to pause / SKIP VERIFY` sale en cada arranque. No hay ninguna tecla de fastboot confirmada para la Slide (uboot_log solo dice `no key pressed!`, `get mode from keypad:0`). Fastboot se entra hoy con `adb reboot bootloader`.

## fastboot boot
No ejecuta la imagen enviada: el LK la descarga y arranca la de flash (ver stock-boot.md).

## BROM / spd_dump
- Direcciones de carga de los FDL de la Slide, del XML del .pac: **FDL1 0x65000800, FDL2 0xb4fffe00** (no las 0x5500/0x9efffe00 de la T618).
- `scripts/brom-restore-misc.sh` esta preparado pero **spd_dump nunca ha conectado con la Slide** (el equipo volvio solo antes de entrar en BROM). Sin probar.

## Prueba con init que escribe marcas en boot_b (2026-11-24 23:33-23:55)
- boot_b = out/boot-rescue.img (init completo con flush_marks). El LK lo cargo 3 veces (`ramdisk size is 1350563`); el usuario vio ~90 s de pantalla del LK y reinicio (coincide con RESCUE_TIMEOUT=90 del init, pero tambien podria ser el watchdog que arma el LK: `wdt was enabled by miscdata`).
- Marcas: NINGUNA (zona boot_b+48 MiB a ceros). **Inconcluso**: flush_marks necesita /sys/class/block/sda37 y el UFS solo existe tras cargar ufs-sprd-qogirn6pro.ko (linea 15 de modules.load); si el init o ese insmod se quedaron antes, no hay marca posible. pstore tambien vacio al volver.
- Siguiente diseno: marcar ANTES de cargar modulos y en un medio que no dependa del UFS (ramoops en 0xfff80000, que el LK/Android pueden limpiar) o cargar primero solo la cadena minima hasta el UFS y marcar tras cada modulo.
- misc volvio a suffix _b / prio b=15 tras la prueba; restaurado con dd (md5 534e94b7).

## Prueba 3 (23:50-23:52): scripts/slot-b-once.sh — NO probo nada
- Escribi boot_b (verificado byte a byte) y `misc` con suffix `_b`, b prio 15 tries 1, desde Android con dd, y `adb reboot`. No relei `misc` antes de reiniciar.
- Resultado: la Slide volvio a GammaOS en ~33 s (dmesg del PC: desconexion 23:51:31, Android 23:52:04), lo que no cuadra con un intento de B (LK ~14 s + 60 s de init). uboot_log no contiene el ramdisk 1350568 de esta imagen. `misc` quedo IDENTICO al original de la fase 0 (0 bytes distintos).
- Android no revierte `misc` mientras corre (reescrito despues y estable 60 s). Quien lo devolvio al original durante el reinicio NO se ha averiguado. Hipotesis sin comprobar: el apagado de Android o un paso previo al LK.
- Lo que si funciono antes: `fastboot set_active b` (lo escribe el propio LK), con ~7 intentos de B.
- Esta prueba no aporta datos sobre el init. pstore: solo pmsg-ramoops-0 de Android (sin cadenas rgslide).
