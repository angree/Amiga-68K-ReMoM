/*
 * amiga_Video.c - paleta i wyswietlanie klatki (Platform.h: "Video").
 *
 * Silnik rysuje do 8-bitowych stron video_page_buffer[] 320x200; widoczna jest
 * strona draw_page_num (Page_Flip() zamienia numer i od razu wola
 * Platform_Video_Update()). Tu klatka trafia do bufora chunky z
 * native/amiga_gfx.c i dalej na ekran (AGA: c2p Kalmsa, RTG: memcpy).
 *
 * OSZCZEDNOSC: bufor chunky jest zawsze kopia tego, co widac. Zamiast
 * przepisywac i konwertowac cala klatke, porownujemy wiersz po wierszu
 * (memcmp), w zmienionym wierszu szukamy pierwszego i ostatniego rozniacego
 * sie bajtu, przepisujemy tylko ten odcinek i wypychamy jeden prostokat
 * obejmujacy wszystkie zmiany. Silnik wola Platform_Video_Update() takze
 * wtedy, gdy nic sie nie zmienilo - wtedy koszt to samo porownanie, bez c2p.
 *
 * PALETA: MoX trzyma 6-bitowa palete (0..63) w current_palette[];
 * platform_palette_buffer[] ma wartosci 0..255 (<<2, jak sdl2/headless -
 * zostawione bez zmian, bo Platform_Get_Palette_Color oddaje je silnikowi).
 * Fonts.c pisze platform_palette_buffer[] BEZPOSREDNIO i woła tylko
 * Platform_Video_Update() - dlatego paleta ekranu jest synchronizowana
 * z bufora przy kazdej klatce, ale tylko w zakresie wpisow, ktore sie
 * zmienily (LoadRGB32 na AGA przebudowuje copperliste - nie za darmo).
 * Na ekran idzie v | (v >> 6), zeby 63<<2 = 252 dawalo pelne 255.
 */

#include "Platform.h"
#include "Platform_Capture.h"
#include "Platform_Perf.h"

#include "amiga_PFL.h"

/* amiga_gfx.h deklaruje strukture AmigaGfxEvent, ktora wypelnia
   amiga_gfx.c skompilowany NATYWNIE - musi byc widziana w kolejnosci
   natywnej, nie little-endian z amiga_le.h. */
#ifdef AMIGA_LE_H
#pragma scalar_storage_order default
#endif
#include "amiga_gfx.h"
#ifdef AMIGA_LE_H
#pragma scalar_storage_order little-endian
#endif

#include <stdlib.h>  /* getenv */
#include <string.h>

/* kopia palety, ktora NAPRAWDE siedzi w rejestrach (wartosci 0..255 jak
   w platform_palette_buffer, przed rozszerzeniem v|(v>>6)) */
static uint8_t amiga_shadow_rgb[256 * 3];
static int amiga_shadow_valid = 0;

static int amiga_full_present = 1;

static uint8_t Amiga_Expand8(uint8_t v)
{
    return (uint8_t)(v | (v >> 6));
}

void Amiga_PFL_Force_Full_Present(void)
{
    amiga_full_present = 1;
    amiga_shadow_valid = 0;
}



/* MoO2  Refresh_Palett() |-> Store_Palette_Block_() - jak sdl2/headless */
void Platform_Palette_Update(void)
{
    int itr;

    for(itr = 0; itr < 256; itr++)
    {
        platform_palette_buffer[itr].r = (*(current_palette + (itr * 3) + 0) << 2);
        platform_palette_buffer[itr].g = (*(current_palette + (itr * 3) + 1) << 2);
        platform_palette_buffer[itr].b = (*(current_palette + (itr * 3) + 2) << 2);
        platform_palette_buffer[itr].a = 0xFF;
    }
}

void Platform_Get_Palette_Color(uint8_t index, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = platform_palette_buffer[index].r;
    *g = platform_palette_buffer[index].g;
    *b = platform_palette_buffer[index].b;
}

/* Cycle_Palette_Color() - kolor zmienia sie od razu w rejestrze, bez klatki. */
void Platform_Set_Palette_Color(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    platform_palette_buffer[index].r = r;
    platform_palette_buffer[index].g = g;
    platform_palette_buffer[index].b = b;
    platform_palette_buffer[index].a = 255;

    if(amiga_pfl_display_open && amiga_shadow_valid)
    {
        uint8_t rgb[3];
        rgb[0] = Amiga_Expand8(r);
        rgb[1] = Amiga_Expand8(g);
        rgb[2] = Amiga_Expand8(b);
        amigagfx_set_palette(rgb, index, 1);
        amiga_shadow_rgb[index * 3 + 0] = r;
        amiga_shadow_rgb[index * 3 + 1] = g;
        amiga_shadow_rgb[index * 3 + 2] = b;
    }
}

/* Wpisy platform_palette_buffer[] rozne od rejestrow -> jeden LoadRGB32
   na zakres od pierwszego do ostatniego zmienionego. Pierwsze wywolanie
   (i po Force_Full) wysyla wszystkie 256 - okno na Workbenchu negocjuje
   piora tylko przy pelnej aktualizacji 0..255. */
static void Amiga_Sync_Palette(void)
{
    uint8_t rgb[256 * 3];
    int lo = -1;
    int hi = -1;
    int itr;

    if(!amiga_shadow_valid)
    {
        lo = 0;
        hi = 255;
    }
    else
    {
        for(itr = 0; itr < 256; itr++)
        {
            const uint8_t * s = amiga_shadow_rgb + itr * 3;
            if((s[0] != platform_palette_buffer[itr].r) ||
               (s[1] != platform_palette_buffer[itr].g) ||
               (s[2] != platform_palette_buffer[itr].b))
            {
                if(lo < 0) { lo = itr; }
                hi = itr;
            }
        }
        if(lo < 0)
        {
            return;
        }
    }

    for(itr = lo; itr <= hi; itr++)
    {
        uint8_t * s = amiga_shadow_rgb + itr * 3;
        uint8_t * d = rgb + (itr - lo) * 3;
        s[0] = platform_palette_buffer[itr].r;
        s[1] = platform_palette_buffer[itr].g;
        s[2] = platform_palette_buffer[itr].b;
        d[0] = Amiga_Expand8(s[0]);
        d[1] = Amiga_Expand8(s[1]);
        d[2] = Amiga_Expand8(s[2]);
    }
    amigagfx_set_palette(rgb, lo, hi - lo + 1);
    amiga_shadow_valid = 1;
}

/* wymiary obszaru, ktory da sie przepisac: min(ekran gry, obszar ekranu) */
static void Amiga_Present_Size(int * w, int * h)
{
    int sw = (screen_pixel_width  > 0) ? screen_pixel_width  : PLATFORM_SCREEN_WIDTH;
    int sh = (screen_pixel_height > 0) ? screen_pixel_height : PLATFORM_SCREEN_HEIGHT;
    int gw = amigagfx_game_width();
    int gh = amigagfx_game_height();
    *w = (sw < gw) ? sw : gw;
    *h = (sh < gh) ? sh : gh;
}

void Amiga_PFL_Present_Rect(int x, int y, int w, int h)
{
    const uint8_t * src_page;
    unsigned char * chunky;
    int pitch;
    int src_pitch;
    int max_w;
    int max_h;
    int row;

    if(!amiga_pfl_display_open)
    {
        return;
    }
    src_page = video_page_buffer[draw_page_num];
    chunky = amigagfx_chunky();
    if(src_page == NULL || chunky == NULL)
    {
        return;
    }
    Amiga_Present_Size(&max_w, &max_h);
    if(x < 0) { w += x; x = 0; }
    if(y < 0) { h += y; y = 0; }
    if(x + w > max_w) { w = max_w - x; }
    if(y + h > max_h) { h = max_h - y; }
    if(w <= 0 || h <= 0)
    {
        return;
    }
    pitch = amigagfx_pitch();
    src_pitch = (screen_pixel_width > 0) ? screen_pixel_width : PLATFORM_SCREEN_WIDTH;
    for(row = y; row < y + h; row++)
    {
        memcpy(chunky + (long)row * pitch + x, src_page + (long)row * src_pitch + x, (size_t)w);
    }
    amigagfx_blit(x, y, w, h);
}

/* Work:<nazwa>.ppm - klatka z palety, ktora idzie na ekran */
char amiga_snap_request[40];

static void Amiga_Dump_Frame(const uint8_t * src, const char * nazwa)
{
    char plik[48];
    unsigned char wiersz[320 * 3];
    FILE * f;
    int y;
    int x;

    snprintf(plik, sizeof(plik), "%s.ppm", nazwa);
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
            uint8_t c = src[(long)y * 320 + x];
            wiersz[x * 3 + 0] = platform_palette_buffer[c].r;
            wiersz[x * 3 + 1] = platform_palette_buffer[c].g;
            wiersz[x * 3 + 2] = platform_palette_buffer[c].b;
        }
        fwrite(wiersz, 1, sizeof(wiersz), f);
    }
    fclose(f);
    printf("[amiga] zapisano %s\n", plik);
    fflush(stdout);
}

/* Wiersz rowny? Porownanie dlugimi slowami (68020+ czyta 32 bity spod
   dowolnego adresu), reszta bajtami. Profil 2026-09-17: bajtowe memcmp
   calej klatki bylo najwiekszym kosztem ekranu mapy (17% probek). */
static int Amiga_Wiersz_Rowny(const uint8_t * a, const uint8_t * b, int w)
{
    const uint32_t * la = (const uint32_t *)a;
    const uint32_t * lb = (const uint32_t *)b;
    int n = w >> 2;
    while(n-- > 0)
    {
        if(*la++ != *lb++)
        {
            return 0;
        }
    }
    a = (const uint8_t *)la;
    b = (const uint8_t *)lb;
    n = w & 3;
    while(n-- > 0)
    {
        if(*a++ != *b++)
        {
            return 0;
        }
    }
    return 1;
}

/*
    ...just update what the user sees on the screen
*/
void Platform_Video_Update(void)
{
    const uint8_t * src;
    unsigned char * dst;
    int pitch;
    int src_pitch;
    int w;
    int h;
    int y;
    int x0;
    int x1;
    int y0;
    int y1;

    if(Platform_Capture_Active())
    {
        Platform_Capture_Video_Frame(video_page_buffer[draw_page_num], screen_pixel_width, screen_pixel_height, platform_palette_buffer);
    }

    if(!amiga_pfl_display_open)
    {
        Perf_Live_Note_Present();
        return;
    }

    src = video_page_buffer[draw_page_num];
    dst = amigagfx_chunky();
    if(src == NULL || dst == NULL)
    {
        return;
    }

    Amiga_Sync_Palette();

    /* Workbench w tle (2026-09-17): log, gdy zmienia sie to, czy ekran gry
       jest z przodu - dowod od srodka, bo zrzut okna WinUAE (D3D11) bywa
       nieaktualny. */
    {
        static int amiga_przod = -2;
        int przod = amigagfx_screen_in_front();
        if(przod != amiga_przod)
        {
            printf("[amiga] ekran gry z przodu: %s (t=%lu ms)\n",
                   (przod == 1) ? "tak" : ((przod == 0) ? "NIE" : "brak ekranu"),
                   (unsigned long)Platform_Get_Millies());
            fflush(stdout);
            amiga_przod = przod;
        }
    }

    /* DIAGNOSTYKA (2026-09-17): ekran AGA otwiera sie i dostaje blity, a okno
       WinUAE pozostaje czarne. Czy klatka ma tresc i czy paleta nie jest
       czarna? Kilka pierwszych klatek i potem co 100. */
    {
        static long amiga_diag_n = 0;
        /* Statystyki i zrzuty tylko na zyczenie: Set REMOM_KLATKI 1
           (zmienna lokalna - getenv() z libnix nie widzi SetEnv) */
        static int amiga_diag_wl = -1;
        if(amiga_diag_wl < 0)
        {
            amiga_diag_wl = (getenv("REMOM_KLATKI") != NULL) ? 1 : 0;
        }
        if(amiga_diag_wl == 0)
        {
            amiga_diag_n = -1;
        }
        else if((amiga_diag_n < 4) || ((amiga_diag_n % 100) == 0 && amiga_diag_n <= 1000))
        {
            long nz = 0;
            long i;
            int p;
            long jasnych = 0;
            for(i = 0; i < 64000L; i++) { if(src[i] != 0) { nz++; } }
            for(p = 0; p < 256; p++)
            {
                if((platform_palette_buffer[p].r | platform_palette_buffer[p].g | platform_palette_buffer[p].b) != 0) { jasnych++; }
            }
            printf("[amiga] klatka %ld: niezerowych pikseli %ld/64000, niezerowych kolorow palety %ld/256,"
                   " kolor[%d]=%d,%d,%d strona=%d\n",
                   amiga_diag_n, nz, jasnych, (int)src[32000],
                   platform_palette_buffer[src[32000]].r, platform_palette_buffer[src[32000]].g,
                   platform_palette_buffer[src[32000]].b, (int)draw_page_num);
            fflush(stdout);
        }
        /* Zrzut klatki z wnetrza goscia (Work:klatka-NNNN.ppm): dokladnie
           piksele i paleta, ktore ida do c2p. Okno WinUAE na tym hoscie bywa
           czarne niezaleznie od programu (2026-09-17), wiec obraz gry
           dowodzimy od srodka. Klatki 60, 300 i 900. */
        if(amiga_diag_n == 60 || amiga_diag_n == 300 || amiga_diag_n == 900)
        {
            char nazwa[32];
            snprintf(nazwa, sizeof(nazwa), "klatka-%04ld", amiga_diag_n);
            Amiga_Dump_Frame(src, nazwa);
        }
        amiga_diag_n++;
    }
    /* "snap NAZWA" z Work:autoinput.txt (amiga_Auto.c) */
    if(amiga_snap_request[0] != '\0')
    {
        char pelny[48];
        Amiga_Dump_Frame(src, amiga_snap_request);
        /* drugi plik: CALY ekran Amigi z paskiem tytulowym (amiga_gfx.c) -
           zrzut okna WinUAE na hoscie nie jest wiarygodny (patrz komentarz
           przy amigagfx_dump_screen) */
        snprintf(pelny, sizeof(pelny), "%s-ekran.ppm", amiga_snap_request);
        if(amigagfx_dump_screen(pelny) == 0)
        {
            printf("[amiga] zapisano %s\n", pelny);
            fflush(stdout);
        }
        amiga_snap_request[0] = '\0';
    }

    Amiga_Present_Size(&w, &h);
    pitch = amigagfx_pitch();
    src_pitch = (screen_pixel_width > 0) ? screen_pixel_width : PLATFORM_SCREEN_WIDTH;

    x0 = w;
    x1 = -1;
    y0 = h;
    y1 = -1;

    for(y = 0; y < h; y++)
    {
        const uint8_t * s = src + (long)y * src_pitch;
        unsigned char * d = dst + (long)y * pitch;
        int l;
        int r;

        if(amiga_full_present)
        {
            l = 0;
            r = w - 1;
        }
        else
        {
            if(Amiga_Wiersz_Rowny(d, s, w))
            {
                continue;
            }
            l = 0;
            while(d[l] == s[l]) { l++; }
            r = w - 1;
            while(d[r] == s[r]) { r--; }
        }
        memcpy(d + l, s + l, (size_t)(r - l + 1));
        if(l < x0) { x0 = l; }
        if(r > x1) { x1 = r; }
        if(y < y0) { y0 = y; }
        y1 = y;
    }
    amiga_full_present = 0;

    if(y1 >= 0)
    {
        amigagfx_blit(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    }

    Perf_Live_Note_Present();
}
