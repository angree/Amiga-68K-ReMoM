/* zjadacz - test "maszyna z mala iloscia FastRAM" bez zmiany konfiguracji
 * emulatora (0.4.4, zgloszenie z EAB: gra nie dziala z 8 MB FastRAM).
 *
 *   Run >NIL: Work:zjadacz 7300    - zajmuje FastRAM tak, by zostalo 7300 KB
 *   ... gra ...
 *   Echo >Work:zjadacz.stop "x"    - zjadacz zwalnia pamiec i konczy sie
 *
 * Wynik (ile zostalo) do Work:zjadacz.log. */

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_KAWALKOW 4096

static void * kawalki[MAX_KAWALKOW];
static unsigned long rozm[MAX_KAWALKOW];

int main(int argc, char ** argv)
{
    unsigned long zostaw = (argc > 1) ? strtoul(argv[1], NULL, 10) * 1024UL : 7300UL * 1024UL;
    unsigned long przed = AvailMem(MEMF_FAST);
    int n = 0, i;
    FILE * f;
    BPTR l;

    /* najwiekszymi blokami, az zostanie dokladnie 'zostaw' */
    while(n < MAX_KAWALKOW && AvailMem(MEMF_FAST) > zostaw + 64UL)
    {
        unsigned long chce = AvailMem(MEMF_FAST) - zostaw;
        unsigned long duzy = AvailMem(MEMF_FAST | MEMF_LARGEST);
        unsigned long r = (chce < duzy) ? chce : duzy;
        r &= ~7UL;
        if(r < 64UL) { break; }
        kawalki[n] = AllocMem(r, MEMF_FAST);
        if(kawalki[n] == NULL) { break; }
        rozm[n] = r;
        n++;
    }
    f = fopen("Work:zjadacz.log", "w");
    if(f != NULL)
    {
        fprintf(f, "zjadacz: FastRAM wolne przed %lu KB, po %lu KB (najwiekszy blok %lu KB), Chip wolne %lu KB\n",
                przed / 1024UL, AvailMem(MEMF_FAST) / 1024UL, AvailMem(MEMF_FAST | MEMF_LARGEST) / 1024UL,
                AvailMem(MEMF_CHIP) / 1024UL);
        fclose(f);
    }
    for(;;)
    {
        l = Lock((CONST_STRPTR)"Work:zjadacz.stop", ACCESS_READ);
        if(l != 0) { UnLock(l); break; }
        Delay(25);
    }
    for(i = 0; i < n; i++)
    {
        FreeMem(kawalki[i], rozm[i]);
    }
    return 0;
}
