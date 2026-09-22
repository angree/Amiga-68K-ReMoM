/*
 * host_klatki.c - WYROCZNIA OBRAZU: HeMoM na PC zapisuje kazda zmieniona
 * klatke do HOST_KLATKI/klatka-NNNNN.ppm.
 *
 * Tylko build hostowy (build/build-host.sh podpina to pod pusty
 * Platform_Video_Update() z platform/headless). Silnik rysuje te same strony
 * co na Amidze; zrzut z Amigi (autoinput "snap", amiga_Video.c) porownuje sie
 * z najblizsza klatka z PC (winuae/harness/porownaj-klatki.py) - roznica
 * pikseli to usterka rysowania portu albo wyrocznia z innego momentu gry.
 *
 * Wlaczane zmienna HOST_KLATKI=<katalog>. Klatka identyczna z poprzednia
 * (piksele + paleta) nie jest zapisywana. Najwyzej 20000 plikow.
 */

#include "Platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void Host_Klatka(void)
{
    static const char * katalog = NULL;
    static int sprawdzone = 0;
    static unsigned long ostatni = 0;
    static long numer = 0;
    const uint8_t * src;
    unsigned long h = 2166136261UL;
    unsigned char wiersz[320 * 3];
    char plik[512];
    FILE * f;
    int x;
    int y;
    int i;

    if(!sprawdzone)
    {
        katalog = getenv("HOST_KLATKI");
        sprawdzone = 1;
    }
    if(katalog == NULL || numer >= 20000)
    {
        return;
    }
    src = video_page_buffer[draw_page_num];
    if(src == NULL)
    {
        return;
    }
    for(i = 0; i < 64000; i++)
    {
        h = (h ^ src[i]) * 16777619UL;
    }
    for(i = 0; i < 256; i++)
    {
        h = (h ^ platform_palette_buffer[i].r) * 16777619UL;
        h = (h ^ platform_palette_buffer[i].g) * 16777619UL;
        h = (h ^ platform_palette_buffer[i].b) * 16777619UL;
    }
    h &= 0xFFFFFFFFUL;
    if(numer > 0 && h == ostatni)
    {
        return;
    }
    ostatni = h;

    snprintf(plik, sizeof(plik), "%s/klatka-%05ld.ppm", katalog, numer);
    f = fopen(plik, "wb");
    if(f == NULL)
    {
        return;
    }
    fprintf(f, "P6\n320 200\n255\n");
    for(y = 0; y < 200; y++)
    {
        for(x = 0; x < 320; x++)
        {
            uint8_t c = src[y * 320 + x];
            wiersz[x * 3 + 0] = platform_palette_buffer[c].r;
            wiersz[x * 3 + 1] = platform_palette_buffer[c].g;
            wiersz[x * 3 + 2] = platform_palette_buffer[c].b;
        }
        fwrite(wiersz, 1, sizeof(wiersz), f);
    }
    fclose(f);
    numer++;
}
