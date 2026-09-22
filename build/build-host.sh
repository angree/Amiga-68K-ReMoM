#!/bin/sh
#
# build-host.sh - HeMoM (ReMoM bez ekranu) zbudowany NATYWNIE w WSL (x86-64).
#
# To jest wyrocznia dla portu: te same zrodla, te same dane, te same
# argumenty - a wynik (zapis gry, zrzut tekstowy, log) porownuje sie z tym,
# co wyprodukowala Amiga. Dwa porownania izoluja dwie rozne rzeczy:
#   host  <-> Amiga  : maszyna i nasze latki (kolejnosc bajtow, toolchain)
#   host  <-> PC-ReMoM upstreamu : nic - to ten sam kod
#
# Buduje z NIEZALATANEGO upstreamu: latki z remom-patch.py sa amigowe
# i na hoscie nie sa potrzebne. HeMoM nie zalezy od SDL, wiec nic poza gcc.
#
# Uzycie (w WSL):  sh /mnt/i/GITHUB/Ami_ReMoM/build/build-host.sh
# Wynik:           $HOME/build-remom-host/hemom
#
set -e
ls /mnt/i/GITHUB >/dev/null 2>&1 || sudo -n mount -t drvfs I: /mnt/i 2>/dev/null || true

REPO=${REPO:-$(cd "$(dirname "$0")/.." && pwd)}
WORK=$HOME/build-remom-host
UPSTREAM_REV=a9cc082
TARBALL=$REPO/upstream/ReMoM-$UPSTREAM_REV.tar.gz
[ -f "$TARBALL" ] || sh "$REPO/build/pobierz-upstream.sh" "$TARBALL"
JOBS=$(nproc 2>/dev/null || echo 4)

mkdir -p "$WORK"
rm -rf "$WORK/stage"
mkdir -p "$WORK/stage"
tar xzf "$TARBALL" -C "$WORK/stage"
rsync -rc --delete "$WORK/stage/ReMoM/" "$WORK/src/"
SRC=$WORK/src
# WYROCZNIA OBRAZU: pusty Platform_Video_Update() z headless zapisuje klatki
# (native/remom/host/host_klatki.c, zmienna HOST_KLATKI=<katalog>). Tylko host.
H=$SRC/platform/headless/headless_PFL.c
if ! grep -q Host_Klatka "$H"; then
	sed -i 's|    /\* No display .*framebuffer exists in memory but is never presented. \*/|    { extern void Host_Klatka(void); Host_Klatka(); }  /* HOST: wyrocznia obrazu */|' "$H"
	grep -q Host_Klatka "$H" || { echo "LATKA NIE PASUJE: headless_PFL.c Platform_Video_Update"; exit 1; }
fi
mkdir -p "$SRC/host"
cp "$REPO/native/remom/host/host_klatki.c" "$SRC/host/host_klatki.c"
mkdir -p "$WORK/gen" "$WORK/obj"
sed "s/@REMOM_VERSION@/0.0.0-host-$UPSTREAM_REV/" "$SRC/cmake/remom_version.h.in" > "$WORK/gen/remom_version.h"

cmake_block() {
	tr -d '\r' < "$1" | sed -n "/$2/,/)/p" | grep -oE '[^ ]+\.c' | while read -r f; do
		case "$f" in
			'${PROJECT_SOURCE_DIR}/'*) echo "${f#\$\{PROJECT_SOURCE_DIR\}/}" ;;
			*) echo "$3$f" ;;
		esac
	done
}
{
	cmake_block "$SRC/STU/src/CMakeLists.txt" "set(LIBSTU_SOURCES" "STU/src/"
	cmake_block "$SRC/MoX/src/CMakeLists.txt" "set(LIBMOX_SOURCES" "MoX/src/"
	cmake_block "$SRC/MoM/src/CMakeLists.txt" "set(LIBMOM_SOURCES" "MoM/src/"
	cmake_block "$SRC/platform/CMakeLists.txt" "add_library(Platform_Headless" "platform/"
	tr -d '\r' < "$SRC/src/CMakeLists.txt" | sed -n 's/^add_executable(HeMoM \(.*\))/\1/p' | tr ' ' '\n' | grep '\.c$' | sed 's#^#src/#'
	echo host/host_klatki.c
} > "$WORK/sources.list"

# flagi jak w upstreamie dla GCC (Release): bez STU_DEBUG
FLAGS="-O2 -std=gnu99 -w -DNO_SOUND_LIBRARY -I$SRC/platform/include -I$SRC/src -I$WORK/gen"
cd "$SRC"
cat "$WORK/sources.list" | xargs -P "$JOBS" -I{} sh -c 'o='"$WORK"'/obj/$(echo {} | tr / _).o; [ -f "$o" ] && [ "$o" -nt {} ] || gcc '"$FLAGS"' -c {} -o "$o"'
OBJS=$(while read -r f; do printf '%s ' "$WORK/obj/$(echo "$f" | tr / _).o"; done < "$WORK/sources.list")
gcc -o "$WORK/hemom" $OBJS -lm
ls -la "$WORK/hemom"
echo "== gotowe (host)"
