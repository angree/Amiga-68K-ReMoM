#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
walka-gen.py - walka taktyczna Z EKRANEM w ReMoM, do testow.

HeMoM ma --combat-tactical (AI prowadzi obie strony, ustawienie walki na
SAVECMBT.GAM), ale HeMoM nie ma ekranu. Ten skrypt wycina z src/HeMoM.c
MECHANICZNIE (bez zmian w tresci) blok includow i fragment od
`#define HEMOM_COMBAT_MAX_TROOPS` do `static void Print_Usage(` - czyli
HeMoM_Combat_Dump_Unit i HeMoM_Combat_Run - i sklada z nich
src/remom_walka.c z jedna funkcja wejscia:

    int Remom_Walka_Z_Env(void)

Gdy zmienna REMOM_WALKA = "<obronca>:<atak1,atak2,...>" (spacja tez dziala na PC) jest ustawiona,
funkcja (raz) odpala HeMoM_Combat_Run(..., tactical=1), zapisuje
HEMOM_COMBAT.txt i konczy program (exit 0). remom-patch.py (Amiga) i
build-host-sdl.sh (PC) wolaja ja przed pierwszym Main_Screen().

UZYCIE:  python3 walka-gen.py <katalog z rozpakowanym ReMoM>
"""

import io
import os
import sys

root = sys.argv[1]
src = os.path.join(root, "src", "HeMoM.c")
t = io.open(src, encoding="latin-1", newline="").read().replace("\r\n", "\n")

a = t.index("#define HEMOM_COMBAT_MAX_TROOPS")
b = t.index("static void Print_Usage(")
fragment = t[a:b]

linie = t.split("\n")
# blok od pierwszej dyrektywy (z #ifdef _WIN32 wokol windows.h) do ostatniego
# includu programu
p = next(i for i, l in enumerate(linie) if l.startswith("#"))
ostatni = next(i for i, l in enumerate(linie) if l.startswith('#include "Game_Save_Dump.h"'))
naglowki = "\n".join(linie[p:ostatni + 1])

wyjscie = """/* remom_walka.c - WYGENEROWANE przez build/walka-gen.py z src/HeMoM.c.
   Nie edytowac: zmiany w build/walka-gen.py. */

%s

%s

int Remom_Walka_Z_Env(void)
{
    static int zrobione = 0;
    const char * env;
    char bufor[128];
    char * token;
    int16_t obronca;
    int16_t oddzial[HEMOM_COMBAT_MAX_TROOPS];
    int16_t ile = 0;
    int wynik;

    if(zrobione)
    {
        return 0;
    }
    zrobione = 1;
    bufor[0] = '\\0';
    /* Najpierw plik remom-walka.txt w katalogu biezacym (Work: na Amidze).
       Zmienna REMOM_WALKA tez dziala, ale na Amidze tylko LOKALNA (Set):
       getenv() z libnix nie widzi SetEnv (sprawdzone 2026-09-17). */
    {
        FILE * plik = fopen("remom-walka.txt", "r");
        if(plik != NULL)
        {
            if(fgets(bufor, sizeof(bufor), plik) == NULL)
            {
                bufor[0] = '\\0';
            }
            fclose(plik);
        }
    }
    if(bufor[0] == '\\0')
    {
        env = getenv("REMOM_WALKA");
        if(env == NULL)
        {
            printf("[walka] brak remom-walka.txt i REMOM_WALKA - zwykla gra\\n");
            fflush(stdout);
            return 0;
        }
        strncpy(bufor, env, sizeof(bufor) - 1);
        bufor[sizeof(bufor) - 1] = '\\0';
    }
    token = strtok(bufor, " :\\r\\n");
    if(token == NULL)
    {
        return 0;
    }
    obronca = (int16_t)atoi(token);
    token = strtok(NULL, " :,\\r\\n");
    while(token != NULL && ile < HEMOM_COMBAT_MAX_TROOPS)
    {
        oddzial[ile] = (int16_t)atoi(token);
        ile++;
        token = strtok(NULL, " :,\\r\\n");
    }
    printf("[walka] REMOM_WALKA: obronca %%d, atakujacych %%d\\n", (int)obronca, (int)ile);
    fflush(stdout);
    wynik = HeMoM_Combat_Run(obronca, ile, oddzial, 1);
    printf("[walka] HeMoM_Combat_Run -> %%d, koniec programu\\n", wynik);
    fflush(stdout);
    exit(0);
    return 1;
}
""" % (naglowki, fragment)

cel = os.path.join(root, "src", "remom_walka.c")
io.open(cel, "w", encoding="latin-1", newline="\n").write(wyjscie)
print("walka-gen: %s (%d linii fragmentu)" % (cel, fragment.count("\n")))
