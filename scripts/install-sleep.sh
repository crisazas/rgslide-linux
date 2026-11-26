#!/bin/sh
# Despliega la suspension s2idle (boton de bloqueo + tapa) en la Slide por ssh.
# Uso: scripts/install-sleep.sh [install|undo]   (H=console@IP para cambiar el destino).
set -eu
cd "$(dirname "$0")/.."
H=${H:-console@10.55.0.2}
SSH="ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh -o ConnectTimeout=8"
O=rootfs/overlay
case "${1:-install}" in
install)
  python3 -m py_compile $O/usr/local/sbin/rgslide-sleepd
  find $O -name __pycache__ -prune -exec rm -rf {} + 2>/dev/null || true
  tar -C $O --owner=0 --group=0 -cf - usr/local/sbin/rgslide-sleep usr/local/sbin/rgslide-sleepd \
      etc/systemd/system/rgslide-sleep.service | $SSH "$H" '
    set -e
    T=$(mktemp -d); tar -C $T -xf -
    sudo cp -r $T/. / && rm -rf $T
    sudo chmod 755 /usr/local/sbin/rgslide-sleep /usr/local/sbin/rgslide-sleepd
    sudo systemctl daemon-reload && sudo systemctl enable --now rgslide-sleep.service >/dev/null 2>&1
    sleep 1; systemctl is-active rgslide-sleep; cat /sys/power/mem_sleep; journalctl -u rgslide-sleep -n 5 --no-pager' ;;
undo)
  $SSH "$H" 'sudo systemctl disable --now rgslide-sleep.service
    sudo rm -f /etc/systemd/system/rgslide-sleep.service /usr/local/sbin/rgslide-sleep /usr/local/sbin/rgslide-sleepd
    sudo systemctl daemon-reload; echo deshecho' ;;
esac
