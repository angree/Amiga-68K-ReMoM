/* adlibtest - koszt syntezy AdLib live (muzyka_konw.c) na Amidze.
 * Dla kazdego utworu z MUSIC.LBX liczy 10 s muzyki porcjami po 4096 probek
 * (jak gra) i wypisuje czas: procent czasu rzeczywistego. Powyzej 100% gra
 * nie moze nadazyc nawet bez niczego innego = przerwy w muzyce.
 *   adlibtest [hz] [sekund]   -> Work:adlibtest.txt */
#include <proto/dos.h>
#include <dos/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "muzyka_konw.h"

static long ms_teraz(void)
{
    struct DateStamp d;
    DateStamp(&d);
    return (d.ds_Days * 86400L + d.ds_Minute * 60L) * 1000L + d.ds_Tick * 20L;
}
static uint32_t le32(const uint8_t * p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int main(int argc, char ** argv)
{
    long hz = argc > 1 ? atol(argv[1]) : 22050;
    int sek = argc > 2 ? atoi(argv[2]) : 10;
    FILE * f = fopen("MUSIC.LBX", "rb"), * wy = fopen("Work:adlibtest.txt", "w");
    uint8_t nag[8], * off;
    signed char * buf = malloc(4096);
    int n, i, najgorszy = 0;
    if (f == NULL || wy == NULL || buf == NULL) return 20;
    fread(nag, 1, 8, f); n = nag[0] | (nag[1] << 8);
    off = malloc((size_t)(n + 1) * 4); fread(off, 4, (size_t)(n + 1), f);
    fprintf(wy, "adlibtest: %ld Hz, %d s na utwor, porcje 4096\n", hz, sek);
    for (i = 0; i < n; i++) {
        uint32_t o = le32(off + i * 4), e = le32(off + i * 4 + 4), dl = e - o;
        uint8_t * we; char blad[64]; konw_t * k; long t0, t1, zrob = 0, cel = hz * sek, proc;
        if (e <= o + 16) continue;
        we = malloc(dl); fseek(f, (long)o, SEEK_SET); fread(we, 1, dl, f);
        if (we[0] != 0xAF || we[1] != 0xDE) { free(we); continue; }
        k = Konw_Na_Zywo(hz, we, dl, blad, 64);
        if (k == NULL) { fprintf(wy, "%3d: %s\n", i, blad); free(we); continue; }
        t0 = ms_teraz();
        while (zrob < cel) { int r = Konw_Graj(k, buf, 4096); if (r <= 0) break; zrob += r; }
        t1 = ms_teraz();
        proc = zrob > 0 ? ((t1 - t0) * hz / 10) / zrob : 0;   /* procent czasu rzeczywistego */
        if (proc > najgorszy) najgorszy = (int)proc;
        fprintf(wy, "%3d: %5ld probek w %6ld ms = %3ld%% czasu rzeczywistego\n", i, zrob, t1 - t0, proc);
        fflush(wy);
        Konw_Koniec(k); free(we);
    }
    fprintf(wy, "najgorszy utwor: %d%%\n", najgorszy);
    fclose(wy); fclose(f);
    return 0;
}
