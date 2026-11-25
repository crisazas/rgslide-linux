# initramfs de rescate (fase 3)

Ramdisk mínimo para el kernel de serie: consola por USB (gadget ACM en
configfs, no `g_serial`), red por USB (RNDIS), carga de módulos desde
`/lib/modules/modules.load` y arranque del sistema real si existe la partición
`userdata`. No lleva Android ni un rootfs completo.

## Qué hace `/init`

1. Monta `/proc`, `/sys`, `/dev` (devtmpfs), `/run` y `/dev/pts`.
2. Monta `configfs` y crea el gadget `g1` con ACM + RNDIS, enlaza el primer
   UDC, configura `10.55.0.2/24` en la interfaz `usb*` y lanza `getty` en
   `ttyGS0`.
3. Carga cada línea de `/lib/modules/modules.load` con `modprobe` (se ignoran
   vacías y comentarios `#`).
4. Busca `PARTNAME=userdata` en `/sys/class/block/*/uevent` (no por PARTUUID),
   la monta en `/mnt/root`, escribe `dmesg` en
   `/mnt/root/var/log/dmesg-<fecha>.log`, copia lo mismo a pstore si existe
   `/sys/fs/pstore`, ejecuta `resize2fs` (si está en el PATH; busybox 1.36 no
   lo trae y entonces avisa y sigue) y hace `switch_root` a `/sbin/init`.
5. Si algo falla se queda en shell de rescate en `ttyGS0` y en la consola.

El arranque real se pide con `rdinit=/init`.

## Construir

```sh
scripts/build-initramfs.sh /ruta/busybox-aarch64
```

Sin argumento usa `third_party/busybox-aarch64/busybox`. Debe ser un ELF
arm64 **estático**; el script lo comprueba. El cpio se genera con fechas
fijas (`@0`), orden `LC_ALL=C`, dueño 0:0 y `gzip -9n`.

### busybox

busybox.net no publica binarios aarch64 (solo x86_64 y armv8l de 32 bits), así
que se compila:

```sh
curl -LO https://busybox.net/downloads/busybox-1.36.1.tar.bz2
tar xf busybox-1.36.1.tar.bz2 && cd busybox-1.36.1
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
sed -i 's/^CONFIG_TC=y/# CONFIG_TC is not set/' .config   # headers 6.8 rompen tc.c
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- oldconfig </dev/null
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
```

`TC` se desactiva porque `networking/tc.c` no compila con `linux-libc-dev` 6.8
(faltan `struct tc_cbq_*`). Si el host no tiene `libc6-dev`, se pueden extraer
los `.deb` a un sysroot y pasar un `HOSTCC` que use `-isystem`/`-B` sobre él,
sin tocar la libc del sistema.

## Verificar

```sh
sh -n initramfs/init
scripts/build-initramfs.sh
gzip -dc out/initramfs.cpio.gz | cpio -it
qemu-aarch64-static -L out/initramfs-root out/initramfs-root/bin/busybox true
```

`cpio -it < out/initramfs.cpio.gz` solo funciona si el cpio del host
descomprime gzip (el de Ubuntu no lo hace sin `-z`); por eso el listado pasa
por `gzip -dc`.

Para probar el init de verdad hace falta un kernel; con `qemu-aarch64-static`
solo se comprueba que busybox arranca. Sin UDC ni particiones el init cae en
la shell de rescate, que es el comportamiento esperado.
