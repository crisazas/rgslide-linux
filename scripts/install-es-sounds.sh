#!/bin/sh
# Sonidos de navegacion de ES (B): WAV + helper + ajuste que apaga los sonidos de ES. Uso: scripts/install-es-sounds.sh [install|undo]
# undo devuelve EnableSounds a true (ES vuelve a intentarlo con su mezclador) y quita helper y WAV.
set -eu
D=$(dirname "$0")/..
H=${RGSLIDE_HOST:-console@10.55.0.2}
O="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh"
P=220800Sa
case "${1:-install}" in
install)
	sshpass -p $P scp -q $O "$D/tools/es-sounds/nav.wav" "$D/rootfs/overlay/usr/local/bin/rgslide-es-navsound" \
		"$D/rootfs/overlay/etc/emulationstation/settings-overrides.cfg" "$H:/tmp/"
	sshpass -p $P ssh $O "$H" 'sudo install -d /usr/share/rgslide/sounds && sudo install -m 644 /tmp/nav.wav /usr/share/rgslide/sounds/nav.wav &&
	 sudo install -m 755 /tmp/rgslide-es-navsound /usr/local/bin/ && sudo install -m 644 /tmp/settings-overrides.cfg /etc/emulationstation/ &&
	 sed -i "s#<bool name=\"EnableSounds\" value=\"[^\"]*\" />#<bool name=\"EnableSounds\" value=\"false\" />#" ~/.emulationstation/es_settings.cfg;
	 rm -f /tmp/nav.wav /tmp/rgslide-es-navsound /tmp/settings-overrides.cfg'
	;;
undo)
	sshpass -p $P ssh $O "$H" 'sed -i "s#<bool name=\"EnableSounds\" value=\"[^\"]*\" />#<bool name=\"EnableSounds\" value=\"true\" />#" ~/.emulationstation/es_settings.cfg;
	 sudo rm -f /usr/local/bin/rgslide-es-navsound /etc/emulationstation/settings-overrides.cfg /usr/share/rgslide/sounds/nav.wav'
	;;
*) echo "uso: $0 [install|undo]"; exit 2 ;;
esac
echo "hecho (${1:-install}); reiniciar ES para que ES lea el ajuste"
