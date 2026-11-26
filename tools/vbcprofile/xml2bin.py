#!/usr/bin/env python3
"""Genera el firmware binario `dsp_vbc` (o audio_structure...) que pide snd-soc-sprd-vbc-v4 a partir del XML de Android (odm/etc/audio_params/sprd).
Formato (medido en vbc_profile_loading/vbc_profile_put del .ko): 16 B 'audio_profile'+NUL, u32 num_mode, u32 struct_size, y num_mode*struct_size bytes de datos.
Cada campo del XML lleva `id` = desplazamiento absoluto en los datos; los u32 con hijos son campos de bits (offset, bits, val)."""
import struct, sys, xml.etree.ElementTree as ET

SIZES = {"u32": 4, "u16": 2, "u8": 1}
def size_of(t):
    if t in SIZES: return SIZES[t]
    if t.startswith("t"): return int(t[1:]) // 8
    if t.startswith("u"): return int(t[1:]) // 8
    raise ValueError(t)

def main(src, dst):
    r = ET.parse(src).getroot()
    num_mode = int(r.get("num_mode"), 16); struct_size = int(r.get("struct_size"), 16)
    data = bytearray(num_mode * struct_size)
    covered = 0; modes = 0
    for m in r.iter():
        if m.get("mode") is None: continue
        modes += 1
        for c in m:
            off = int(c.get("id"), 16); t = c.get("t"); n = size_of(t)
            if off + n > len(data): raise SystemExit("campo fuera de rango: %s %s" % (c.tag, c.get("id")))
            kids = list(c)
            if kids:
                v = 0
                for k in kids:
                    o = int(k.get("offset")); b = int(k.get("bits")); val = int(k.get("val"), 16)
                    v |= (val & ((1 << b) - 1)) << o
                data[off:off + n] = (v & ((1 << (8 * n)) - 1)).to_bytes(n, "little")
            else:
                val = int(c.get("val", "0x0"), 16) & ((1 << (8 * n)) - 1)
                if val: data[off:off + n] = val.to_bytes(n, "little")
            covered += n
    if modes != num_mode: raise SystemExit("modos %d != num_mode %d" % (modes, num_mode))
    if covered != len(data): raise SystemExit("cobertura %d != %d" % (covered, len(data)))
    with open(dst, "wb") as f:
        f.write(b"audio_profile".ljust(16, b"\0") + struct.pack("<II", num_mode, struct_size) + bytes(data))
    print("%s: %d modos x %d B = %d B de datos (+24 de cabecera)" % (dst, num_mode, struct_size, len(data)))

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
