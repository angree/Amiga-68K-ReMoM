#!/bin/sh
#
# cc-one.sh - kompiluje JEDEN plik; wolany rownolegle przez build.sh (xargs -P).
#
#   cc-one.sh <katalog zrodel> <sciezka wzgledna> <katalog obiektow>
#
# Flagi przychodza w zmiennej srodowiska CC_FLAGS, a optymalizacja osobno
# w CC_OPT (zeby fallback po ICE mogl ja zamienic). CC_FIX to obejscia
# defektow kompilatora, ktore MUSZA isc takze z nizszymi poziomami:
# -O1 ma defekt nr 7 tak samo jak -O2. Wynik: obiekt
# <katalog obiektow>/<sciezka z _ zamiast />.o i jego plik zaleznosci .d.
#
# Plik przekompilowany dopisuje sie do $CC_WORK/built.list, a zdegradowany
# po bledzie kompilatora - do $CC_WORK/ice.list. Ostrzezenia ida do
# $CC_WORK/warn/<obiekt>.txt, zeby 245 tysiecy linii cudzego kodu nie zalalo
# wyjscia buildu.

src="$1/$2"
name=$(echo "$2" | tr / _)
out="$3/$name.o"
warn="$CC_WORK/warn/$name.txt"

needs_build() {
	[ -f "$out" ] || return 0
	[ "$src" -nt "$out" ] && return 0
	if [ -f "$out.d" ]; then
		for dep in $(sed -e 's/\\$//' -e 's/^[^:]*://' "$out.d" | tr ' ' '\n' | grep -v ':$' | grep -v '^$'); do
			[ -f "$dep" ] || return 0
			[ "$dep" -nt "$out" ] && return 0
		done
		return 1
	fi
	return 0
}

needs_build || exit 0

# Nadpisanie optymalizacji dla pojedynczych plikow (build/opt-override.txt).
if [ -f "$CC_WORK/buildscripts/opt-override.txt" ]; then
	nadpis=$(grep -v '^#' "$CC_WORK/buildscripts/opt-override.txt" | awk -v f="$2" '$1 == f { $1 = ""; print; exit }')
	if [ -n "$nadpis" ]; then
		CC_OPT="$nadpis"
		echo "$2 ($nadpis - opt-override.txt)" >> "$CC_WORK/ice.list"
	fi
fi

echo "$2" >> "$CC_WORK/built.list"

# cc1 na WSL1 potrafi sie wysypac na duzych plikach (rekursja w przebiegach
# RTL przepelnia stos) - wyglada to jak blad kompilatora. Stos w gore,
# a to, co nadal pada, schodzi nizej i trafia na liste.
ulimit -s 65536 2>/dev/null || true

if m68k-amigaos-gcc $CC_FLAGS $CC_OPT -MMD -MP -MF "$out.d" -c "$src" -o "$out" 2>"$warn"; then
	exit 0
fi
# ICE jest PRAWDZIWY i powtarzalny, nie skutek rownoleglosci: Util.c
# i CMBTMVPT.c padaja z "Segmentation fault" na -O2 takze szeregowo
# (sprawdzone 2026-09-17, bo pierwsza hipoteza - brak pamieci przy
# rownoleglych cc1 - okazala sie falszywa). Ponawianie nic nie daje.
if grep -q "internal compiler error" "$warn"; then
	# Defekt toolchainu nr 8 (CLAUDE.md): segfault w przebiegu RTL
	# "bebbo's-optimizers" - 12-linijkowa petla wywoluje go juz na -O1
	# (probe/ice/ice-bebbo.c). -fbbb=- wylacza tylko ten przebieg, reszta
	# optymalizacji zostaje - zamiast spadku calego pliku do -O1/-O0.
	if m68k-amigaos-gcc $CC_FLAGS $CC_OPT -fbbb=- -MMD -MP -MF "$out.d" -c "$src" -o "$out" 2>"$warn"; then
		echo "$2 (-fbbb=-)" >> "$CC_WORK/ice.list"
		exit 0
	fi
	if m68k-amigaos-gcc $CC_FLAGS $CC_OPT -fno-inline -MMD -MP -MF "$out.d" -c "$src" -o "$out" 2>"$warn"; then
		echo "$2 (-fno-inline)" >> "$CC_WORK/ice.list"
		exit 0
	fi
	if m68k-amigaos-gcc $CC_FLAGS -O1 $CC_FIX -MMD -MP -MF "$out.d" -c "$src" -o "$out" 2>"$warn"; then
		echo "$2 (-O1)" >> "$CC_WORK/ice.list"
		exit 0
	fi
	if m68k-amigaos-gcc $CC_FLAGS -O0 $CC_FIX -MMD -MP -MF "$out.d" -c "$src" -o "$out" 2>"$warn"; then
		echo "$2 (-O0)" >> "$CC_WORK/ice.list"
		exit 0
	fi
fi
echo "BLAD KOMPILACJI: $2" >&2
grep -E "error|Error" "$warn" | head -20 >&2
rm -f "$out"
exit 1
