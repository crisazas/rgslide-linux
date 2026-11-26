#!/bin/sh
# Descarga el tema "carbon" (RetroPie/es-theme-carbon, de Rookervik sobre "simple" de Nils Bonenberger; el repo NO declara licencia, por eso no se versiona aqui),
# instala solo los sistemas que usa la Slide en /etc/emulationstation/themes/carbon, crea las carpetas de roms que faltan y fija ThemeSet=carbon. No reinicia ES.
# Uso: scripts/install-es-theme.sh [host]
set -eu
H=${1:-console@10.55.0.2}
SSH="ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/opencode/kh -o ConnectTimeout=8"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
git clone -q --depth 1 https://github.com/RetroPie/es-theme-carbon.git "$T/src"
mkdir -p "$T/pkg/carbon"
(cd "$T/src" && cp -r carbon.xml theme.xml readme.txt art gb gbc gba nes snes nds virtualboy wonderswan "$T/pkg/carbon/")
tar -C "$T/pkg" -cf - carbon | $SSH "$H" 'sudo mkdir -p /etc/emulationstation/themes && sudo tar -C /etc/emulationstation/themes --no-same-owner -xf - && sudo chmod -R a+rX /etc/emulationstation/themes'
$SSH "$H" 'for d in gb gbc gba nes snes nds virtualboy wonderswan; do mkdir -p ~/roms/$d; done; sed -i "s#<string name=\"ThemeSet\" value=\"[^\"]*\" />#<string name=\"ThemeSet\" value=\"carbon\" />#" ~/.emulationstation/es_settings.cfg; grep -a ThemeSet ~/.emulationstation/es_settings.cfg'
