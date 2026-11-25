#!/bin/sh
# Bucle: lanza el build de bionic con Soong; si falla por "depends on undefined module X" o por un proyecto ausente, busca X en los
# Android.bp ya clonados o en el manifiesto de android-13.0.0_r1, clona el proyecto que lo define y repite. Maximo N vueltas.
# Uso: scripts/aosp-fill-deps.sh [N]
set -u
cd "$(dirname "$0")/.."
N=${1:-15}
ROOT=third_party/aosp13
T=android-13.0.0_r1
B=https://android.googlesource.com/platform
LOG=third_party/aosp13-build.log
M=/tmp/opencode/manifest13.xml
[ -s "$M" ] || { mkdir -p /tmp/opencode; curl -sL "https://android.googlesource.com/platform/manifest/+/refs/tags/$T/default.xml?format=TEXT" | base64 -d > "$M"; }
clone() {
  path=$1
  [ -d "$ROOT/$path/.git" ] && return 1
  name=$(sed -n "s#.*path=\"$path\" name=\"\([^\"]*\)\".*#\1#p" "$M" | head -1)
  [ -n "$name" ] || { echo "no esta en el manifiesto: $path"; return 2; }
  mkdir -p "$(dirname "$ROOT/$path")"
  echo "clonando $path ($name)"
  git clone -q --depth 1 --branch "$T" "https://android.googlesource.com/$name" "$ROOT/$path" 2>&1 | tail -1
  [ -d "$ROOT/$path/.git" ]
}
i=0
while [ $i -lt "$N" ]; do
  i=$((i+1))
  echo "=== vuelta $i $(date +%T)"
  ./scripts/build-bionic-aosp.sh libc > "$LOG" 2>&1
  rc=$?
  if [ $rc -eq 0 ]; then echo "BUILD OK"; exit 0; fi
  err=$(grep -E "error" "$LOG" | sed 's/\x1b\[[0-9;]*m//g' | head -3)
  echo "$err" | cut -c1-200
  mod=$(echo "$err" | sed -n 's/.*depends on undefined module "\([^"]*\)".*/\1/p' | head -1)
  [ -n "$mod" ] || { echo "error que no es de modulo ausente; paro"; exit 2; }
  # proyecto que define el modulo: heuristica por nombre en el manifiesto
  base=$(echo "$mod" | sed 's/^lib//; s/-.*//; s/_.*//')
  cand=$(grep -o "path=\"[^\"]*\"" "$M" | sed 's/path="//; s/"//' | grep -E "(^|/)(lib)?$base($|[-_])|/$mod($|-)" | head -6)
  echo "modulo $mod; candidatos: $(echo $cand | tr '\n' ' ')"
  got=0
  for c in $cand; do clone "$c" && got=1 && break; done
  [ $got -eq 1 ] || { echo "no encuentro quien define $mod; paro"; exit 3; }
done
echo "se acabaron las vueltas"
exit 4
