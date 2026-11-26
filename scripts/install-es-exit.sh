#!/bin/sh
# Salida de EmulationStation (A): despliega rgslide-es con el modo de salida y su menu de rescate. Uso: scripts/install-es-exit.sh [install|undo]
# install (por defecto) deja EXIT_MODE=boot-menu en /etc/default/rgslide-es; undo quita el menu y deja el comportamiento anterior.
# No reinicia el servicio: los cambios de /usr/local/bin/rgslide-es se aplican al siguiente reinicio de ES (systemctl restart rgslide-es).
set -eu
D=$(dirname "$0")/..
H=${RGSLIDE_HOST:-console@10.55.0.2}
O="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh"
case "${1:-install}" in
install)
	sshpass -p 220800Sa scp -q $O "$D/rootfs/overlay/usr/local/bin/rgslide-es" "$D/rootfs/overlay/usr/local/bin/rgslide-bootmenu" \
		"$D/rootfs/overlay/etc/systemd/system/rgslide-bootmenu.service" "$D/rootfs/overlay/etc/default/rgslide-es" "$H:/tmp/"
	sshpass -p 220800Sa ssh $O "$H" 'sudo install -m 755 /tmp/rgslide-es /tmp/rgslide-bootmenu /usr/local/bin/ &&
	 sudo install -m 644 /tmp/rgslide-bootmenu.service /etc/systemd/system/ && sudo install -m 644 /tmp/rgslide-es /etc/default/rgslide-es &&
	 sudo systemctl daemon-reload && rm -f /tmp/rgslide-es /tmp/rgslide-bootmenu /tmp/rgslide-bootmenu.service /tmp/rgslide-es'
	;;
undo)
	sshpass -p 220800Sa ssh $O "$H" 'sudo systemctl stop rgslide-bootmenu 2>/dev/null || true;
	 sudo rm -f /etc/systemd/system/rgslide-bootmenu.service /etc/default/rgslide-es /usr/local/bin/rgslide-bootmenu && sudo systemctl daemon-reload' || true
	# el rgslide-es anterior (sin EXIT_MODE) esta en el repo: se despliega a mano si hace falta.
	;;
*) echo "uso: $0 [install|undo]"; exit 2 ;;
esac
echo "hecho (${1:-install})"
