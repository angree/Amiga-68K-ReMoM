#!/bin/sh
#
# Ami MoM - kompilacja ReMoM (jbalcomb/ReMoM) na AmigaOS 68k.
#
# Uruchamiac w WSL, ZAWSZE z guardem montowania PRZED skryptem:
#   wsl sh -c "ls /mnt/i/GITHUB >/dev/null 2>&1 || sudo -n mount -t drvfs I: /mnt/i; sh /mnt/i/GITHUB/Ami_ReMoM/build/build.sh"
#   ... build.sh clean     # od zera
#
# MODEL (ten sam co w porcie OpenXcom): repozytorium nie przechowuje
# zmienionej kopii upstreamu. Kazdy przebieg rozpakowuje przypiety snapshot
# upstream/ReMoM-<rewizja>.tar.gz, naklada build/remom-patch.py (mechaniczne,
# przerywa build, gdy latka nie pasuje) i dopiero wynik kompiluje. Wszystko,
# co nowe, lezy w native/.
#
# Do 2026-09-16 ten projekt pisal silnik od nowa metoda clean-room (core/,
# build/build-core.sh). Na decyzje developera clean room porzucono: kompilujemy
# ReMoM wprost, z warstwa platformy z naszych portow OpenTTD i OpenXcom.
#
# Pulapki toolchainu (kazda kosztowala realny czas, kazda wyglada jak blad gry):
#   -m68040 po cichu wybiera multilib 68881     -> -mcpu=68020 -msoft-float
#   -lpthread / -lc wciagaja newlib obok libnix -> nigdy
#   strip daje Hunk, ktory zatrzymuje maszyne   -> NIGDY nie stripowac
#   sprintf daje bzdury                         -> amiga_le.h zamienia na snprintf
#   peephole2 generuje zly kod (clr.w+clr.l)    -> -fno-peephole2
#   float*float / float/float trafia do ROM-u z zepsutym IEEESPMul/Div
#                                               -> native/fp_single.c przed -lm
#   int->double zwraca wynik w fp0 zamiast d0   -> native/fp_conv.c przed -lm
#   rownolegle buildy psuja sie nawzajem (jeden katalog obiektow)
#                                               -> jeden build naraz
#
set -e

ls /mnt/i/GITHUB >/dev/null 2>&1 || sudo -n mount -t drvfs I: /mnt/i 2>/dev/null || true

REPO=${REPO:-$(cd "$(dirname "$0")/.." && pwd)}
WORK=$HOME/build-remom
JOBS=$(nproc 2>/dev/null || echo 4)
UPSTREAM_REV=a9cc082
TARBALL=$REPO/upstream/ReMoM-$UPSTREAM_REV.tar.gz
[ -f "$TARBALL" ] || sh "$REPO/build/pobierz-upstream.sh" "$TARBALL"
DEPLOY=${DEPLOY:-/mnt/c/temp/amiga_mom/work}

export PATH=/opt/amiga/bin:/usr/local/bin:/usr/bin:/bin

log() { echo "== $*"; }

MODE="$1"
if [ "$MODE" = "clean" ]; then
	log "clean: usuwam $WORK"
	rm -rf "$WORK"
fi
mkdir -p "$WORK" "$DEPLOY"

# ------------------------------------------------------------------ zrodla --

# LUSTRO PO ZAWARTOSCI, NIGDY kompilacja wprost z /mnt/i: repozytorium lezy
# na udziale SMB, gdzie edycja pliku NIE podbija mtime widzianego przez WSL
# (zmierzone 2026-09-07). rsync -c porownuje sumy kontrolne.
log "lustro native/ i build/"
rsync -rc --delete "$REPO/native/" "$WORK/native/"
rsync -rc --delete "$REPO/build/" "$WORK/buildscripts/"

log "rozpakowanie upstreamu $UPSTREAM_REV i latki"
rm -rf "$WORK/stage"
mkdir -p "$WORK/stage"
tar xzf "$TARBALL" -C "$WORK/stage"
python3 "$WORK/buildscripts/remom-patch.py" "$WORK/stage/ReMoM" || { log "BUILD PRZERWANY - remom-patch.py"; exit 1; }
# walka taktyczna z ekranem do testow (REMOM_WALKA) - wyciete z HeMoM.c
python3 "$WORK/buildscripts/walka-gen.py" "$WORK/stage/ReMoM" || { log "BUILD PRZERWANY - walka-gen.py"; exit 1; }

# Zalatany wynik trafia do drzewa kompilacji znowu po zawartosci: plik, ktorego
# latki nie zmienily, zachowuje mtime i nie wymusza rekompilacji.
rsync -rc --delete "$WORK/stage/ReMoM/" "$WORK/src/"
SRC=$WORK/src
NATIVE=$WORK/native

mkdir -p "$WORK/gen"
sed "s/@REMOM_VERSION@/0.0.0-amiga-$UPSTREAM_REV/" "$SRC/cmake/remom_version.h.in" > "$WORK/gen/remom_version.h.new"
if ! cmp -s "$WORK/gen/remom_version.h.new" "$WORK/gen/remom_version.h"; then
	mv "$WORK/gen/remom_version.h.new" "$WORK/gen/remom_version.h"
else
	rm -f "$WORK/gen/remom_version.h.new"
fi

# ------------------------------------------------------------------- flagi --

CPU="-mcpu=68020 -msoft-float"
# -fno-peephole2: defekt toolchainu nr 6 (CLAUDE.md) - peephole2 sklada
# clr.w N z clr.l N+2 w jedno clr.l N i zostawia dwa bajty niewyzerowane.
#
# -fno-tree-scev-cprop -fno-branch-count-reg: defekt toolchainu nr 7
# (2026-09-17, CLAUDE.md). Backend m68k rozszerza bajt do slowa/dlugiego samym
# "move.b" do rejestru, ktorego gorne bity sa brudne (po "moveq #-2"). Przy
# -O2 wychodzi to w wartosci koncowej wskaznika po petli (scev-cprop: src i dst
# cofaja sie o ~250 bajtow), bez scev-cprop - w liczniku dbra (0xFF0D zamiast
# 13 obrotow, potem 4 mld). Dekodowanie klatek FLIC (Add_Picture_To_Bitmap)
# niszczylo naglowek bitmapy i wieszalo HeMoM. Odtworzone sonda probe/flic:
# -O0 czysto, -O1 i -O2 zle, -O2 z tymi dwiema flagami czysto.
FIX="-fno-peephole2 -fno-tree-scev-cprop -fno-branch-count-reg"
OPT="-O2 $FIX"
COMMON="$CPU -noixemul -fomit-frame-pointer"
# NO_SOUND_LIBRARY: upstream wlacza go, gdy nie ma SDL_mixer. Dzwiek przez
# Paule to osobny etap.
# NDEBUG (2026-09-17, optymalizacja): 241 asercji silnika siedzialo w petlach
# rysowania (FLIC_Draw_Frame: przy kazdym pikselu). Zadna nie ma skutkow
# ubocznych (sprawdzone grepem: brak ++/--/przypisan; walidatory tylko czytaja),
# a wydanie CMake upstreamu tez je wylacza.
# STU_LOG_MIN_SEVERITY=INFO (2026-09-17): LOG_DEBUG/LOG_TRACE (754 miejsc,
# w tym [FN-ENTER]/[FN-EXIT] w goracych funkcjach) to ~75% czasu tury AI.
# Upstream przewiduje to makro; argumenty nie sa wtedy liczone - zaden nie ma
# skutkow ubocznych (sprawdzone grepem). INFO zostaje (slad RNG dla regresji).
REMOM_DEFS="-D__AMIGA__ -DNO_SOUND_LIBRARY -DMOUSE_DEBUG -DNDEBUG -DSTU_LOG_MIN_SEVERITY=LOG_SEV_INFO"   # MOUSE_DEBUG: znaczniki etapow do Work:mouse_debug.log (diagnoza startu ekranu glownego)
# amiga_le.h MUSI byc pierwszy: zamyka naglowki systemowe w kolejnosci
# natywnej, a potem przelacza wszystkie struktury silnika na little-endian.
REMOM_INCS="-include $NATIVE/remom/amiga_le.h -I$SRC/platform/include -I$SRC/src -I$WORK/gen -I$NATIVE"
REMOM_FLAGS="$COMMON -std=gnu99 $REMOM_DEFS $REMOM_INCS"
NATIVE_FLAGS="$COMMON -std=gnu99 -D__AMIGA__ -I$NATIVE -I$NATIVE/cgx-include"
LIBS="-lamiga -lm"

OBJ=$WORK/obj
mkdir -p "$OBJ" "$WORK/warn"

# Build przebudowuje po zawartosci zrodel, nie po flagach - zmiana flag
# zostawiala stare obiekty (pulapka opisana w CLAUDE.md, trafiona znowu
# 2026-09-17 przy wlaczaniu MOUSE_DEBUG: binarka identyczna co do bajtu).
# Wiec: inne flagi niz poprzednio -> obiekty od zera.
FLAGSUM=$(printf '%s|%s|%s' "$REMOM_FLAGS" "$OPT" "$NATIVE_FLAGS" | md5sum | cut -d' ' -f1)
if [ "$(cat "$WORK/flags.sum" 2>/dev/null)" != "$FLAGSUM" ]; then
	log "flagi kompilacji sie zmienily - obiekty od zera"
	rm -f "$OBJ"/*.o "$OBJ"/*.d
	echo "$FLAGSUM" > "$WORK/flags.sum"
fi
: > "$WORK/built.list"
: > "$WORK/ice.list"
export CC_WORK="$WORK"

# lista zrodel z CMakeLists upstreamu - zeby build szedl za upstreamem
# zamiast za reczna lista, ktora rozjechalaby sie po pierwszej aktualizacji
cmake_block() {   # $1 plik, $2 poczatek bloku, $3 prefiks sciezki
	tr -d '\r' < "$1" | sed -n "/$2/,/)/p" | grep -oE '[^ ]+\.c' | while read -r f; do
		case "$f" in
			'${PROJECT_SOURCE_DIR}/'*) echo "${f#\$\{PROJECT_SOURCE_DIR\}/}" ;;
			*) echo "$3$f" ;;
		esac
	done
}

LIST=$WORK/sources.list
{
	# STU_WRLD.c (symulacja generatora swiata, ~40 miejsc z double) - bez
	# niego: jego jedyny symbol globalny nie jest uzywany (2026-09-17)
	cmake_block "$SRC/STU/src/CMakeLists.txt" "set(LIBSTU_SOURCES" "STU/src/" | grep -v '^STU/src/STU_WRLD\.c$'
	cmake_block "$SRC/MoX/src/CMakeLists.txt" "set(LIBMOX_SOURCES" "MoX/src/"
	cmake_block "$SRC/MoM/src/CMakeLists.txt" "set(LIBMOM_SOURCES" "MoM/src/"
	cmake_block "$SRC/platform/CMakeLists.txt" "add_library(Platform_Headless" "platform/"
	echo src/remom_walka.c
} > "$LIST"
HEMOM_LIST=$WORK/hemom.list
# tr -d '\r': CMakeLists upstreamu ma konce linii CRLF i bez tego ostatnia
# nazwa pliku z listy dostawala \r na koncu, a grep '\.c$' ja gubil - linker
# zglaszal wtedy brak Game_Save_Dump.
tr -d '\r' < "$SRC/src/CMakeLists.txt" | sed -n 's/^add_executable(HeMoM \(.*\))/\1/p' | tr ' ' '\n' | grep '\.c$' | sed 's#^#src/#' > "$HEMOM_LIST"
log "zrodel silnika: $(wc -l < "$LIST"), HeMoM: $(wc -l < "$HEMOM_LIST")"

# ------------------------------------------------------------- kompilacja --

log "kompilacja silnika ($JOBS rownolegle)"
export CC_FLAGS="$REMOM_FLAGS" CC_OPT="$OPT" CC_FIX="$FIX"
if ! cat "$LIST" "$HEMOM_LIST" | xargs -P "$JOBS" -I{} sh "$WORK/buildscripts/cc-one.sh" "$SRC" {} "$OBJ"; then
	log "BUILD PRZERWANY - bledy kompilacji wyzej"
	exit 1
fi

log "kompilacja warstwy natywnej"
# fp_*.c podmieniaja rutyny, ktore GCC umie rozpoznac i zamienic z powrotem
# na wywolania wbudowane - stad -fno-builtin.
export CC_FLAGS="$NATIVE_FLAGS -fno-builtin"
for f in fp_conv.c fp_single.c fp_double.c; do
	sh "$WORK/buildscripts/cc-one.sh" "$NATIVE" "$f" "$OBJ"
done
export CC_FLAGS="$NATIVE_FLAGS"
# znacznik built: ma oznaczac TEN build - obiekt banera zawsze od nowa
rm -f "$OBJ/remom_amiga_banner.c.o" "$OBJ/remom_amiga_banner.c.o.d"
for f in amiga_trap.c amiga_stack.c remom/amiga_posix.c remom/amiga_endian.c remom/amiga_banner.c remom/amiga_profil.c remom/amiga_czas.c remom/amiga_wb.c remom/amiga_hemom_stuby.c; do
	sh "$WORK/buildscripts/cc-one.sh" "$NATIVE" "$f" "$OBJ"
done

objs() { while read -r f; do printf '%s ' "$OBJ/$(echo "$f" | tr / _).o"; done < "$1"; }
ENGINE_OBJS=$(objs "$LIST")
HEMOM_OBJS=$(objs "$HEMOM_LIST")
NATIVE_OBJS="$OBJ/fp_conv.c.o $OBJ/fp_single.c.o $OBJ/fp_double.c.o $OBJ/amiga_trap.c.o $OBJ/amiga_stack.c.o $OBJ/remom_amiga_posix.c.o $OBJ/remom_amiga_endian.c.o $OBJ/remom_amiga_banner.c.o $OBJ/remom_amiga_profil.c.o $OBJ/remom_amiga_czas.c.o $OBJ/remom_amiga_wb.c.o"

# ------------------------------------------------------------- linkowanie --

log "linkowanie hemom (silnik bez ekranu)"
# Obiekty natywne PRZED -lm: linker bierze nasze __mulsf3/__divsf3/__floatsidf
# i nigdy nie siega po zepsute czlonki libm.a.
m68k-amigaos-gcc $COMMON -o "$WORK/hemom" $HEMOM_OBJS $ENGINE_OBJS $NATIVE_OBJS $OBJ/remom_amiga_hemom_stuby.c.o $LIBS 2>"$WORK/link.txt" || {
	log "LINKOWANIE NIEUDANE:"
	grep -E "undefined reference|multiple definition|error" "$WORK/link.txt" | sed 's/^.*: //' | sort | uniq -c | sort -rn | head -40
	exit 1
}
grep -E "duplicate section" "$WORK/link.txt" | head -5 || true

# ------------------------------------------------ ReMoM z ekranem (AGA/RTG) --

# Backend platformy Amigi: native/remom/platform_amiga/ (w miejsce
# platform/headless z upstreamu) + warstwa grafiki z portu OpenXcom.
log "kompilacja ReMoM z ekranem"
export CC_FLAGS="$REMOM_FLAGS"
REMOM_BACKEND=""
for f in amiga_State.c amiga_Timer.c amiga_Stubs.c amiga_KD.c amiga_Video.c amiga_PFL.c amiga_Auto.c amiga_Audio.c amiga_Opcje.c; do
	sh "$WORK/buildscripts/cc-one.sh" "$NATIVE" "remom/platform_amiga/$f" "$OBJ"
	REMOM_BACKEND="$REMOM_BACKEND $OBJ/remom_platform_amiga_$f.o"
done
sh "$WORK/buildscripts/cc-one.sh" "$SRC" "src/ReMoM.c" "$OBJ"
# amiga_Req.c ciagnie naglowki systemu - bez wymuszonego amiga_le.h;
# amiga_gfx.c to warstwa z portu OpenXcom.
export CC_FLAGS="$NATIVE_FLAGS"
for f in remom/platform_amiga/amiga_Req.c amiga_gfx.c amiga_audio.c amiga_adpcm.c remom/platform_amiga/amiga_Domyslny_aga.c; do
	sh "$WORK/buildscripts/cc-one.sh" "$NATIVE" "$f" "$OBJ"
done
REMOM_BACKEND="$REMOM_BACKEND $OBJ/remom_platform_amiga_amiga_Req.c.o $OBJ/amiga_gfx.c.o $OBJ/amiga_audio.c.o $OBJ/amiga_adpcm.c.o"
# c2p Kalmsa w skladni Motoroli - vasm z amiga-gcc
if [ ! -f "$OBJ/c2p_glue.o" ] || [ "$NATIVE/c2p_glue.s" -nt "$OBJ/c2p_glue.o" ]; then
	vasmm68k_mot -Fhunk -m68020 -no-opt -I/opt/amiga/m68k-amigaos/ndk-include -I"$NATIVE" \
		-o "$OBJ/c2p_glue.o" "$NATIVE/c2p_glue.s"
fi
REMOM_BACKEND="$REMOM_BACKEND $OBJ/c2p_glue.o"

# silnik bez platformy headless (Replay.c i PFL_Perf.c zostaja)
ENGINE_NOHL=$(grep -v '^platform/headless/' "$LIST" | while read -r f; do printf '%s ' "$OBJ/$(echo "$f" | tr / _).o"; done)
REMOM_OBJS="$OBJ/src_ReMoM.c.o $OBJ/src_ReMoM_Init.c.o $OBJ/src_Artificial_Human_Player.c.o"

# Jedna binarka gry (developer 2026-09-17: tryb obrazu wybiera sie
# w remom-prefs, druga wersja niepotrzebna). Domyslny tryb: AGA
# (amiga_Domyslny_aga.c); gfx= w amiga.cfg i REMOM_GFX wygrywaja.
log "linkowanie remom"
m68k-amigaos-gcc $COMMON -Wl,-Map,"$WORK/remom.map" -o "$WORK/remom" $REMOM_OBJS $ENGINE_NOHL $REMOM_BACKEND 	"$OBJ/remom_platform_amiga_amiga_Domyslny_aga.c.o" $NATIVE_OBJS $LIBS 2>"$WORK/link-remom.txt" || {
	log "LINKOWANIE remom NIEUDANE:"
	grep -E "undefined reference|multiple definition|error" "$WORK/link-remom.txt" | sed 's/^.*: //' | sort | uniq -c | sort -rn | head -40
	exit 1
}
grep -E "duplicate section" "$WORK/link-remom.txt" | head -5 || true
cp "$WORK/remom" "$DEPLOY/remom"
ls -la "$WORK/remom"
# dawne nazwy (remom-aga, remom-rtg - do 2026-09-17) - artefakty tego builda
rm -f "$DEPLOY/remom-aga" "$DEPLOY/remom-aga.info" "$DEPLOY/remom-rtg" "$DEPLOY/remom-rtg.info"

# Ikony Workbencha (build/mkicon.py - rysunek nasz, format z portu OpenXcom).
mkdir -p "$WORK/ikony"
rm -f "$WORK"/ikony/*.info   # tylko biezace ikony - dawne nazwy nie wracaja do Work:
python3 "$WORK/buildscripts/mkicon.py" "$WORK/ikony" >/dev/null || { log "BUILD PRZERWANY - mkicon.py"; exit 1; }
for f in "$WORK"/ikony/*.info; do
	cmp -s "$f" "$DEPLOY/$(basename "$f")" || cp "$f" "$DEPLOY/"
done

# ------------------------------------------------------------------ raport --

if [ -s "$WORK/ice.list" ]; then
	log "skompilowane PONIZEJ $OPT po bledzie kompilatora (koszt predkosci):"
	sed 's/^/    /' "$WORK/ice.list"
fi
NBUILT=$(wc -l < "$WORK/built.list")
if [ "$NBUILT" -eq 0 ]; then
	log "przekompilowano: NIC"
else
	log "przekompilowano: $NBUILT plikow"
fi

# NIGDY nie stripowac (Hunk po strip zatrzymuje maszyne, HALT1).
cp "$WORK/hemom" "$DEPLOY/hemom"
ls -la "$WORK/hemom"

# Straznik resetu (native/remom/straznik.c): Work:run uruchamia go w tle,
# a host resetuje Amige plikiem Work:reset.flag - bez restartu emulatora.
m68k-amigaos-gcc $NATIVE_FLAGS -O2 -o "$WORK/straznik" "$NATIVE/remom/straznik.c" 2>"$WORK/warn/straznik.txt" || {
	log "BLAD KOMPILACJI: straznik.c"; cat "$WORK/warn/straznik.txt"; exit 1; }
cmp -s "$WORK/straznik" "$DEPLOY/straznik" || cp "$WORK/straznik" "$DEPLOY/straznik"
# wbstart (native/remom/wbstart.c): test startu z ikony bez myszy
m68k-amigaos-gcc $NATIVE_FLAGS -O2 -o "$WORK/wbstart" "$NATIVE/remom/wbstart.c" 2>"$WORK/warn/wbstart.txt" || {
	log "BLAD KOMPILACJI: wbstart.c"; cat "$WORK/warn/wbstart.txt"; exit 1; }
cmp -s "$WORK/wbstart" "$DEPLOY/wbstart" || cp "$WORK/wbstart" "$DEPLOY/wbstart"
# remom-prefs (native/remom/remom-prefs.c): ustawienia w oknie Workbencha,
# wzor: tools/gtaprefs.c z portu AmiGTA
# + konwerter muzyki (native/remom/muzyka_konw.c) z konwerterem XMIDI ReMoM
# wycietym mechanicznie (build/xmi2mid-gen.py ... biblioteka) - 2026-09-24
python3 "$WORK/buildscripts/xmi2mid-gen.py" "$WORK/stage/ReMoM" "$WORK/xmid-amiga.c" biblioteka >/dev/null
m68k-amigaos-gcc $NATIVE_FLAGS -O2 -w -I"$WORK/stage/ReMoM/platform/sdl2" -c -o "$WORK/xmid-amiga.o" "$WORK/xmid-amiga.c" 2>"$WORK/warn/xmid-amiga.txt" || {
	log "BLAD KOMPILACJI: xmid-amiga.c"; cat "$WORK/warn/xmid-amiga.txt"; exit 1; }
m68k-amigaos-gcc $NATIVE_FLAGS -O2 -o "$WORK/remom-prefs" "$NATIVE/remom/remom-prefs.c" "$NATIVE/remom/muzyka_konw.c" "$WORK/xmid-amiga.o" 2>"$WORK/warn/remom-prefs.txt" || {
	log "BLAD KOMPILACJI: remom-prefs.c"; cat "$WORK/warn/remom-prefs.txt"; exit 1; }
cmp -s "$WORK/remom-prefs" "$DEPLOY/remom-prefs" || cp "$WORK/remom-prefs" "$DEPLOY/remom-prefs"

# Work:run - to, co maszyna wykona po starcie (User-Startup odpala Work:run).
# Skrypt wybiera sie parametrem RUN (plik build/run-<RUN>), domyslnie hemom.
RUN=${RUN:-hemom}
if ! cmp -s "$REPO/build/run-$RUN" "$DEPLOY/run"; then
	cp "$REPO/build/run-$RUN" "$DEPLOY/run"
	log "Work:run <- build/run-$RUN"
fi
# skrypty wejscia dla Work:autoinput.txt (amiga_Auto.c), kopiowane przez run-*
for f in "$REPO"/build/autoinput-*.txt; do
	cmp -s "$f" "$DEPLOY/$(basename "$f")" || cp "$f" "$DEPLOY/"
done
log "wdrozone do $DEPLOY"
log "gotowe"
