#!/bin/sh
# Despliega el idioma es/en en la Slide por ssh (rgslide-ctl, rgslide-ui, menus, lang/*.tr, nombres traducidos de ~/roms/settings) y, con --es, el ES parcheado.
# Uso: scripts/install-lang.sh [--host console@10.55.0.2] [--set-lang auto|es|en] [--es] [--restart-es] | --undo
#   --set-lang   valor de /etc/default/rgslide-lang (por defecto auto = el idioma del sistema; la Slide tiene LANG=en_US.UTF-8 => ingles)
#   --es         ademas copia out/es-root-lang/opt/emulationstation/emulationstation (ES compilado CON patches/es/0001; ver docs/agent-lang.md)
#   --restart-es reinicia SOLO rgslide-es al final (si no, se aplica al reiniciar ES a mano o en el proximo arranque)
#   --undo       restaura rgslide-ui/rgslide-ctl/ES desde las copias .pre-lang y quita lang/ y rgslide-lang
# NO reinicia ni apaga la Slide. Sin la Slide arriba falla en el primer ssh y no toca nada.
set -eu
cd "$(dirname "$0")/.."
R=$PWD
H=console@10.55.0.2; LANGSET=auto; ES=0; RESTART=0; UNDO=0
while [ $# -gt 0 ]; do
	case "$1" in
	--host) H=$2; shift;; --set-lang) LANGSET=$2; shift;; --es) ES=1;; --restart-es) RESTART=1;; --undo) UNDO=1;;
	*) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 2;;
	esac
	shift
done
case "$LANGSET" in auto|es|en) ;; *) echo "--set-lang auto|es|en"; exit 2;; esac
SO="-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh -o ConnectTimeout=8"
SSH="ssh -n $SO $H"

$SSH 'true' || { echo "la Slide no responde por ssh: no se toca nada"; exit 1; }

if [ $UNDO = 1 ]; then
	$SSH 'for f in /usr/local/bin/rgslide-ui /usr/local/bin/rgslide-ctl /opt/emulationstation/emulationstation; do
		[ -f $f.pre-lang ] && sudo cp -p $f.pre-lang $f && sudo rm -f $f.pre-lang && echo "restaurado $f"; done
		sudo rm -rf /usr/share/rgslide/lang /etc/default/rgslide-lang; rm -f ~/roms/settings/gamelist.xml; echo "idioma desinstalado (reiniciar ES para ver los nombres originales)"'
	exit 0
fi

# 1) rgslide-ui (cruzado, mismo orden que docs/agent-control.md)
NDKB=${NDK:-$HOME/Android/Sdk/ndk/29.0.14206865}/toolchains/llvm/prebuilt/linux-x86_64/bin
SD=$R/out/sdl2-hybris-root/usr/local/sdl2-hybris
GLH=${GLES_HDRS:-$HOME/Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/inc"; cp -r "$GLH/GLES2" "$GLH/KHR" "$GLH/EGL" "$T/inc/"   # solo estas carpetas: el resto del sysroot del NDK taparia las cabeceras de glibc
$NDKB/clang --target=aarch64-linux-gnu --sysroot=$R/out/sysroot-trixie -fuse-ld=lld -O2 -I$SD/include/SDL2 -I$T/inc -o $R/out/rgslide-ui.lang \
	tools/rgslide-ui/rgslide-ui.c -L$R/out/hybris-root/usr/lib -L$SD/lib -lGLESv2 -lSDL2 -lm -Wl,--allow-shlib-undefined
echo "compilado out/rgslide-ui.lang"

# 2) copia previa en la Slide (solo la primera vez) y envio
$SSH 'for f in /usr/local/bin/rgslide-ui /usr/local/bin/rgslide-ctl /opt/emulationstation/emulationstation; do [ -f $f ] && [ ! -f $f.pre-lang ] && sudo cp -p $f $f.pre-lang; done; true'
mkdir -p "$T/pkg/usr/local/bin" "$T/pkg/usr/share/rgslide"
cp rootfs/overlay/usr/local/bin/rgslide-ctl "$T/pkg/usr/local/bin/"; cp out/rgslide-ui.lang "$T/pkg/usr/local/bin/rgslide-ui"
cp -r rootfs/overlay/usr/share/rgslide/lang rootfs/overlay/usr/share/rgslide/ui "$T/pkg/usr/share/rgslide/"
tar -C "$T/pkg" --owner=0 --group=0 -cf - . | ssh $SO $H 'sudo tar --warning=no-timestamp -C / -xf - && sudo chmod 755 /usr/local/bin/rgslide-ctl /usr/local/bin/rgslide-ui && sudo chmod -R a+rX /usr/share/rgslide/lang /usr/share/rgslide/ui'

# 3) /etc/default/rgslide-lang: no pisa un valor que ya exista salvo que se pida --set-lang explicitamente
if [ "$LANGSET" != auto ] || ! $SSH 'test -f /etc/default/rgslide-lang'; then
	$SSH "printf '# Idioma de rgslide-ctl, rgslide-ui y EmulationStation: auto (el del sistema) | es | en\nRGSLIDE_LANG=$LANGSET\n' | sudo tee /etc/default/rgslide-lang >/dev/null"
fi

# 4) nombres traducidos del sistema "Ajustes" (gamelist.xml de ~/roms/settings; ES lo lee antes que ningun otro)
$SSH 'rgslide-ctl lang apply'

# 5) ES parcheado (opcional)
if [ $ES = 1 ]; then
	[ -x out/es-root-lang/opt/emulationstation/emulationstation ] || { echo "falta out/es-root-lang/opt/emulationstation/emulationstation (compilar ES con patches/es/0001; ver docs/agent-lang.md)"; exit 1; }
	ssh $SO $H 'sudo tee /opt/emulationstation/emulationstation.new >/dev/null && sudo chmod 755 /opt/emulationstation/emulationstation.new && sudo mv /opt/emulationstation/emulationstation.new /opt/emulationstation/emulationstation' < out/es-root-lang/opt/emulationstation/emulationstation
	echo "ES parcheado copiado"
fi
[ $RESTART = 1 ] && $SSH 'sudo systemctl restart rgslide-es; sleep 6; systemctl is-active rgslide-es; journalctl -u rgslide-es -n 8 --no-pager | tail -n 8'

echo "--- comprobacion"
$SSH 'echo "idioma activo: $(rgslide-ctl lang show) (config: $(rgslide-ctl lang get))"; rgslide-ctl status time; ls /usr/share/rgslide/lang'
echo "listo. Para ver los menus de ES en el idioma nuevo hay que reiniciar ES (Start > Quit > Restart ES, o --restart-es)."
