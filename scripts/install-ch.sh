#!/bin/sh
# Instala en la Slide la segunda fase del CM4 (CH / sensorhub): genera modem_control_ch (modem_control con 2 instrucciones parcheadas), el xml en un directorio APARTE
# (no en /vendor/etc: asi el modem_control original del arranque no lo ve) y SOLO con ENABLE=1 habilita rgslide-ch.service. No arranca nada. Uso: [ENABLE=1] scripts/install-ch.sh [host]
set -eu
cd "$(dirname "$0")/.."
H=${1:-console@10.55.0.2}
SSH="ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh -o ConnectTimeout=8"
python3 - <<'PY' | $SSH "$H" 'sudo tee /opt/android/vendor/bin/modem_control_ch.new >/dev/null && sudo chmod 755 /opt/android/vendor/bin/modem_control_ch.new && sudo mv -f /opt/android/vendor/bin/modem_control_ch.new /opt/android/vendor/bin/modem_control_ch'
import sys
d=bytearray(open('dump/android/root/vendor/bin/modem_control','rb').read())
assert d[0xcf4c:0xcf50].hex()=="68000035" and d[0xcf50:0xcf54].hex()=="e0078052"
d[0xcf4c:0xcf50]=bytes.fromhex("1f2003d5"); d[0xcf50:0xcf54]=bytes.fromhex("00048052")
sys.stdout.buffer.write(d)
PY
$SSH "$H" 'sudo mkdir -p /opt/android/vendor/etc-ch' 
cat dump/android/modem_ch_info.xml | $SSH "$H" 'sudo tee /opt/android/vendor/etc-ch/modem_ch_info.xml >/dev/null'
for f in usr/local/sbin/rgslide-ch-start usr/local/sbin/rgslide-cm4-prep etc/systemd/system/rgslide-ch.service; do
  cat rootfs/overlay/$f | $SSH "$H" "sudo tee /$f >/dev/null"
done
$SSH "$H" 'sudo chmod 755 /usr/local/sbin/rgslide-ch-start /usr/local/sbin/rgslide-cm4-prep; sudo systemctl daemon-reload; [ "${ENABLE:-0}" = 1 ] && sudo systemctl enable rgslide-ch.service; systemd-analyze verify /etc/systemd/system/rgslide-ch.service 2>&1 | head -3'
