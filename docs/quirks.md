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

## Prueba 9 (01:28-01:33) y analisis: el gadget SI se activa; el RNDIS no pasa datos
- Sin sipa_usb/sipa_eth: el PC tuvo 12 `NETDEV WATCHDOG: transmit queue 0 timed out` (vs 4 con ellos en la prueba 8). `rxtx=usb0: 0 0 0 0` todo el tiempo. Reinicio solo otra vez a ~283 s.
- `ifname` de la funcion rndis no es editable (se queda `usb0`).
- CORRECCION: el dwc3 SI completa la secuencia del stock en mi init: runtime_suspend -> b_sess_vld -> runtime_resume -> `otg_start_peripheral: turn on gadget` -> `peripheral state` -> `configfs-gadget gadget: high-speed config #1` -> USB_STATE=CONFIGURED (20.6-21.4 s), igual que GammaOS (10.50-11.02 s). power/control es `auto` en GammaOS y runtime_suspend ocurre tambien en el init: no es la causa.
- El dwc3 del DT tiene `usb-pam = <0x52>` (pamu3): `sipa 25220000.sipa: sipa_pam_connect ep id is 0` aparece al configurar; el camino de datos USB de este SoC pasa por el IPA (pamu3/sipa). `sprd_u_ether` no importa simbolos sipa; `sipa_pam_connect` lo exporta sipa-core y lo usan sprd-pamu3 y sprd_wlan_combo.
- Siguiente: probar la consola `gser` (u_serial, fuera del IPA) desde el PC: `usbserial generic` + `new_id 1d6b 0104`; getty en bucle (gs_close lo mata en cada reconexion).

## Prueba 10 (01:41-01:46): CONSOLA SERIE gser FUNCIONA (primera shell en la Slide)
- PC: `echo '1d6b 0104' > /sys/bus/usb-serial/drivers/generic/new_id` (usbserial generic) -> aparece `/dev/ttyUSB0` al enumerar el gadget.
- Slide: getty en bucle sobre ttyGS0 con `-l /bin/sh`. Respuesta recibida en el PC: `AT\r\n/bin/sh: ` -> el eco del `AT` (sondeo de ModemManager en el PC, que llego antes que mi comando) y el error de la shell. Canal bidireccional con la shell del init.
- RNDIS sigue sin pasar datos (rxtx usb0 = 0) con sipa cargado. La Slide vuelve a reiniciarse sola a ~283 s.
- Pendiente: excluir ttyUSB0 de ModemManager (regla udev ID_MM_DEVICE_IGNORE) y usar la consola de forma interactiva.

## Pruebas 11-14 (01:41-02:17): consola y red FUNCIONAN; el reinicio a ~283 s sigue sin explicar
- **Consola serie gser: OK.** PC: regla udev tools/udev/91-rgslide-rescue.rules (ModemManager ignorado, usbserial generic, /dev/rgslide-console). Slide: getty en bucle en ttyGS0. scripts/slide-console.py ejecuta comandos con su codigo de salida (19/19 en la primera sesion).
- **Red: OK, y la causa del fallo era la interfaz.** En este SoC el RNDIS entrega los datos por el IPA: `usb0` queda `up`/carrier=1 pero con contadores a 0; los paquetes entran/salen por **`sipa_usb0`** (creada por sipa_usb.ko). Con la IP 10.55.0.2/24 en sipa_usb0: ping 4/4 y telnet desde el PC. El init ya lo hace solo. Mi hipotesis anterior de que sipa competia con el RNDIS era falsa (quitar sipa_usb/sipa_eth empeoro: sin ellos no existe la interfaz buena).
- NetworkManager del PC retira la IP 10.55.0.1/24 de la interfaz del gadget: `nmcli dev set <if> managed no` o reponerla (scripts/uptime-watch.sh).
- **El kernel de serie no registra /dev/watchdog** (watchdogd de Android falla igual). sprd_wdt_fiq (timeout 40 s, "pet" cada 8 s en GammaOS) no escribe ninguna linea en mi init.
- Cadena de fabrica ordenada por dependencias (scripts/gen-factory-order.py -> rgslide-factory.load): con insmod ya no falla ninguno (antes sdhci-sprd, sensorhub, slog_bridge). sprd_pmic_wdt.ko (dlkm) tambien carga.
- **Reinicio espontaneo: SIGUE.** Corte a 279.6 s (ultimo uptime leido por telnet) con TODO cargado (sensorhub, sprd_pmic_wdt, cadena ordenada) y con red activa; ultima marca 284.56 s. No hay panico, ni thermal, ni mensaje en pstore: el log se corta en seco (SPRD_PDBG 283.9 s, luego nada). Descartado: mi temporizador, la red, sipa, sprd_pmic_wdt, el orden de modulos.
- Candidatos que quedan sin probar: watchdog de hardware armado por el LK/BootROM que solo Android alimenta (nada en sprd_wdt_fiq lo hace en el init), el CM4 (sensorhub cargado pero sin su firmware/servicio), o un limite de energia/carga.

## Dato del usuario (2026-11-25): los GSI en la RG Slide tambien se reinician cada ~5 min o menos, sin fin
- Un GSI usa el kernel y el vendor de fabrica, como nosotros. Que reinicie igual apunta a algo que el Android de fabrica / GammaOS hace y un GSI o Linux no (no es de mi init ni de Debian).
- Compatible con el corte medido a 279-285 s (4 min 44 s). Intervalo del usuario aproximado: no es una prueba.
- Sin documentacion encontrada en la web ni en third_party/ (UnisocBypass, rg-rotate-linux, base618). Hipotesis principal: el CM4 (pm_sys cargado por modem_control en GammaOS a ~10 s, tras lo cual aparece `cm4 watchdog on/off` y `CM4 Version`).

## RESUELTO (2026-11-25): el reinicio a ~284 s lo evita arrancar el CM4 con modem_control
- Prueba manual en Debian: uptime 415 s+ sin corte (antes siempre ~284 s). Dmesg: `sprd-pmic-wdt: sbuf ready for pmic wdt init` y `cm4 watchdog on/off` a ~75 s.
- Lo que hizo falta para que modem_control llegue al CM4 fuera de Android:
  1. `libkernelbootcp.trusty.so` y `libtrusty.so` del vendor_a del .pac (en install-cm4.sh).
  2. Propiedades de Android: `tools/propshim/libpropshim.so` (property_get/__system_property_get con getprop de GammaOS) cargada con `LD_PRELOAD`. Sin ella: "can't get modem type!" y duerme.
  3. Enlaces `/dev/block/by-name/<PARTNAME>` (pm_sys_b, nr_modem_b, nr_phy_b...) y lectura `o+r` en esas particiones: corre como uid 1000. Sin permiso: EACCES y el reinicio llega a ~40 s (read sobre Trusty).
- Pendiente: `/dev/sctl_ch` da ENODEV en bucle (nodo de sprd-spipe dst=9, mayor dinamico); `CM4 Version` no visto. /dev sin devtmpfs: /dev/null queda 660.
- Comando de la prueba: `LD_PRELOAD=/opt/android/libpropshim.so /vendor/bin/modem_control` tras ejecutar rgslide-cm4-prep.

## Nota: ¿aplica a una GSI? (sin verificar)
- En el vendor de fabrica `modem_control` es el servicio `vendor.modem_control` (`/vendor/etc/init/modem_control.rc`, class core, user root). Una GSI conserva vendor, asi que deberia arrancar solo; si la GSI reiniciaba igual, algo le impedia llegar al CM4.
- Candidatos: by-name ausente, permisos/SELinux sobre pm_sys_b / nr_modem_b / nr_phy_b, propiedades que no coinciden, o el servicio cae antes.
- Comprobar en una GSI: `getprop init.svc.vendor.modem_control`, `dmesg | grep "cm4 watchdog"`, `logcat -s MODEM_CTRL`. Sin logs de una GSI real, no se sabe cual.

## Pantalla y GPU con el kernel de fabrica en Debian (2026-11-25)
- Modulos: `dump/live/dlkm/*.ko` + `dump/boot/rd_vendor/lib/modules/*.ko` (190, vermagic 5.4.233 = el kernel). Copiados a `/lib/modules/5.4.233/` y `depmod -a`.
- Carga que funciona: `modprobe sprd-vpu-pw-domain` (antes que nada: da el `soc:mm:power-domain@0` que esperan DPU y DSI) y luego `pwm-sprd unisoc-iommu apsys-dvfs sprd-drm`. Sin el power-domain, dpu/dsi quedan diferidos.
- Resultado: `/dev/dri/card0` con `card0-DSI-1` y `card0-DP-1`; panel `lcd_ft8201p_ylm_lbf047001h_mipi_1280x960` (vs `lcd_id=IDffff`); backlight `sprd_backlight` (max 255).
- `mali_kbase` carga: `/sys/class/misc/mali0` (dev 10:113), `Mali-G57 4 cores r0p1 0x9091`. Sin devtmpfs no hay `/dev/mali0` ni `/dev/dri/card0` (226:0): hay que mknod.
- NO hacer `rmmod sprd_drm`: oops en `drm_mode_object_unregister` (via `sprd_dpu_unbind`) y panic, reinicio. Descargar este modulo no es seguro.
- Pendiente: `status` de DSI/DP en `unknown`/`disabled` (no hay modo ni imagen aun); falta un cliente DRM (modetest/kmscube) y el driver de usuario de Mali con kbase.
- Nodos a mano: `mknod -m 666 /dev/dri/card0 c 226 0` y `mknod -m 666 /dev/mali0 c 10 113`. Con ellos `kmscube -D /dev/dri/card0` (Mesa 25.0.7) hace modeset 1280x960@60 en DSI-1 (`status=connected`, luego `enabled`; `dsi is initialized`).
- Mesa usa `llvmpipe` (software), no Mali: falta driver de usuario para kbase. `Modifiers failed!` en kmscube.
- Imagen en el panel: CONFIRMADA por el usuario (ve el cubo de kmscube, renderizado en CPU con llvmpipe).

## GPU por hardware: situacion (2026-11-25)
- Mesa 25.0.7 de Debian en la Slide: `panfrost_dri.so` solo habla con el kernel `panfrost` upstream; no contiene `kbase` ni `/dev/mali`. Con `mali_kbase` (el de fabrica) cae a `llvmpipe` (CPU).
- Mesa documenta G57 (Valhall v9) con GLES 3.1 / GL 3.1 sobre panfrost upstream.
- Existe `zenithblue-oss/panvk-kbase-android` (PanVK sobre mali_kbase), pensado para Android y Vulkan; no verificado para G57 ni en glibc.
- Opciones sin decidir: (1) Mesa propio con backend kbase, (2) libmali de Arm (bionic) en glibc con capa de compatibilidad, (3) kernel con panfrost upstream para el T820 (no se si existe).

## panvk-kbase-android: lo que dice el repo (leido 2026-11-25, no probado en la Slide)
- Mesa PanVK 26.3-devel parcheado para hablar con `mali_kbase` (`/dev/mali0`). Vulkan 1.4. Soporta G57 (v9, JM) con perfil `profiles/g57-v9-jm.json`. Probado por ellos en una G57 MC2 (Lenovo TB336FU, Android): PanProbe 41/41; PanPlay (DXVK) solo "1-9 pass".
- Es Vulkan, no OpenGL ES. Para emuladores GL haria falta Zink (OpenGL sobre Vulkan) o que el emulador use Vulkan.
- Tiene `meson/linux-aarch64-native.ini` (glibc, "NativeCode PRoot / Bachata") y mencionan "glibc gates" pasadas, pero el README, las apps y el empaquetado son Android/bionic. La via glibc existe y NO esta documentada como soportada.
- Build: fetch-mesa.sh + apply-patches.sh --profile g57-v9-jm + meson con el native file; compila contra Mesa fijado en sources.lock.
- Limitaciones declaradas: sin geometry/tessellation en algunos casos, sin BC nativo, sin display/wayland/xcb (en Android).
- Riesgo: kbase JM en G57 con el UAPI de ESTE kernel (5.4.233 de Unisoc); el repo exige coincidir `pan_arch+gpu_id+frontend+kbase_uapi`. Nuestro gpu_id es 0x9091.

## Evaluacion del driver de Arm del vendor en glibc (2026-11-25, solo lectura; no probado en la Slide)
- Blobs: `dump/android/mali/libGLES_mali.so` (43.7 MB; es EGL+GLES, md5 310ee915) y `vulkan.ums9620.so` (MISMO fichero, mismo md5: un solo blob para GLES y Vulkan 1.4), `gralloc.default.so`. Version r40p0-01eac0 = la del modulo kbase de la Slide (UK 11.36): no hay desajuste de UAPI.
- NEEDED: allocator-V1-ndk, libbinder_ndk, libnativewindow, mapper@4.0, libgralloctypes, libhidlbase, libhardware, libdmabufheap, libcutils, liblog, libbase, libutils, libc++ (todo Android, bionic).
- Simbolos Android sin resolver (45): ANativeWindow_* (16), AHardwareBuffer_* (6), AServiceManager_checkService, __system_property_*, android::base::CachedProperty, android::gralloc4::decode*, hidl_string, IMapper::getService, RefBase.
- Plataforma de salida: EGL solo `EGL_KHR_platform_android`; sin gbm, sin wayland/x11, sin /dev/dri. Vulkan: solo `VK_ANDROID_native_buffer` (+ dma_buf/drm_modifier de memoria); sin VK_KHR_display ni wayland/xcb. Para sacar imagen a `/dev/dri/card0` habria que emular ANativeWindow/AHardwareBuffer o escribir un WSI propio.
- Kernel de la Slide: binder, ION y dma_heap compilados (`/proc/devices`: binder 235, dma_heap 248); faltan los nodos (sin devtmpfs).
- Camino realista: libhybris (no esta en el apt de la Slide; hay que compilarlo) + servicios Android minimos (servicemanager, allocator@4.0-service) + una plataforma EGL de hybris que muestre en DRM. Es un proyecto largo y fragil; no lo hemos hecho nunca en este SoC.

## HITO 1 CONSEGUIDO (2026-11-25): la Mali-G57 renderiza en Debian con el blob de Arm del vendor
- `out/gltest` (bionic, NDK) bajo `linker64`, uid 1000, `LD_PRELOAD=libpropshim.so`: `EGL_VENDOR ARM`, `EGL 1.4 Valhall r40p0-01eac0`, `GL_RENDERER Mali-G57`, `OpenGL ES 3.2 v1.r40p0-01eac0`. Triangulo con degradado en pbuffer 256x256, 17959 colores, pixel de fondo azul correcto. Sin binder, sin servicemanager, sin gralloc: el render offscreen (pbuffer) no los necesita.
- Lo que hizo falta: (1) `scripts/extract-mali-deps.sh` + `scripts/install-mali.sh` (120 ficheros de system/vendor a /opt/android con enlaces); (2) `scripts/gpu-prep.sh` (modulos en orden seguro, nodos /dev: card0, mali0, ion, binderfs montado en /dev/binderfs con enlaces binder/hwbinder/vndbinder; dma_heap NO aparece en /sys/class); (3) propshim ampliado con `__system_property_find/read/read_callback` (el blob lee `ro.vendor.arm.egl.configs.*` por la API baja; sin ella abortaba en 0x88f5f8 al construir las configs EGL).
- Diagnostico del abort: `tools/gltest/abortbt.c` (LD_PRELOAD que imprime modulo+offset del SIGABRT).
- Pendiente: surfaces de ventana (ANativeWindow/gralloc/binder), salida al panel (`tools/drmshow`, sin probar), Vulkan (el blob es un modulo HAL; `vulkan.ums9620.so` es el mismo fichero), servicio de arranque, rendimiento (sin medir).

## Analisis estatico de gralloc/binder en libGLES_mali.so (subagente, 2026-11-25; confirmado por gltest solo para offscreen)
- pbuffer / surfaceless / FBO: NO usan binder. `eglCreatePbufferSurface` (0x837c40) no llama a binder, gralloc ni ANativeWindow. Los buffers salen de `libdmabufheap` (0x18d0ad0: CreateDmabufHeapBufferAllocator / MapDmabufHeapNameToIonHeap / DmabufHeapAlloc) sobre `/dev/dma_heap/*` o ION, mas `/dev/mali0`.
- binder SI hace falta en: surfaces de ventana (winsys ANativeWindow_dequeueBuffer 0x82619c/0x8272bc), AHardwareBuffer_allocate (0x775b10), swapchain de Vulkan, clCreateBuffer/clImportMemoryARM (IMapper::getService "default" 0x89055c).
- Sondeo `AServiceManager_checkService` (0x892960): busca el allocator AIDL `IAllocator/default`; vendor_a solo trae el HIDL `allocator@4.0`, asi que ese sondeo probablemente da false aunque se arranquen los servicios. Hay vía alternativa dentro del blob, no verificada.
- Orden si hiciera falta: servicemanager (/dev/binder) -> hwservicemanager (/dev/hwbinder, necesita libvintf + manifiestos) -> vndservicemanager (/dev/vndbinder) -> allocator@4.0-service (user system, group graphics drmrpc).
- Hueco: el nombre del heap dma_heap sale de una tabla global (0x1a7cfe0/0x1a7d000). En la Slide `/dev/dma_heap` no existe y el render funciono igual: probablemente usa ION (`/dev/ion` existe). Sin comprobar.
- Otras rutas que mira: /vendor/etc/mali_platform.config (no existe, opcional), /proc/self/*, /proc/cpuinfo, sockets perfetto (opcionales), props `vendor.mali.*` y `ro.vendor.arm.gralloc.*`.

## drmshow en la Slide (2026-11-25)
- `drmshow -p`: patron de prueba en el panel DSI (1280x960@60); el usuario ve "varios colores y franjas". CREATE_DUMB funciona (DRM_CAP_DUMB_BUFFER=1), ADDFB2 y SETCRTC legacy OK; no hizo falta el camino atomico.
- Restaurar el CRTC original da ENOENT: el driver libera la memoria del logo de arranque (fb 183) en el primer modeset. El panel conserva el ultimo patron.
- `drmshow /tmp/gltest.ppm` muestra la imagen renderizada por la Mali (256x256 escalada): CONFIRMADO por el usuario (ve el triangulo). Camino completo: Mali (GLES 3.2) -> glReadPixels -> ppm -> dumb buffer -> DSI. Es una copia por CPU, sin rendimiento.

## Rendimiento: Mali-G57 (blob de Arm) vs llvmpipe (2026-11-25) - `tools/glbench`, `scripts/build-glbench.sh`
- Mismo fuente, 1280x960, FBO offscreen RGBA8, 4 s por prueba tras 1 s de calentamiento, `glFinish` por frame, sin presentar. Slide con 8 nucleos; Mali a 850 MHz (`cur_freq`).
| prueba | llvmpipe (Mesa 25.0.7, CPU) | Mali-G57 (r40p0) | relacion |
|---|---|---|---|
| A: shader ALU pesado, 3 pasadas de pantalla/frame | 5.37 fps, 19.8 Mpix/s | 54.66 fps, 201.5 Mpix/s | x10.2 |
| B: 1M triangulos pequenos/frame | 1.65 fps, 1.7 Mtri/s | 18.45 fps, 18.4 Mtri/s | x11.2 |
- Limites de la medida: es sintetica; no mide texturas, ancho de banda de memoria, ni la presentacion al panel. El pixel central difiere entre las dos (226,39,129 vs 196,254,78) porque el shader A itera sin/cos de forma caotica y la precision float difiere; no es un fallo de render. Solo se ha medido una vez.

## Ventanas, paso 1: servicios de Android bajo linker64 (2026-11-25)
- `servicemanager` abortaba con `Check failed: gSehandle != nullptr`: faltaban `/system/etc/selinux/plat_service_contexts` y `/vendor/etc/selinux/vendor_service_contexts` (copiados de system_a/vendor_a a `dump/android/root/{system,vendor}/etc/selinux/`). Luego fallaba con `Unknown class service_manager` por el libselinux real sin politica cargada.
- Solucion: `tools/selinuxshim/selinuxshim.c` -> libselinux.so de sustitucion (sin libc, permite todo: check_access=0, getcon fijo `u:r:shell:s0`, selabel_lookup devuelve contexto fijo). Se instala sobre `/opt/android/system/lib64/libselinux.so` (el original queda en `.orig`). Cubre todos los simbolos que importan servicemanager, hwservicemanager y vndservicemanager.
- `scripts/android-services.sh` (a /usr/local/sbin/android-services): start/stop/status de servicemanager, vndservicemanager (/dev/vndbinder), hwservicemanager y allocator@4.0-service, como uid 1000 con propshim. Los 4 siguen vivos pasados >14 s; el allocator espera en BINDER_WRITE_READ.
- Aun NO comprobado: que el allocator se registre en hwservicemanager y que el blob consiga reservar un buffer (paso 2).
