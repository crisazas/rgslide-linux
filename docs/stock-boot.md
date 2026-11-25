# Arranque de serie de la Slide (GammaOS, medido 2026-11-24)

Fuente: lectura con adb+su de boot_a/vendor_boot_a/init_boot_a/dtb_a/dtbo_a (dump/boot/, md5 verificado contra el equipo) y /proc/config.gz (dump/live/config.txt). Nada escrito en flash.

## Imagenes
- `boot_a`: header v4, kernel 38 MB (5.4.233), ramdisk 1.3 MB LZ4 **parcheada con Magisk** (.backup/.magisk). cmdline: `enforcing=0 audit=0 androidboot.selinux=permissive`.
- `vendor_boot_a`: header v4, page 0x1000, kernel@0x8000, ramdisk@0x5400000, dtb@0x1f00000 (159462 B), 1 vendor ramdisk LZ4 de 23.6 MB.
- `init_boot_a` y `dtb_a`: **todo ceros** (el DTB real va dentro de vendor_boot). `boot_b` difiere de `boot_a`.
- `fastboot boot <img>` NO prueba nada en este LK: descarga la imagen (`image len=0x259e000`) pero luego arranca el `boot_a` de la flash (uboot_log: `load boot.img succeed`, `boot_a kernel read OK`, ramdisk 1306498 = el instalado). Corrige una afirmacion anterior de este documento. Para probar un kernel/initramfs propio hay que escribirlo en una particion.

## cmdline del LK (completo en dump/boot/cmdline.txt)
`earlycon=sprd_serial,0x20210000,115200n8 console=ttyS1,115200n8 ... androidboot.hardware=ums9620_2h10 androidboot.dtbo_idx=2 lcd_name=lcd_ft8201p_ylm_lbf047001h_mipi_1280x960 lcd_size=960x1280 lcd_base=b0000000 sprdboot.flash=ufs androidboot.ddrsize=8192M androidboot.boot_devices=soc/soc:ap-ahb/22000000.ufs androidboot.slot_suffix=_a androidboot.auto.efuse=T820 rfboard.id=2 marlin.clktype=1 androidboot.product.hardware.sku=wifionly`
- Panel confirmado: **FT8201P, 1280x960** (framebuffer 960x1280 en 0xb0000000). UFS. 8 GB RAM. Consola serie UART en 0x20210000 (ttyS1).

## fastboot (LK, desbloqueado)
`current-slot: a`, `slot-count: 2`, `max-download-size: 0x8000000` (128 MiB), `partition-size:userdata: 0x1bfa400000` (~114.6 GiB), `partition-type: LK`, `is-userspace: no`. `getvar all` no devuelve nada; `slot-successful/retry-count/unbootable` salen vacios (no los expone). `version: 1.0`.

## USB gadget del kernel de serie (afecta a la fase 3)
- NO hay `USB_CONFIGFS_ACM`, `ECM`, `NCM`, `RNDIS`, `EEM` (comentados en config). SI hay `USB_CONFIGFS_SERIAL` (funcion `gser`), `F_FS` (FunctionFS: adb/mtp) y `mass_storage`.
- RNDIS existe como **modulo vendor**: `/vendor_dlkm/lib/modules/sprd_usb_f_rndis.ko` + `sprd_u_ether.ko`.
- UDC: `25100000.dwc3` (y `musb-hdrc.2.auto`). configfs en `/config/usb_gadget/g1` (el init de Android lo monta ahi).
- Consecuencia: el initramfs no puede usar ACM; opciones = `gser` (el host necesita driver vendor-specific `usbserial`), FunctionFS con adbd, o RNDIS del modulo vendor. Hay que probarlo con `fastboot boot`.
- pstore: `PSTORE_RAM`/`PSTORE_CONSOLE` activos en el kernel; en dmesg no aparece ramoops (hay que ver si el DT lo declara).

## Modulos
189 cargados en marcha; 137 `.ko` en /vendor/lib/modules y 137 en /vendor_dlkm/lib/modules.

## Prueba con `fastboot boot` (2026-11-24 23:04) — resultado: la imagen se ignora
- `scripts/try-boot.sh out/boot-min.img`: sin gadget `1d6b:0104`, sin marcas `RGSLIDE-MIN` en pstore, Android de vuelta a los ~26 s con `androidboot.mode=fastboot` y `bootcause="Reboot into fastboot"`.
- Evidencia en `dump/live/try/boot-min-230432/uboot_log.txt` (volcado de la particion `uboot_log`, 16 MiB; util para depurar el LK sin cable UART).
- `uboot_log` registra cada arranque del LK con su cmdline final, la carga de boot/vendor_boot y el DTB elegido: es la mejor fuente de diagnostico sin UART.
- El LK elige DTB por `sprd,sc-id=ums9620 2h10` y `dtbo_idx=2`; en el log aparece `mismatching dtbo entry(id:0x2700)`.
