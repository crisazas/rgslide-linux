# rootfs

Rootfs Debian 13 (trixie) arm64 mínimo para la RG Slide. Lo construye `scripts/build-rootfs.sh`
(debootstrap `--variant=minbase` + `qemu-aarch64-static` + `mkfs.ext4 -d`, el mismo mecanismo que
`linux-x400/x400-debian/build-rootfs.sh`). Sale en `out/rootfs.ext4`; `scripts/mk-sparse.sh` lo pasa
a `out/rootfs.sparse.img` con `img2simg` para `fastboot flash userdata`.

Contenido: systemd, openssh-server, iwd, wpasupplicant, bluez, usbutils, pciutils, evtest,
libinput-tools, iproute2, iputils-ping, ca-certificates, locales, sudo, vim-tiny, htop, strace,
picocom, mesa-utils, kmscube, weston y fuentes (Noto core, DejaVu). Sin KDE, Venus ni Zink.

- Usuario `console` (grupos sudo/audio/video/input/render/plugdev), sudo sin contraseña, contraseña
  vacía. Las claves SSH se leen de `rootfs/authorized_keys` si existe; si no, el aviso sale al construir.
- `sshd` y `getty@ttyGS0` habilitados; `systemd-networkd` con `10-usb0.network` = 10.55.0.2/24
  (el gadget ACM/RNDIS/NCM lo crea el initramfs).
- `/init` existe y hace `exec /sbin/init` para `rdinit=/init`.
- Las host keys de SSH se generan al construir, no en el primer arranque (allí no hay entropía).

## Tamaño mínimo

`SIZE` (por defecto `1500M`) es solo el tamaño de la imagen; el contenido real ocupa menos y el
resto se rellena de ceros. Por debajo de ~1200M el `mkfs.ext4 -d` se queda sin bloques con este
juego de paquetes. Si añades paquetes, sube `SIZE`; para bajar, mide el `du` de `out/rootfs` y deja
margen. La partición `userdata` es mucho mayor que la imagen, así que el sistema de ficheros se
expande en el primer arranque:

```sh
# en el initramfs, tras montar userdata, o una sola vez a mano:
resize2fs /dev/block/by-name/userdata
```

`resize2fs` sobre un ext4 montado en rw también vale (online resize). No hace falta `fdisk`: el
bloque de respaldo se recoloca solo. Si algún día se flashea sin sparse, `fastboot` exige que el
tamaño sea múltiplo del sector y que la partición quepa: el sparse evita ambas cosas.
