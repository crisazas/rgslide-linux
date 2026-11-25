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

## Prueba 5 (00:26-00:43): ramdisk en LZ4 — el kernel YA ejecuta mi /init
- Con la ramdisk en LZ4 legacy el kernel descomprime la cadena (vendor LZ4 + mia LZ4) y arranca `/init` (pstore: `Run /init as init process`, 3.15 s).
- `/init` muere a los 20 ms: `Kernel panic - not syncing: Attempted to kill the init task!` (exit_group de PID 1). Causa: `exec </dev/console >/dev/console` falla porque el kernel no tiene ningun tty de consola registrado (`unable to open an initial console`; solo `ramoops-1` esta habilitada) y en ash un `exec` con redireccion fallida mata el shell. Reproducido con busybox bajo qemu.
- Corregido: abre /dev/console solo si se puede; si no, cae a /dev/null.
- `adb reboot bootloader` deja `bootonce-bootloader` en misc+0 si fastboot no llega a abrirse; hay que restaurar misc a mano. `fastboot fetch` NO esta soportado en este LK (no se puede leer boot_b desde fastboot).
- `fastboot set_active b` hace reiniciar el LK por si solo: no hace falta que el usuario reinicie.

## Prueba 6 (00:46-00:54): el init corre y carga modulos; USB y UFS fallan
- Marcas en boot_b (+48 MiB): FUNCIONAN. `RGSLIDE-MARK` con el log completo del init desde t=2.9 s (uptime del kernel) hasta t=43 s.
- La cadena de modulos de fabrica carga bien (ufs-sprd-qogirn6pro.ko en t=3.58). **boot_b no aparece en /sys/class/block a los 20 s** (`MARCA-3 NO aparece`): el UFS termina su probe despues; al reiniciar el kernel si sincroniza sda/sdb/sdc (t=63), asi que existe mas tarde. Por eso `restore_misc` no actuo ("misc no visible") y el LK tuvo que agotar los intentos de B. Corregido: espera 30 s tras TODA la cadena en vez de tras el modulo 15.
- Fallos de insmod por dependencias (insmod no resuelve deps): `dwc3-sprd` necesita `sc27xx_typec`/`sprd_tcpm` antes; `phy-sprd-qogirn6pro` y `phy-sprd-usb31-qogirn6pro` necesitan `sprd_usbm` y `musb_hdrc`; `musb_sprd` necesita `dwc3-sprd`. Resultado: **sin UDC** (`gadget: sin UDC en /sys/class/udc`) y sin gadget USB. `scripts/gen-usb-order.py` genera el orden por modules.dep.
- Otros insmod fallidos de la cadena de fabrica: `sdhci-sprd`, `sensorhub`, `slog_bridge` (unknown symbol): no se investigaron.
- `ptn38003a: probe of 6-0030 failed with error -5` (conmutador USB-C por I2C).
- OJO: `dmesg-ramoops-0` del pstore puede ser de un arranque ANTERIOR; comparar con cmp antes de leerlo.

## Prueba 7 (01:03-01:05): restore_misc FUNCIONA; el UDC no aparece
- `MARCA-3 boot_b visible tras 0 esperas` (UFS listo a los 18.9 s, al terminar la cadena de fabrica) y `MARCA-4 misc restaurado a slot A (verificado)` a los 19.0 s. El LK arranca A en el reinicio siguiente: GammaOS vuelve en ~2.5 min, sin agotar los 7 intentos.
- Cadena USB sin errores de simbolos; `sprd-hsphy`/`sprd-ssphy` hacen probe; `sc27xx-typec ... works as DRP`. PERO no aparece ninguna linea de `dwc3` y `/sys/class/udc` queda vacio.
- Comparacion con el dmesg de GammaOS (dump/live/dmesg-gamma-now.txt): el dwc3 hace probe a los 4.58 s, JUSTO DESPUES de `aw32257-charger 6-006a: driver registered` (4.52 s). El overlay (dtbo id 0x2100) define el regulador `vddvbus` como `otg-vbus` dentro de `charger@6a` (i2c6), y `dwc3-sprd.ko` contiene `unable to get vbus supply`. `aw32257_charger.ko` (y `sprd-charger-manager`, `sprd_battery_info`) NO estaban en mi lista: hipotesis para el UDC ausente. En el recovery de fabrica (`modules.load.recovery`) van despues de dwc3-sprd, pero los carga antes de que el dwc3 reintente su probe diferido.
- Hipotesis sin confirmar.

## Prueba 8 (01:17-01:22): el gadget RNDIS ENUMERA en el PC pero la Slide no responde
- Con aw32257_charger + deps, `dwc3-sprd` hace probe, aparece el UDC `25100000.dwc3`, el gadget `1d6b:0104` "RG Slide rescue" enumera sin errores y el PC crea la interfaz RNDIS (`rndis_host`). El init llega a "listo" (t=20.8 s) con `usb0 -> 10.55.0.2/24` y el UDC pasa a `configured` / `high-speed` (t=23.8 s).
- El PC emite 153 paquetes (ARP, DHCP, mDNS, IPv6) y recibe 0: la Slide no contesta a nada.
- pstore (t=266-285 s): `dwc3-sprd ... idle state / b_sess_vld / waiting dwc3 suspended` en BUCLE (~16 veces/s) y `sipa_dummy: ndev sipa_usb0 down` repetido. Hipotesis: `sipa_usb`/`sipa_eth` (modem IPA) comparten el gadget y compiten con rndis. `sipa-core` importa el nombre de netdev.
- La Slide se reinicio SOLA a los 284.6 s de uptime (01:22:13), ~2 min ANTES de mi temporizador (102.9 + 300 s): NO fue mi timeout. Sin panico en pstore, sin `bootcause` util (`Reboot into normal`). Hipotesis sin comprobar: watchdog (`sprd_wdt_fiq`, timeout 40 s; GammaOS lo alimenta cada 8 s) — el init no lo alimenta pero tampoco habia reiniciado a los 100 s en pruebas anteriores.
- Mis contadores `rxtx` eran erroneos: `grep usb0:` casaba con `sipa_usb0:` (modem). Corregido a `^ *(usb0|rndis0):`.
