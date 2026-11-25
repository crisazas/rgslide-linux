#!/bin/sh
# PRUEBA REVERSIBLE: arranca el CH (CM4 sensorhub) con una copia parcheada de modem_control que llama a la carga solo del CH (a290(0x20)).
# Uso: scripts/agent-ch-test.sh start|undo [host]. No toca boot/misc ni el modem_control original.
set -eu
cd "$(dirname "$0")/.."
cmd=${1:?start|undo}; H=${2:-console@10.55.0.2}
SSH="ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh -o ConnectTimeout=8"
case $cmd in
start)
  python3 - <<'PY' | $SSH "$H" 'sudo tee /opt/android/vendor/bin/modem_control_ch >/dev/null && sudo chmod 755 /opt/android/vendor/bin/modem_control_ch'
import sys
d=bytearray(open('dump/android/root/vendor/bin/modem_control','rb').read())
assert d[0xcf4c:0xcf50].hex()=="68000035" and d[0xcf50:0xcf54].hex()=="e0078052"
d[0xcf4c:0xcf50]=bytes.fromhex("1f2003d5"); d[0xcf50:0xcf54]=bytes.fromhex("00048052")
sys.stdout.buffer.write(d)
PY
  $SSH "$H" 'sudo systemctl stop rgslide-cm4; sudo systemd-run --unit=rgslide-ch-test -p Environment=LD_PRELOAD=/opt/android/libpropshim.so -p StandardOutput=file:/var/log/modem_control_ch.log -p StandardError=inherit /opt/android/vendor/bin/modem_control_ch' ;;
undo)
  $SSH "$H" 'sudo systemctl stop rgslide-ch-test; sudo rm -f /opt/android/vendor/bin/modem_control_ch /opt/android/vendor/etc/modem_ch_info.xml /vendor/etc/modem_ch_info.xml; sudo systemctl restart rgslide-cm4' ;;
esac
