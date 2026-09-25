/*
 * muzyka-pc.c - remom-music(.exe): ten sam konwerter muzyki co w remom-prefs
 * (native/remom/muzyka_konw.c), jako program dla PC (Windows/Linux). Dla
 * graczy, ktorzy wola przekonwertowac muzyke szybko na PC i skopiowac
 * katalog muzyka na Amige (2026-09-24: "ludzie sie skarza ze nie ma
 * konwertera muzyki na pc").
 *
 *   remom-music [11|22] [katalog z plikami LBX]
 * Wynik: <katalog>/muzyka/*.wav. Tekst dla gracza po angielsku.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir_(d) _mkdir(d)
#define chdir_(d) _chdir(d)
#else
#include <sys/stat.h>
#include <unistd.h>
#define mkdir_(d) mkdir(d, 0755)
#define chdir_(d) chdir(d)
#endif
#include "muzyka_konw.h"

int main(int argc, char ** argv)
{
    char s[160];
    konw_t * k;
    long rate = 11025;
    int synth = 0;
    int r, ostatni = -1, i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "22") == 0) rate = 22050;
        else if (strcmp(argv[i], "11") == 0) rate = 11025;
        else if (strcmp(argv[i], "adlib") == 0) synth = 1;
        else if (chdir_(argv[i]) != 0) {
            printf("remom-music: cannot open folder %s\n", argv[i]);
            return 1;
        }
    }
    printf("Master of Magic music converter for the Amiga port\n"
           "Usage: remom-music [11|22] [adlib] [folder with MUSIC.LBX]\n"
           "  adlib = AdLib sound with the game's own FM instruments (needs FAT.AD)\n\n");
    mkdir_("muzyka");
    k = Konw_Start("muzyka", rate, synth, 0, s, (int)sizeof s);
    if (k == NULL) { printf("%s\n", s); return 1; }
    printf("Converting %d tracks at %ld Hz (%s) into the muzyka folder...\n", Konw_Ile(k), rate,
           synth ? "AdLib" : "simple synth");
    while ((r = Konw_Krok(k, s, (int)sizeof s)) > 0) {
        if (Konw_Zrobione(k) != ostatni) { ostatni = Konw_Zrobione(k); printf("\r%s   ", s); fflush(stdout); }
    }
    printf("\n%s\nCopy the muzyka folder next to remom on the Amiga.\n", s);
    Konw_Koniec(k);
    return r < 0;
}
