# -*- coding: utf-8 -*-
# skanuj-tablice-le.py - ciche pulapki kolejnosci bajtow: tablice WIELOBAJTOWE
# w strukturach little-endian uzyte jako wskaznik (rozklad tablicy).
#
# amiga_le.h trzyma struktury silnika w little-endian. GCC odmawia jawnego
# `&s.pole`, ale NIE zglasza rozkladu tablicy do wskaznika:
#     uint16_t *p = _players[i].research_spells;   // cicho!
#     p[k]                                          // natywny odczyt LE -> zle
# Tak powstal pierwszy blad logiki gry na Amidze (2026-09-17): AI badan czarow
# czytalo odwrocone numery czarow i dzielilo przez zero po 2228 wywolaniach RNG
# zgodnych z PC.
#
# Skaner: (1) zbiera z naglowkow pola-tablice typow wielobajtowych,
# (2) szuka w .c ich uzyc BEZ indeksu (`.pole` / `->pole` nie poprzedzone &
# i nie zakonczone `[`), poza sizeof i memcpy/memset (bajtowo - bezpieczne).
# Wynik to lista do przejrzenia, nie werdykt - rozklad do (uint8_t *) jest
# bezpieczny, rozklad do int16_t * juz nie.
#
# UZYCIE:  python skanuj-tablice-le.py <katalog zrodel ReMoM>

import os
import re
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else r"I:\GITHUB\_AmiReMoM-poza-repo\upstream\ReMoM"
KATALOGI = ["MoX/src", "MoM/src", "STU/src", "src"]
TYPY = r"(?:u?int16_t|u?int32_t|short|unsigned\s+short|long|unsigned\s+long|int|unsigned\s+int|unsigned|SAMB_ptr|int16|uint16)"
POLE = re.compile(r"^\s*(?:/\*[^*]*\*/\s*)?" + TYPY + r"\s+([A-Za-z_][A-Za-z_0-9]*)\s*\[[^\]]+\]\s*(?:\[[^\]]+\]\s*)?;")

pola = {}
skalary = set()
SKALAR = re.compile(r"^\s*(?:/\*[^*]*\*/\s*)?(?:const\s+)?[A-Za-z_][A-Za-z_0-9 ]*[\s\*]+([A-Za-z_][A-Za-z_0-9]*)\s*(?::\s*\d+)?\s*;")
for k in KATALOGI:
    d = os.path.join(ROOT, k)
    for n in os.listdir(d):
        if not n.endswith(".h"):
            continue
        with open(os.path.join(d, n), encoding="latin-1") as f:
            w_strukturze = 0
            for nr, linia in enumerate(f, 1):
                if re.match(r"^\s*(typedef\s+)?struct\b", linia):
                    w_strukturze = 1
                if w_strukturze:
                    m = POLE.match(linia)
                    if m:
                        pola.setdefault(m.group(1), []).append("%s:%d" % (n, nr))
                    else:
                        s = SKALAR.match(linia)
                        if s:
                            skalary.add(s.group(1))
                if re.match(r"^\s*\}", linia):
                    w_strukturze = 0

niejednoznaczne = sorted(n for n in pola if n in skalary)
for n in niejednoznaczne:
    del pola[n]
print("pol-tablic wielobajtowych w strukturach: %d (pominiete, bo gdzie indziej sa skalarem: %s)" % (len(pola), ", ".join(niejednoznaczne)))
znal = 0
for k in KATALOGI:
    d = os.path.join(ROOT, k)
    for n in sorted(os.listdir(d)):
        if not n.endswith(".c"):
            continue
        with open(os.path.join(d, n), encoding="latin-1") as f:
            for nr, linia in enumerate(f, 1):
                kod = linia.split("//")[0]
                for m in re.finditer(r"(?<!&)(?:\.|->)([A-Za-z_][A-Za-z_0-9]*)\b(?!\s*\[)", kod):
                    nazwa = m.group(1)
                    if nazwa not in pola:
                        continue
                    if re.search(r"sizeof|memset|memcpy|memmove|LE_FIELD_PTR|uint8_t\s*\*|char\s*\*", kod):
                        continue
                    znal += 1
                    print("%s/%s:%d  %s   [%s]" % (k, n, nr, linia.strip()[:120], nazwa))
print("podejrzanych miejsc: %d" % znal)
