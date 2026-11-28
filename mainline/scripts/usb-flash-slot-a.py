#!/usr/bin/env python3
"""Flash a mainline boot image to Slot A (boot_a) ONLY over USB from Slot B
rescue shell (10.55.0.2:23), set misc to try Slot A ONCE with automatic
fallback to Slot B, verify MD5 readback, and trigger a warm reboot.

Never touches boot_b, vendor_boot_a/b, or userdata.
"""
import hashlib
import http.server
import os
import socket
import struct
import sys
import threading
import time
import zlib

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT_DIR = os.path.join(ROOT_DIR, 'out', 'mainline')
BOOT_IMG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(OUT_DIR, 'boot-mainline-m0.img')
MISC_BASE = os.path.join(ROOT_DIR, 'dump', 'min', 'misc.img')
MISC_OUT = os.path.join(OUT_DIR, 'misc-a-once.img')

HOST_IP = os.environ.get('RGSLIDE_HOST_IP', '10.55.0.1')
DEV_IP = os.environ.get('RGSLIDE_DEV_IP', '10.55.0.2')
HTTP_PORT = int(os.environ.get('RGSLIDE_HTTP_PORT', '18080'))

for path in (BOOT_IMG, MISC_BASE):
    if not os.path.isfile(path):
        sys.exit(f'Error: missing file: {path}')

# Generate misc-a-once.img: Slot A prio=15 tries=2 ok=0; Slot B prio=14 tries=2 ok=1
# Note: Unisoc LK checks rollback when tries<=1 with successful_boot=0, so tries=2 gives 1 real boot attempt.
m = bytearray(open(MISC_BASE, 'rb').read())
o = 0x800
m[o:o + 4] = b'_a\0\0'
m[o + 12] = (15 & 15) | (2 << 4)
m[o + 13] = 0
m[o + 14] = (14 & 15) | (2 << 4) | (1 << 7)
m[o + 15] = 0
struct.pack_into('<I', m, o + 0x1c, zlib.crc32(bytes(m[o:o + 0x1c])) & 0xffffffff)
open(MISC_OUT, 'wb').write(m)

boot_sz = os.path.getsize(BOOT_IMG)
misc_sz = os.path.getsize(MISC_OUT)
boot_md5 = hashlib.md5(open(BOOT_IMG, 'rb').read()).hexdigest()
misc_md5 = hashlib.md5(open(MISC_OUT, 'rb').read()).hexdigest()


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=OUT_DIR, **kwargs)

    def log_message(self, fmt, *args):
        pass


def tel_cmd(cmd, timeout=30.0):
    s = socket.create_connection((DEV_IP, 23), timeout=10)
    time.sleep(0.3)
    s.sendall((cmd + '; echo END""_MARKER; exit\n').encode())
    s.settimeout(timeout)
    out = b''
    while b'END_MARKER' not in out:
        chunk = s.recv(8192)
        if not chunk:
            break
        out += chunk
    s.close()
    return out.decode('latin1', errors='replace')


# Safety check: verify partition names and that we are running from Slot B
pre = tel_cmd(
    'tr " " "\\n" < /proc/cmdline | grep slot_suffix; '
    'grep PARTNAME /sys/class/block/sda3/uevent /sys/class/block/sda36/uevent /sys/class/block/sda37/uevent'
)
assert 'slot_suffix=_b' in pre, f'Refusing to flash: device is not running Slot B:\n{pre}'
assert 'sda3/uevent:PARTNAME=misc' in pre, f'Unexpected sda3:\n{pre}'
assert 'sda36/uevent:PARTNAME=boot_a' in pre, f'Unexpected sda36:\n{pre}'
assert 'sda37/uevent:PARTNAME=boot_b' in pre, f'Unexpected sda37:\n{pre}'

srv = http.server.ThreadingHTTPServer((HOST_IP, HTTP_PORT), QuietHandler)
threading.Thread(target=srv.serve_forever, daemon=True).start()

boot_name = os.path.basename(BOOT_IMG)
misc_name = os.path.basename(MISC_OUT)
try:
    res = tel_cmd(
        f'wget -q -O /tmp/boot.img http://{HOST_IP}:{HTTP_PORT}/{boot_name} && '
        f'wget -q -O /tmp/misc.img http://{HOST_IP}:{HTTP_PORT}/{misc_name} && '
        f'dd if=/tmp/boot.img of=/dev/sda36 bs=1048576 conv=fsync && '
        f'dd if=/tmp/misc.img of=/dev/sda3 bs=1048576 conv=fsync && '
        f'rm -f /tmp/boot.img /tmp/misc.img && '
        f'head -c {boot_sz} /dev/sda36 | md5sum && '
        f'head -c {misc_sz} /dev/sda3 | md5sum'
    )
finally:
    srv.shutdown()

print(res)
assert boot_md5 in res, f'boot_a MD5 mismatch! expected {boot_md5}'
assert misc_md5 in res, f'misc MD5 mismatch! expected {misc_md5}'

print('[mainline] boot_a and misc verified; triggering warm reboot into Slot A...')
s = socket.create_connection((DEV_IP, 23), timeout=5)
time.sleep(0.2)
s.sendall(b'sync; reboot -f\n')
time.sleep(0.5)
s.close()
