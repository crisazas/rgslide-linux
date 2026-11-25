#!/usr/bin/env python3
"""Consola de la shell de rescate de la Slide por gser (/dev/rgslide-console o /dev/ttyUSB*).

Espera a que aparezca el puerto, manda los comandos de un fichero (uno por linea) o de la linea de ordenes,
y guarda todo lo recibido en out/console-<hora>.log. Sin pyserial: termios en modo raw.

  scripts/slide-console.py [--wait 420] [--cmds fichero] ["cmd1" "cmd2" ...]
"""
import argparse, glob, os, select, sys, termios, time, tty

MARK = "__RGS_FIN__"


def find_port():
    for p in ["/dev/rgslide-console"] + sorted(glob.glob("/dev/ttyUSB*")):
        if os.path.exists(p):
            return p
    return None


def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def read_until(fd, token, timeout, log):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r:
            continue
        try:
            chunk = os.read(fd, 4096)
        except (BlockingIOError, OSError):
            return buf, False
        if not chunk:
            continue
        buf += chunk
        log.write(chunk)
        log.flush()
        if token.encode() in buf:
            return buf, True
    return buf, False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wait", type=int, default=420)
    ap.add_argument("--cmds")
    ap.add_argument("--timeout", type=int, default=20, help="segundos por comando")
    ap.add_argument("cmd", nargs="*")
    a = ap.parse_args()
    cmds = list(a.cmd)
    if a.cmds:
        cmds += [l.rstrip("\n") for l in open(a.cmds) if l.strip() and not l.startswith("#")]
    os.makedirs("out", exist_ok=True)
    logp = time.strftime("out/console-%H%M%S.log")
    print(f"log: {logp}", flush=True)
    end = time.time() + a.wait
    port = None
    while time.time() < end and not (port := find_port()):
        time.sleep(0.5)
    if not port:
        print(f"no aparecio el puerto en {a.wait}s")
        return 1
    print(f"{time.strftime('%T')} puerto {port}", flush=True)
    time.sleep(1.0)
    with open(logp, "ab") as log:
        fd = open_port(port)
        # despertar la shell: el getty del init lanza /bin/sh directamente
        os.write(fd, b"\n")
        read_until(fd, "#", 3, log)
        ok = 0
        for c in cmds:
            log.write(f"\n### {time.strftime('%T')} $ {c}\n".encode())
            # la marca se arma en la shell (printf %s%s) para que el eco del comando no la contenga
            os.write(fd, f"{c}; printf '%s%s:%s:\\n' __RGS_ FIN__ $?\n".encode())
            out, got = read_until(fd, MARK + ":", a.timeout, log)
            txt = out.decode("utf-8", "replace")
            rc = txt.split(MARK + ":", 1)[1].split(":", 1)[0] if got else "?"
            print(f"{time.strftime('%T')} [{'ok' if got else 'SIN FIN'} rc={rc}] {c}  ({len(out)} B)", flush=True)
            ok += got
            if not got and not os.path.exists(port):
                print("el puerto desaparecio")
                break
        os.close(fd)
    print(f"{ok}/{len(cmds)} comandos con respuesta completa; log {logp}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
