#!/usr/bin/env python3
# Lee N muestras del sensorhub (CM4) por IIO. Requiere el CH arrancado (scripts/agent-ch-test.sh) y root.
# Registro de 40 bytes: [0]=0x81 [1]=handle [2]=0 | 3 x float32 LE (x,y,z) desde el byte 3 | u64 timestamp ns en el byte 27 (sin alinear).
# handle 1 = acelerometro (m/s2, medido |g|=9.6, reposo z~+9.6); handle 10 = x,y~1e-3, z~-0.168 (tipo NO identificado); handle 127: sin muestras (el script sale a los 5 s).
# Uso: agent-sensors-read.py [handle=1] [n=50]
import os, select, struct, sys, time
D = "/sys/bus/iio/devices/iio:device0"
H = "/sys/class/sprd_sensorhub/sensor_hub"
h = int(sys.argv[1]) if len(sys.argv) > 1 else 1
n = int(sys.argv[2]) if len(sys.argv) > 2 else 50
w = lambda p, v: open(p, "w").write(v)
w(D + "/trigger/current_trigger", "sprd-sensor-dev0")
for c in "xyz":
    w(f"{D}/scan_elements/in_accel_{c}_en", "1")
w(D + "/scan_elements/in_timestamp_en", "1")
w(H + "/batch", f"{h} 0 20000 0")
w(H + "/enable", f"{h} 1")
w(D + "/buffer/enable", "1")
try:
    fd = os.open("/dev/iio:device0", os.O_RDONLY)
    buf = b""
    t0 = time.time()
    got = 0
    while got < n and time.time() - t0 < 5:
        if not select.select([fd], [], [], 1)[0]:
            continue
        buf += os.read(fd, 40 * 16)
        while len(buf) >= 40 and got < n:
            r, buf = buf[:40], buf[40:]
            if r[0] != 0x81:
                buf = b""
                break
            if r[1] != h:
                continue
            x, y, z = struct.unpack_from("<3f", r, 3)
            ts = struct.unpack_from("<Q", r, 27)[0]
            print(f"{ts} h={r[1]} x={x:.4f} y={y:.4f} z={z:.4f}")
            got += 1
finally:
    w(D + "/buffer/enable", "0")
    w(H + "/enable", f"{h} 0")
