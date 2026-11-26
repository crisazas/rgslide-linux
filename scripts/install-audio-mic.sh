#!/bin/sh
# Prepara la captura del microfono en la Slide (DSP ya vivo tras rgslide-audio-prep): firmware dsp_vbc y audio_structure,
# herramientas ctl.py/cap y el script de mixer rgslide-audio-mic. Requiere el ODM de Android montado (ver install-dsp-vbc.sh).
set -eu
D=$(dirname "$0")/..
H=${RGSLIDE_HOST:-console@192.168.68.116}
O="-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh"
P=${RGSLIDE_ODM:-/tmp/opencode/odm_m}/etc/audio_params/sprd
python3 "$D/tools/vbcprofile/xml2bin.py" $P/dsp_vbc.xml /tmp/dsp_vbc.out
python3 "$D/tools/vbcprofile/xml2bin.py" $P/audio_structure.xml /tmp/audio_structure.out
aarch64-linux-gnu-gcc -O2 -Wall -o /tmp/cap.out "$D/tools/pcmprobe/cap.c" -lm
aarch64-linux-gnu-gcc -O2 -Wall -o /tmp/play.out "$D/tools/pcmprobe/play.c" -lm
scp -q $O /tmp/dsp_vbc.out /tmp/audio_structure.out /tmp/cap.out "$D/tools/pcmprobe/ctl.py" /tmp/play.out "$D/rootfs/overlay/usr/local/sbin/rgslide-audio-mic" "$D/rootfs/overlay/usr/local/sbin/rgslide-audio-spk" "$H:/tmp/"
ssh $O "$H" 'sudo install -m 644 /tmp/dsp_vbc.out /lib/firmware/dsp_vbc && sudo install -m 644 /tmp/audio_structure.out /lib/firmware/audio_structure &&
 sudo install -d /usr/local/lib/rgslide && sudo install -m 755 /tmp/ctl.py /usr/local/lib/rgslide/ctl.py && sudo install -m 755 /tmp/cap.out /usr/local/bin/rgslide-cap &&
 sudo install -m 755 /tmp/play.out /usr/local/bin/rgslide-play && sudo install -m 755 /tmp/rgslide-audio-mic /usr/local/sbin/rgslide-audio-mic && sudo install -m 755 /tmp/rgslide-audio-spk /usr/local/sbin/rgslide-audio-spk && ls -l /lib/firmware/dsp_vbc /lib/firmware/audio_structure /usr/local/bin/rgslide-cap'
