/*
 * muzyka_konw.h - konwerter muzyki Master of Magic NA SAMEJ AMIDZE
 * (i na PC do testow): XMIDI z LBX gracza -> MIDI (konwerter ReMoM,
 * build/xmi2mid-gen.py) -> nasz prosty syntezator -> WAV IMA ADPCM mono.
 * Nazwy plikow jak w build/muzyka-render.py: muzyka/<fnv>p.wav (w petli)
 * albo <fnv>1.wav (raz) - gra szuka ich w native/remom/platform_amiga/amiga_Audio.c.
 */
#ifndef MUZYKA_KONW_H
#define MUZYKA_KONW_H

#include <stdint.h>

typedef struct konw konw_t;

/* katalog wyjsciowy musi istniec; rate 11025 albo 22050; max_utworow 0 = wszystkie.
   NULL = brak MUSIC.LBX albo pamieci (powod w bledzie). */
/* synth: 0 prosty syntezator, 1 AdLib (OPL2, barwy z FAT.AD w katalogu biezacym) */
konw_t * Konw_Start(const char * katalog, long rate, int synth, int max_utworow, char * blad, int cap);

/* kawalek pracy (ok. 0,1-0,3 s na 68030). 1 = jest dalej, 0 = koniec, -1 = blad.
   status: tekst dla gracza, po angielsku. */
int Konw_Krok(konw_t * k, char * status, int cap);

/* AdLib na zywo (gra, music=3): utwor XMIDI z LBX (caly wpis), FAT.AD z katalogu biezacego */
konw_t * Konw_Na_Zywo(long rate, const uint8_t * we, uint32_t dl, char * blad, int cap);
/* max probek 8 bit ze znakiem; mniej niz max = koniec utworu bez petli */
int Konw_Graj(konw_t * k, signed char * dst, int max);

int Konw_Zrobione(konw_t * k);   /* ile plikow zapisano */
int Konw_Ile(konw_t * k);        /* ile utworow w kolejce */
void Konw_Koniec(konw_t * k);    /* sprzata; przerwany plik jest kasowany */

#endif
