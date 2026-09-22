#!/bin/sh
#
# build-host-sdl.sh - ReMoM (Z EKRANEM) zbudowany natywnie w WSL z prawdziwa
# platforma sdl2 upstreamu. Wyrocznia dla ekranow, ktorych HeMoM (headless)
# nie umie - przede wszystkim walki taktycznej: upstream sam uruchamia
# combat_s1/combat_s2 tylko w wersji okienkowej
# (doc/@AI_Plans/BRA-Combat-Testing.md).
#
# Silnik bierze z obiektow build-host.sh (te same flagi, GCC, bez STU_DEBUG),
# wiec NAJPIERW build-host.sh. Bez dzwieku (DISABLE_AUDIO). Ekran:
# SDL_VIDEODRIVER=dummy przy uruchomieniu - SDL rysuje do pamieci.
# Klatki: HOST_KLATKI=<katalog> (native/remom/host/host_klatki.c), jak w HeMoM.
#
# Uzycie (w WSL):  sh /mnt/i/GITHUB/Ami_ReMoM/build/build-host-sdl.sh
# Wynik:           $HOME/build-remom-host/remom-sdl
#
set -e
ls /mnt/i/GITHUB >/dev/null 2>&1 || sudo -n mount -t drvfs I: /mnt/i 2>/dev/null || true

WORK=$HOME/build-remom-host
SRC=$WORK/src
OBJ=$WORK/obj-sdl
[ -x "$WORK/hemom" ] || { echo "najpierw build-host.sh"; exit 1; }
mkdir -p "$OBJ"

# hak na klatki w Platform_Video_Update() z sdl2 (tylko to drzewo hostowe)
V=$SRC/platform/sdl2/sdl2_Video.c
if ! grep -q Host_Klatka "$V"; then
	python3 - "$V" <<'EOF'
import sys
p = sys.argv[1]
t = open(p, encoding="latin-1").read()
k = t.index("void Platform_Video_Update(void)")
k = t.index("{", k) + 1
t = t[:k] + "\n    { extern void Host_Klatka(void); Host_Klatka(); }  /* HOST: wyrocznia obrazu */\n" + t[k:]
open(p, "w", encoding="latin-1").write(t)
EOF
fi
grep -q Host_Klatka "$V" || { echo "LATKA NIE PASUJE: sdl2_Video.c"; exit 1; }

# Jak na Amidze (remom-patch.py, latki_remom): dane, zapisy i log w katalogu
# biezacym, nie w katalogach XDG pod $HOME - inaczej SAVE9.GAM moze przyjsc
# z innego miejsca niz na Amidze.
R=$SRC/src/ReMoM.c
sed -i 's|    STU_GRAF_Init(STU_GRAF_PLAYER);|    STU_GRAF_Init(STU_GRAF_HEADLESS);  /* HOST: jak na Amidze */|' "$R"
grep -q "HOST: jak na Amidze" "$R" || { echo "LATKA NIE PASUJE: ReMoM.c STU_GRAF_Init"; exit 1; }

# Test walki z ekranem (REMOM_WALKA) - jak w buildzie Amigi: remom_walka.c
# wyciety z HeMoM.c i wywolanie przed pierwszym Main_Screen(). MOM_SCR.c
# kompilujemy tu z hakiem i podmieniamy nim obiekt silnika z build-host.sh.
python3 "$(dirname "$0")/walka-gen.py" "$SRC"
python3 - "$SRC/MoM/src/MOM_SCR.c" "$SRC/MoM/src/MOM_SCR_walka.c" <<'EOF'
import sys
t = open(sys.argv[1], encoding="latin-1", newline="").read()
k = "                Main_Screen();"
assert t.count(k) == 1, "LATKA NIE PASUJE: MOM_SCR.c Main_Screen()"
t = t.replace(k, "                { extern int Remom_Walka_Z_Env(void); Remom_Walka_Z_Env(); }  /* HOST: test walki */\n" + k)
open(sys.argv[2], "w", encoding="latin-1", newline="").write(t)
EOF

FLAGS="-O2 -std=gnu99 -w -DNO_SOUND_LIBRARY -I$SRC/platform/include -I$SRC/platform/sdl2 -I$SRC/src -I$SRC -I$WORK/gen $(sdl2-config --cflags)"
cd "$SRC"
for f in platform/sdl2/sdl2_EMM.c platform/sdl2/sdl2_Init.c platform/sdl2/sdl2_KD.c \
         platform/sdl2/sdl2_MD.c platform/sdl2/sdl2_PFL.c platform/sdl2/sdl2_State.c \
         platform/sdl2/sdl2_Timer.c platform/sdl2/sdl2_Video.c platform/replay/Replay.c \
         platform/capture/PFL_Capture.c platform/metrics/PFL_Input_Metrics.c \
         platform/metrics/PFL_Perf.c src/ReMoM.c src/ReMoM_Init.c \
         src/Artificial_Human_Player.c host/host_klatki.c \
         src/remom_walka.c MoM/src/MOM_SCR_walka.c; do
	o=$OBJ/$(echo "$f" | tr / _).o
	if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
		gcc $FLAGS -c "$f" -o "$o"
	fi
done
# silnik: obiekty build-host.sh bez platformy headless i bez programow
SILNIK=$(ls "$WORK"/obj/*.o | grep -e '/STU_' -e '/MoX_' -e '/MoM_' -e '/ext_' | grep -v '/MoM_src_MOM_SCR.c.o' | tr '\n' ' ')
gcc -o "$WORK/remom-sdl" $OBJ/*.o $SILNIK $(sdl2-config --libs) -lm
ls -la "$WORK/remom-sdl"
echo "== gotowe (host, sdl2)"
