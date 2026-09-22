/*
 * amiga_Domyslny_aga.c - domyslny tryb obrazu gry (remom), gdy nic go nie
 * wybralo. Kolejnosc w amiga_PFL.c: argument/latka > zmienna REMOM_GFX >
 * gfx= w amiga.cfg (remom-prefs) > ta stala. Osobnej binarki RTG juz nie ma
 * (developer 2026-09-17) - RTG wybiera sie w remom-prefs.
 */
#include "amiga_gfx.h"

int amiga_domyslny_backend = AMIGAGFX_BACKEND_AGA;
