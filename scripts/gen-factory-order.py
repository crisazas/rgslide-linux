#!/usr/bin/env python3
# Orden de carga de la cadena de fabrica resolviendo dependencias (insmod no las resuelve; Android usa modprobe).
# Lee modules.load/modules.dep de la ramdisk de vendor y escribe initramfs/modules/rgslide-factory.load:
# cada modulo va despues de TODAS sus dependencias, y por lo demas se respeta el orden de fabrica.
import os, sys
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VR = R + '/dump/boot/rd_vendor/lib/modules'
dep = {}
for l in open(VR + '/modules.dep'):
    if ':' in l:
        k, v = l.strip().split(':', 1)
        dep[os.path.basename(k)] = [os.path.basename(x) for x in v.split()]
load = [l.strip() for l in open(VR + '/modules.load') if l.strip()]
out, seen = [], set()
def visit(m):
    if m in seen:
        return
    seen.add(m)
    for d in dep.get(m, []):
        visit(d)
    out.append(m)
for m in load:
    visit(m)
extra = [m for m in out if m not in load]
open(R + '/initramfs/modules/rgslide-factory.load', 'w').write('\n'.join(out) + '\n')
moved = [m for m in load if out.index(m) != load.index(m)]
print('%d modulos (%d extra por dependencias: %s); %d cambian de posicion' % (len(out), len(extra), ' '.join(extra) or '-', len(moved)))
# comprobacion: ningun modulo antes de sus deps
pos = {m: i for i, m in enumerate(out)}
bad = [(m, d) for m in out for d in dep.get(m, []) if d in pos and pos[d] > pos[m]]
sys.exit('ERROR de orden: %s' % bad[:3] if bad else 0)
