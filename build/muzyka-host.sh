#!/bin/sh
#
# muzyka-host.sh - muzyka dla Amigi z LBX gracza, renderowana w WSL.
# Szczegoly: build/muzyka-render.py. Wynik: Work:muzyka/<fnv>.wav
# (C:\temp\amiga_mom\work\muzyka). Pliki istniejace sa pomijane.
#
# Uzycie (w WSL):  sh /mnt/i/GITHUB/Ami_ReMoM/build/muzyka-host.sh
#
set -e
ls /mnt/i/GITHUB >/dev/null 2>&1 || sudo -n mount -t drvfs I: /mnt/i 2>/dev/null || true

REPO=${REPO:-$(cd "$(dirname "$0")/.." && pwd)}
WORK=$HOME/build-remom-host
SRC=$WORK/src
DANE=${DANE:-/mnt/c/temp/amiga_mom/work}
SF=${SF:-/usr/share/sounds/sf2/TimGM6mb.sf2}
[ -d "$SRC" ] || { echo "najpierw build-host.sh"; exit 1; }

mkdir -p "$WORK/tools"
python3 "$REPO/build/xmi2mid-gen.py" "$SRC" "$WORK/tools/xmi2mid.c"
gcc -O2 -w -I"$SRC/platform/sdl2" -o "$WORK/tools/xmi2mid" "$WORK/tools/xmi2mid.c"

python3 "$REPO/build/muzyka-render.py" "$WORK/tools/xmi2mid" "$SF" "$DANE/muzyka" "$DANE"/*.LBX
ls -la "$DANE/muzyka" | tail -3
du -sh "$DANE/muzyka"
