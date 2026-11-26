#!/bin/sh
# Despliega el atajo de Ajustes (rgslide-hotkeyd + nuevo rgslide-es-launch) en la Slide por ssh. Guarda el lanzador actual como .pre-hotkey.
# Uso: scripts/install-hotkey.sh [install|undo]   (H=console@IP para cambiar el destino). No reinicia ES: el lanzador nuevo se usa en el proximo juego.
set -eu
cd "$(dirname "$0")/.."
H=${H:-console@192.168.68.116}
SSH="ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh -o ConnectTimeout=8"
O=rootfs/overlay
case "${1:-install}" in
install)
  python3 -m py_compile $O/usr/local/sbin/rgslide-hotkeyd $O/usr/local/lib/rgslide/hotkey-launch.py
  find $O -name __pycache__ -prune -exec rm -rf {} + 2>/dev/null || true
  tar -C $O --owner=0 --group=0 -cf - usr/local/sbin/rgslide-hotkeyd usr/local/lib/rgslide/hotkey-launch.py usr/local/bin/rgslide-es-launch \
      etc/rgslide/hotkey.conf etc/retroarch-hotkey.cfg etc/systemd/system/rgslide-hotkeyd.service | $SSH "$H" '
    set -e
    [ -e /usr/local/bin/rgslide-es-launch.pre-hotkey ] || sudo cp -p /usr/local/bin/rgslide-es-launch /usr/local/bin/rgslide-es-launch.pre-hotkey
    T=$(mktemp -d); tar -C $T -xf -
    sudo mkdir -p /usr/local/lib/rgslide /etc/rgslide
    sudo cp -r $T/. / && rm -rf $T
    sudo chmod 755 /usr/local/sbin/rgslide-hotkeyd /usr/local/lib/rgslide/hotkey-launch.py /usr/local/bin/rgslide-es-launch
    sudo systemctl daemon-reload && sudo systemctl enable rgslide-hotkeyd.service >/dev/null 2>&1 && sudo systemctl restart rgslide-hotkeyd.service
    sleep 1; systemctl is-active rgslide-hotkeyd; journalctl -u rgslide-hotkeyd -n 5 --no-pager' ;;
undo)
  $SSH "$H" 'sudo systemctl disable --now rgslide-hotkeyd.service
    [ -e /usr/local/bin/rgslide-es-launch.pre-hotkey ] && sudo cp -p /usr/local/bin/rgslide-es-launch.pre-hotkey /usr/local/bin/rgslide-es-launch
    sudo rm -f /etc/systemd/system/rgslide-hotkeyd.service /usr/local/sbin/rgslide-hotkeyd /usr/local/lib/rgslide/hotkey-launch.py /etc/retroarch-hotkey.cfg /etc/rgslide/hotkey.conf
    sudo systemctl daemon-reload; echo deshecho' ;;
esac
