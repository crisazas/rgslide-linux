#!/bin/sh
# Genera /lib/firmware/dsp_vbc en la Slide a partir de dsp_vbc.xml del ODM de Android (audio_params/sprd).
# Uso: scripts/install-dsp-vbc.sh [dsp_vbc.xml]   (por defecto el ODM montado en /tmp/opencode/odm_m)
# El ODM sale de dump/pac/ex/super.img: python3 tools/lpunpack -p odm_a dump/pac/ex/super.img DIR; mount -o ro,loop -t erofs DIR/odm_a.img /tmp/opencode/odm_m
set -eu
X=${1:-/tmp/opencode/odm_m/etc/audio_params/sprd/dsp_vbc.xml}
H=${RGSLIDE_HOST:-console@192.168.68.116}
O="-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh"
python3 "$(dirname "$0")/../tools/vbcprofile/xml2bin.py" "$X" /tmp/dsp_vbc.out
scp -q $O /tmp/dsp_vbc.out "$H:/tmp/dsp_vbc"
ssh $O "$H" 'sudo install -m 644 /tmp/dsp_vbc /lib/firmware/dsp_vbc && ls -l /lib/firmware/dsp_vbc'
