#!/usr/bin/env python3
# Lista las entradas de un .pac de Unisoc (BP_R1.0.0 / BP_R2.0.1) y extrae las que se pidan.
# Formato tomado de third_party/base618/scripts/pac-fdl.py, sin su filtro ums512.
#   pac-list.py PAC                     lista id, fichero, tamano, offset
#   pac-list.py PAC OUTDIR ID [ID...]   extrae las entradas con esos id (p. ej. FDL FDL2)
import os, struct, sys
HDR = struct.Struct("<44s I I 512s 512s I I I I I I I 200s I I I 800s I H H")
ENT = struct.Struct("<I 512s 512s 504s I I I I I I I I 5I 996s")
s = lambda b: b.decode("utf-16-le", "replace").split("\0", 1)[0]
if len(sys.argv) < 2:
    sys.exit(__doc__)
pac = sys.argv[1]
size = os.path.getsize(pac)
with open(pac, "rb") as f:
    h = HDR.unpack(f.read(HDR.size))
    if s(h[0]) not in ("BP_R1.0.0", "BP_R2.0.1"):
        sys.exit("no es un .pac de Unisoc")
    print(f"# version={s(h[0])} producto={s(h[3])} fw={s(h[4])} entradas={h[5]} tabla@{h[6]}", file=sys.stderr)
    f.seek(h[6])
    ents = []
    for _ in range(h[5]):
        e = ENT.unpack(f.read(ENT.size))
        ents.append((s(e[1]), s(e[2]), e[4] << 32 | e[6], e[5] << 32 | e[9]))
    if len(sys.argv) == 2:
        for pid, fn, n, off in ents:
            print(f"{pid:20} {fn:40} {n:>12} @{off}")
        sys.exit(0)
    out, want = sys.argv[2], set(sys.argv[3:])
    os.makedirs(out, exist_ok=True)
    for pid, fn, n, off in ents:
        if pid not in want:
            continue
        if not n or off + n > size:
            sys.exit(f"{pid}: tamano/offset no validos ({n} @{off}, pac={size})")
        f.seek(off)
        dst = os.path.join(out, fn or pid)
        with open(dst, "wb") as o:
            o.write(f.read(n))
        print(f"{pid} -> {dst} ({n} B)")
