#!/usr/bin/env python3
"""Supervisor de juego para rgslide-es-launch: uso  hotkey-launch.py NUCLEO.so ROM   (o  hotkey-launch.py -- CMD ARGS...)

Lanza RetroArch (o el emulador dado) y vigila /run/rgslide/settings-request (lo crea rgslide-hotkeyd).
Con peticion: RetroArch guarda estado en el slot 99 por su comando stdin, se cierra (SIGTERM), se abre el panel
(rgslide-ui) con la pantalla libre y se relanza el mismo nucleo+ROM con --entryslot=99. Fallo en cualquier paso: el
juego sigue (si no se pudo guardar) o el wrapper termina y ES recupera la pantalla. Ver docs/agent-hotkey.md.
"""
import glob, os, shlex, signal, subprocess, sys, time

RUN = os.environ.get("RGSLIDE_RUN", "/run/rgslide")
RA = os.environ.get("RGSLIDE_RA_BIN", "/usr/local/bin/rgslide-retroarch")
PANEL = os.environ.get("RGSLIDE_PANEL_CMD", "/usr/local/bin/rgslide-ui /usr/share/rgslide/ui/panel.menu")
STATES = os.environ.get("RGSLIDE_STATES", "/opt/retroarch/states")
RUMBLE = os.environ.get("RGSLIDE_RUMBLE", "/usr/local/sbin/rgslide-rumble")
SLOT = 99
SAVE_TIMEOUT = float(os.environ.get("RGSLIDE_SAVE_TIMEOUT", "8"))
QUIT_TIMEOUT = float(os.environ.get("RGSLIDE_QUIT_TIMEOUT", "8"))
CONFIRM_S = float(os.environ.get("RGSLIDE_CONFIRM_S", "4"))
KILLREQ = os.path.join(RUN, "kill-game")
REQ = os.path.join(RUN, "settings-request")
SESSION = os.path.join(RUN, "session")
CONFS = "/etc/retroarch-es.cfg|/etc/retroarch-hotkey.cfg"


def log(*a):
    print("hotkey-launch:", *a, file=sys.stderr, flush=True)


def set_session(v):
    try:
        os.makedirs(RUN, exist_ok=True)
        with open(SESSION, "w") as f:
            f.write(v + "\n")
    except OSError:
        pass


def take_request():
    try:
        os.unlink(REQ)
        return True
    except OSError:
        return False


def buzz(n=1):
    if os.path.exists(RUMBLE):
        try:
            subprocess.Popen([RUMBLE, "-d", "120", "-n", str(n), "-p", "120"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL)
        except OSError:
            pass


class Game:
    """Un RetroArch con su tuberia de comandos (stdin no bloqueante) y su salida de respuestas en un fichero."""

    def __init__(self, core, rom, entryslot=None):
        self.core, self.rom = core, rom
        r, w = os.pipe()
        os.set_blocking(r, False)  # la build no tiene red: sin esto read(stdin) bloquearia a RetroArch
        self.w = w
        self.outpath = os.path.join(RUN, "ra-out.%d" % os.getpid())
        self.out = open(self.outpath, "w+")
        cmd = [RA, "--appendconfig=" + CONFS]
        if entryslot is not None:
            cmd.append("--entryslot=%d" % entryslot)
        cmd += ["-L", core, rom]
        self.p = subprocess.Popen(cmd, stdin=r, stdout=self.out, close_fds=True)
        os.close(r)

    def send(self, line):
        try:
            os.write(self.w, (line + "\n").encode())
            return True
        except OSError:
            return False

    def replies(self):
        self.out.seek(0)
        return self.out.read()

    def alive(self):
        return self.p.poll() is None

    def stop(self, timeout):
        if self.alive():
            self.p.terminate()
            try:
                self.p.wait(timeout)
            except subprocess.TimeoutExpired:
                log("RetroArch no sale con SIGTERM, SIGKILL")
                self.p.kill()
                self.p.wait()
        return self.p.returncode

    def close(self):
        for f in (lambda: os.close(self.w), self.out.close, lambda: os.unlink(self.outpath)):
            try:
                f()
            except OSError:
                pass


def state_files(rom):
    base = os.path.splitext(os.path.basename(rom))[0]
    pats = [os.path.join(STATES, "*", "%s.state%d" % (base, SLOT)), os.path.join(STATES, "%s.state%d" % (base, SLOT)),
            os.path.join(os.path.dirname(rom), "%s.state%d" % (base, SLOT))]
    out = []
    for p in pats:
        out += glob.glob(p) + glob.glob(p + ".entry")
    return out


def drop_state(rom):
    for f in state_files(rom):
        try:
            os.unlink(f)
        except OSError:
            pass


def save_state(g):
    """True solo si RetroArch contesta OK y el .state99 es nuevo (mtime posterior a la orden)."""
    t0 = time.time() - 1
    g.send("SHOW_MSG Guardando y abriendo Ajustes...")
    time.sleep(0.3)
    if not g.send("SAVE_STATE_SLOT %d" % SLOT):
        return False
    end = time.monotonic() + SAVE_TIMEOUT
    while time.monotonic() < end and g.alive():
        rep = g.replies()
        fresh = [f for f in state_files(g.rom) if os.path.getmtime(f) >= t0 and os.path.getsize(f) > 0]
        if "SAVE_STATE_SLOT ERROR" in rep:
            log("RetroArch no pudo guardar:", rep.strip().splitlines()[-1])
            return False
        if fresh and ("SAVE_STATE_SLOT %d" % SLOT) in rep:
            time.sleep(0.3)  # que termine de escribirse
            return True
        time.sleep(0.1)
    log("sin confirmacion del guardado en", SAVE_TIMEOUT, "s")
    return False


def run_panel():
    set_session("panel")
    log("panel:", PANEL)
    try:
        return subprocess.call(shlex.split(PANEL))
    except OSError as e:
        log("no pude abrir el panel:", e)
        return 127


def run_retroarch(core, rom):
    try:
        return _run_retroarch(core, rom)
    finally:
        drop_state(rom)  # el slot 99 es del panel: no se deja nada tras salir del juego


def _run_retroarch(core, rom):
    entry = None
    drop_state(rom)  # un 99 viejo de una ejecucion caida no debe cargarse
    rc = 0
    while True:
        g = Game(core, rom, entry)
        set_session("game")
        started = time.monotonic()
        try:
            while g.alive():
                if os.path.exists(REQ):
                    take_request()
                    if time.monotonic() - started < 3:  # ignora rebotes recien arrancado
                        continue
                    buzz(1)
                    if not save_state(g):
                        log("no se pudo guardar estado (juego colgado o sin respuesta): forzando parada para abrir Ajustes")
                        drop_state(rom)
                    g.stop(QUIT_TIMEOUT)
                    break
                time.sleep(0.15)
            else:
                rc = g.p.returncode
                return rc  # RetroArch salio solo (20+Start o fallo): ES retoma la pantalla
            run_panel()
            if os.path.exists(KILLREQ):
                try:
                    os.unlink(KILLREQ)
                except OSError:
                    pass
                log("cierre forzado solicitado desde el panel: saliendo a ES")
                drop_state(rom)
                return 0
            if not state_files(rom):
                log("el estado desaparecio: no se relanza el juego")
                return 0
            entry = SLOT
            take_request()
            if os.environ.get("RGSLIDE_NO_RELAUNCH"):
                return 0
        finally:
            if g.alive():
                g.stop(3)
            g.close()


def _kill_group(p, sig):
    try:
        os.killpg(p.pid, sig)
    except OSError:
        try:
            p.send_signal(sig)
        except OSError:
            pass


def _cleanup_port_helpers():
    for proc in ("gptokeyb2", "gptokeyb"):
        try:
            subprocess.run(["sudo", "-n", "pkill", "-9", "-x", proc],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=2)
        except (OSError, subprocess.TimeoutExpired):
            pass


def run_generic(cmd):
    """Emulador o port que no es RetroArch: al pulsar Select+Home cierra el juego de inmediato y abre el panel."""
    set_session("game")
    p = subprocess.Popen(cmd, start_new_session=True)
    try:
        return _generic_loop(p)
    finally:
        if p.poll() is None:
            _kill_group(p, signal.SIGKILL)
            try:
                p.wait(3)
            except subprocess.TimeoutExpired:
                pass
        _cleanup_port_helpers()


def _generic_loop(p):
    started = time.monotonic()
    while p.poll() is None:
        if os.path.exists(REQ):
            take_request()
            if time.monotonic() - started < 1.5:  # ignora rebotes recien arrancado
                continue
            buzz(1)
            _kill_group(p, signal.SIGTERM)
            try:
                p.wait(QUIT_TIMEOUT)
            except subprocess.TimeoutExpired:
                _kill_group(p, signal.SIGKILL)
                try:
                    p.wait(3)
                except subprocess.TimeoutExpired:
                    pass
            _cleanup_port_helpers()
            run_panel()
            if os.path.exists(KILLREQ):
                try:
                    os.unlink(KILLREQ)
                except OSError:
                    pass
            return 0
        time.sleep(0.15)
    return p.returncode


def main(argv):
    if len(argv) >= 2 and argv[0] == "--":
        run = lambda: run_generic(argv[1:])
    elif len(argv) == 2 and argv[0].endswith(".so"):
        run = lambda: run_retroarch(argv[0], argv[1])
    else:
        log("uso: hotkey-launch.py NUCLEO.so ROM | -- CMD...")
        return 2
    take_request()
    try:
        return run()
    except Exception as e:  # nunca dejar a ES sin recuperar
        log("error inesperado:", repr(e))
        return 1
    finally:
        set_session("menu")  # ES recupera la pantalla: el menu vuelve a sonar (rgslide-hotkeyd)


if __name__ == "__main__":
    signal.signal(signal.SIGHUP, signal.SIG_IGN)
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
    sys.exit(main(sys.argv[1:]))
