#!/bin/sh
# Empaqueta out/boot-rescue.img: kernel de serie (dump/boot/boot_a.unp/kernel) + out/initramfs.cpio.gz, header v4 como el de serie.
# Solo para `fastboot boot` (RAM); no flashear sin haber probado.
set -eu
cd "$(dirname "$0")/.."
python3 tools/mkbootimg.py --header_version 4 --kernel dump/boot/boot_a.unp/kernel --ramdisk out/initramfs.cpio.gz \
  --cmdline "${CMDLINE:-enforcing=0 audit=0 androidboot.selinux=permissive}" --os_version 13.0.0 --os_patch_level 2023-08 -o out/boot-rescue.img
ls -l out/boot-rescue.img
