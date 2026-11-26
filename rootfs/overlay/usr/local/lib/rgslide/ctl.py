import fcntl, os, struct, ctypes, sys
def ioc(t, nr, sz): return (3 << 30) | (sz << 16) | (ord(t) << 8) | nr
LIST = ioc('U', 0x10, 80); READ = ioc('U', 0x12, 1224)
fd = os.open('/dev/snd/controlC0', os.O_RDWR)
def lst():
    b = ctypes.create_string_buffer(80); fcntl.ioctl(fd, LIST, b, True)
    total = struct.unpack_from('IIII', b, 0)[3]
    ids = ctypes.create_string_buffer(64 * total)
    b = ctypes.create_string_buffer(80); struct.pack_into('IIIIQ', b, 0, 0, total, 0, 0, ctypes.addressof(ids))
    fcntl.ioctl(fd, LIST, b, True)
    return [(struct.unpack_from('I', ids, i*64)[0], ids.raw[i*64+16:i*64+60].split(b'\0')[0].decode('latin1')) for i in range(total)]
def rd(numid):
    b = ctypes.create_string_buffer(1224); struct.pack_into('I', b, 0, numid)
    fcntl.ioctl(fd, READ, b, True)
    return struct.unpack_from('qq', b, 72)
WRITE = ioc('U', 0x13, 1224)
def wr(numid, v):
    b = ctypes.create_string_buffer(1224); struct.pack_into('I', b, 0, numid); struct.pack_into('q', b, 72, v)
    fcntl.ioctl(fd, WRITE, b, True)
INFO = ioc('U', 0x11, 272)
def info(numid):
    b = ctypes.create_string_buffer(272); struct.pack_into('I', b, 0, numid)
    fcntl.ioctl(fd, INFO, b, True)
    t, acc, cnt = struct.unpack_from('iII', b, 64)
    return {'tipo': {1:'bool',2:'int',3:'enum',4:'bytes'}.get(t,t), 'acceso': hex(acc), 'n': cnt}
if sys.argv[1] == 'info':
    for n in sys.argv[2:]: print(n, info(int(n)))
elif sys.argv[1] == 'write':
    wr(int(sys.argv[2]), int(sys.argv[3])); print('escrito', sys.argv[2], sys.argv[3])
elif sys.argv[1] == 'list':
    for n, name in lst(): print(n, name)
elif sys.argv[1] == 'read':
    n = int(sys.argv[2]); print('numid', n, 'valor', rd(n))

ELEM_INFO = ioc('U', 0x11, 272)
def setname(name, vals):
    cand = [n for n, nm in lst() if nm == name]
    if not cand: print('NO EXISTE', name); return 1
    n = cand[0]
    b = ctypes.create_string_buffer(272); struct.pack_into('I', b, 0, n)
    fcntl.ioctl(fd, ELEM_INFO, b, True)
    t, acc, cnt = struct.unpack_from('iII', b, 64)
    v = ctypes.create_string_buffer(1224); struct.pack_into('I', v, 0, n)
    if t == 3:
        items = struct.unpack_from('I', b, 80)[0]
        names = []
        for i in range(items):
            bb = ctypes.create_string_buffer(272); struct.pack_into('I', bb, 0, n); struct.pack_into('I', bb, 84, i)
            fcntl.ioctl(fd, ELEM_INFO, bb, True)
            names.append(bb.raw[88:152].split(b'\0')[0].decode())
        for k, x in enumerate(vals):
            idx = int(x) if x.isdigit() else names.index(x)
            struct.pack_into('I', v, 72 + 4 * k, idx)
        print(name, '=', [names[struct.unpack_from('I', v, 72 + 4 * k)[0]] for k in range(len(vals))])
    else:
        for k, x in enumerate(vals):
            struct.pack_into('q', v, 72 + 8 * k, int(x))
        print(name, '=', vals)
    fcntl.ioctl(fd, WRITE, v, True)
    return 0
if sys.argv[1] == 'set':
    sys.exit(setname(sys.argv[2], sys.argv[3:]))
