#!/bin/sh
#
# muzyka-pc.sh - buduje remom-music.exe (Windows, mingw) i remom-music (Linux):
# konwerter muzyki z native/remom/muzyka_konw.c + konwerter XMIDI ReMoM.
# Uzycie (w WSL):  sh build/muzyka-pc.sh   -> $HOME/build-remom/pc/
#
set -e
ls /mnt/i/GITHUB >/dev/null 2>&1 || sudo -n mount -t drvfs I: /mnt/i 2>/dev/null || true
REPO=${REPO:-$(cd "$(dirname "$0")/.." && pwd)}
WORK=$HOME/build-remom/pc
TARBALL=$REPO/upstream/ReMoM-a9cc082.tar.gz
[ -f "$TARBALL" ] || sh "$REPO/build/pobierz-upstream.sh" "$TARBALL"
rm -rf "$WORK"; mkdir -p "$WORK/src"
tar xzf "$TARBALL" -C "$WORK/src"
python3 "$REPO/build/xmi2mid-gen.py" "$WORK/src/ReMoM" "$WORK/xmid.c" biblioteka >/dev/null
Z="$WORK/xmid.c $REPO/native/remom/muzyka_konw.c $REPO/native/remom/muzyka-pc.c"
I="-I$WORK/src/ReMoM/platform/sdl2 -I$REPO/native/remom"
x86_64-w64-mingw32-gcc -O2 -w -static $I -o "$WORK/remom-music.exe" $Z
gcc -O2 -w $I -o "$WORK/remom-music" $Z
ls -la "$WORK"/remom-music*
