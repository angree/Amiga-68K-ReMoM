/* konw-koniec-test.c - test na PC (0.5.2): czy Konw_Graj (AdLib na zywo)
 * konczy utwor bez petli, tzn. zwraca w koncu 0.
 * Przed poprawka po wygasnieciu ogona kazde wywolanie oddawalo jeszcze jeden
 * maly kawalek - strumien nigdy sie nie konczyl (trzaski po melodii czaru).
 *
 * Uzycie: konw-koniec-test MUSIC.LBX
 * Dla kazdego wpisu: petla tak/nie, ile wywolan Konw_Graj(2048) do zera
 * (albo "NIE KONCZY" po limicie). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "muzyka_konw.h"

static unsigned le16(const unsigned char * p) { return p[0] | (p[1] << 8); }
static unsigned long le32(const unsigned char * p) { return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }

int main(int argc, char ** argv)
{
    FILE * f;
    unsigned char * d;
    long dl;
    unsigned ile, i;
    int zle = 0;
    static signed char buf[2048];

    if (argc < 2) { fprintf(stderr, "uzycie: %s MUSIC.LBX\n", argv[0]); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END); dl = ftell(f); fseek(f, 0, SEEK_SET);
    d = malloc((size_t)dl);
    if (fread(d, 1, (size_t)dl, f) != (size_t)dl) { return 2; }
    fclose(f);
    ile = le16(d);
    for (i = 0; i < ile; i++) {
        unsigned long od = le32(d + 8 + 4 * i), do_ = le32(d + 12 + 4 * i);
        char blad[64];
        konw_t * k;
        long wyw = 0, probki = 0;
        int n;
        if (do_ <= od || do_ > (unsigned long)dl) continue;
        k = Konw_Na_Zywo(11025, d + od, (uint32_t)(do_ - od), blad, (int)sizeof blad);
        if (k == NULL) continue;
        if (Konw_Petla(k)) { printf("wpis %2u: petla\n", i); Konw_Koniec(k); continue; }
        while ((n = Konw_Graj(k, buf, (int)sizeof buf)) > 0 && wyw < 20000) { wyw++; probki += n; }
        if (n > 0) {
            printf("wpis %2u: bez petli - NIE KONCZY (%ld wywolan, ostatnie po %d probek)\n", i, wyw, n);
            zle++;
        } else {
            printf("wpis %2u: bez petli - koniec po %ld wywolaniach, %.1f s\n", i, wyw, probki / 11025.0);
        }
        Konw_Koniec(k);
    }
    printf("%s\n", zle ? "WYNIK: SA UTWORY, KTORE SIE NIE KONCZA" : "WYNIK: wszystkie utwory bez petli sie koncza");
    return zle ? 1 : 0;
}
