/*
 * amiga_Opcje.c - ustawienia Amigi i ekran "Amiga Options" w menu glownym.
 *
 * USTAWIENIA (Work:amiga.cfg, tekst "klucz=wartosc", zapisywane przy
 * wyjsciu z ekranu opcji):
 *   cursor=0|1     1 = wskaznik systemowy (Intuition) zamiast kursora gry
 *   bar=0|1        pasek tytulowy ekranu z gadzetem glebi (jak OpenXcom)
 *   video=0|1|2    tryb obrazu: 0 jak maszyna, 1 PAL, 2 NTSC (jak OpenXcom:
 *                  amigagfx_set_video_mode przed otwarciem ekranu)
 *   gfx=0|1|2|3    grafika: 0 auto (= AGA), 1 AGA, 2 RTG, 3 okno na Workbenchu
 *   music=0|1      muzyka
 *   fps=0|1        kl./s i najdluzsza klatka na pasku ekranu
 * Ten sam plik edytuje program remom-prefs (native/remom/remom-prefs.c) -
 * okno na Workbenchu, przed uruchomieniem gry.
 * Zmienne lokalne powloki nadpisuja plik (testy): REMOM_PASEK 0|1,
 * REMOM_KURSOR 0|1, REMOM_WIDEO auto|pal|ntsc.
 *
 * NAPISY MENU: pozycje menu glownego to bitmapy z VORTEX.LBX gracza, bez
 * liter potrzebnych do "Quit To AmigaOS" i "Amiga Options". Font 4 gry ma
 * ten sam kroj (porownanie 2026-09-17), wiec napis renderujemy fontem 4,
 * a kolory przepisujemy wierszami z oryginalnej bitmapy "Quit To DOS"
 * (klatka 0 = podswietlona, 1 = zwykla), a obrys - jej najciemniejszym
 * kolorem. Zadnej grafiki gry w repozytorium - wszystko z danych gracza.
 *
 * Tekst na ekranie po angielsku, komentarze po polsku.
 */

#include "../ext/stu_compat.h"
#include "../MoX/src/MOX_TYPE.h"
#include "../MoX/src/MOX_DEF.h"
#include "../MoX/src/MOX_BASE.h"
#include "../MoX/src/Mouse.h"
#include "../MoX/src/Timer.h"
#include "../MoX/src/Video.h"
#include "../MoX/src/FLIC_Draw.h"
#include "../MoX/src/Fields.h"
#include "../MoX/src/Input.h"
#include "../MoX/src/Fonts.h"
#include "../MoX/src/Graphics.h"

#include "Platform.h"
#include "../STU/src/STU_LOG.h"

#include "amiga_PFL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern SAMB_ptr mainmenu_top;
extern SAMB_ptr mainmenu_bot;
extern SAMB_ptr mainmenu_q;
extern uint8_t * current_palette;
extern uint16_t outline_color;   /* MoX/src/Fonts.c */

int amiga_opt_kursor = 0;
int amiga_opt_pasek = 1;
int amiga_opt_wideo = 0;
int amiga_opt_grafika = 0;
int amiga_opt_muzyka = 1;
static char amiga_opt_reszta[256];   /* linie amiga.cfg, ktorych gra nie zna */
int amiga_opt_fps = 1;
static int amiga_opt_wczytane = 0;

#define AMIGA_OPT_PLIK "amiga.cfg"

void Amiga_Opcje_Wczytaj(void)
{
    FILE * f;
    char linia[64];
    const char * e;
    int v;

    if(amiga_opt_wczytane)
    {
        return;
    }
    amiga_opt_wczytane = 1;

    f = fopen(AMIGA_OPT_PLIK, "r");
    if(f != NULL)
    {
        while(fgets(linia, sizeof(linia), f) != NULL)
        {
            if(sscanf(linia, "cursor=%d", &v) == 1)     { amiga_opt_kursor = (v != 0); }
            else if(sscanf(linia, "bar=%d", &v) == 1)   { amiga_opt_pasek = (v != 0); }
            else if(sscanf(linia, "video=%d", &v) == 1) { if(v >= 0 && v <= 2) { amiga_opt_wideo = v; } }
            else if(sscanf(linia, "gfx=%d", &v) == 1)   { if(v >= 0 && v <= 3) { amiga_opt_grafika = v; } }
            else if(sscanf(linia, "music=%d", &v) == 1) { if(v >= 0 && v <= 2) { amiga_opt_muzyka = v; } }  /* 2 = MIDI (camd) */
            else if(sscanf(linia, "fps=%d", &v) == 1)   { amiga_opt_fps = (v != 0); }
            else if(strlen(amiga_opt_reszta) + strlen(linia) < sizeof(amiga_opt_reszta)) { strcat(amiga_opt_reszta, linia); }  /* klucze remom-prefs (musicrate, synth) - oddawane przy zapisie */
        }
        fclose(f);
    }

    e = getenv("REMOM_PASEK");
    if(e != NULL) { amiga_opt_pasek = (e[0] != '0'); }
    e = getenv("REMOM_KURSOR");
    if(e != NULL) { amiga_opt_kursor = (e[0] != '0'); }
    e = getenv("REMOM_WIDEO");
    if(e != NULL)
    {
        if(e[0] == 'p' || e[0] == 'P')      { amiga_opt_wideo = 1; }
        else if(e[0] == 'n' || e[0] == 'N') { amiga_opt_wideo = 2; }
        else                                { amiga_opt_wideo = 0; }
    }
    printf("[amiga] opcje: cursor=%d bar=%d video=%d gfx=%d music=%d fps=%d\n", amiga_opt_kursor, amiga_opt_pasek,
           amiga_opt_wideo, amiga_opt_grafika, amiga_opt_muzyka, amiga_opt_fps);
    fflush(stdout);
}

static void Amiga_Opcje_Zapisz(void)
{
    FILE * f = fopen(AMIGA_OPT_PLIK, "w");
    if(f == NULL)
    {
        return;
    }
    fprintf(f, "cursor=%d\nbar=%d\nvideo=%d\ngfx=%d\nmusic=%d\nfps=%d\n", amiga_opt_kursor, amiga_opt_pasek,
            amiga_opt_wideo, amiga_opt_grafika, amiga_opt_muzyka, amiga_opt_fps);
    fputs(amiga_opt_reszta, f);
    fclose(f);
}


/* ------------------------------------------------------------------------ */
/*  Napisy w stylu bitmap menu                                               */
/* ------------------------------------------------------------------------ */

#define WZ_WYS_MAX 32

static int wz_gotowy = 0;
static int wz_wys = 0;                       /* wysokosc bitmapy wzorca */
static int wz_gora[2];                       /* pierwszy wiersz liter */
static int wz_srodek = 52;                   /* srodek napisu we wzorcu (x) */
static uint8_t wz_kolor[2][WZ_WYS_MAX];      /* kolor liter w wierszu */
static uint8_t wz_obrys[2];                  /* kolor obrysu */

static int Amiga_Jasnosc(uint8_t c)
{
    const uint8_t * p = current_palette + (int)c * 3;
    return (int)p[0] + (int)p[1] + (int)p[2];
}

/* bitmapa "Create_Picture": 16 B naglowka, piksele kolumnami (x * h + y) */
#define BM_PX(bm, h, x, y) ((bm)[SZ_FLIC_HDR + (long)(x) * (h) + (y)])

static void Amiga_Wzorzec_Analizuj(void)
{
    int k;
    int w;
    int h;
    int x;
    int y;
    int c;
    uint8_t * bm;
    int xmin = 1000;
    int xmax = -1;

    wz_gotowy = 1;
    w = FLIC_GET_WIDTH(mainmenu_q);
    h = FLIC_GET_HEIGHT(mainmenu_q);
    if(w <= 0 || h <= 0 || w > 320 || h > WZ_WYS_MAX)
    {
        wz_wys = 0;
        return;
    }
    wz_wys = h;
    bm = (uint8_t *)malloc((size_t)(SZ_FLIC_HDR + w * h + 64));
    if(bm == NULL)
    {
        wz_wys = 0;
        return;
    }
    for(k = 0; k < 2; k++)
    {
        long hist[256];
        long wiersz[256];
        int najciemniejszy = -1;

        Set_Animation_Frame(mainmenu_q, k);
        Draw_Picture_To_Bitmap(mainmenu_q, bm);
        memset(hist, 0, sizeof(hist));
        for(x = 0; x < w; x++)
        {
            for(y = 0; y < h; y++)
            {
                c = BM_PX(bm, h, x, y);
                if(c != ST_TRANSPARENT)
                {
                    hist[c]++;
                    if(x < xmin) { xmin = x; }
                    if(x > xmax) { xmax = x; }
                }
            }
        }
        for(c = 1; c < 256; c++)
        {
            if(hist[c] > 0 && (najciemniejszy < 0 || Amiga_Jasnosc((uint8_t)c) < Amiga_Jasnosc((uint8_t)najciemniejszy)))
            {
                najciemniejszy = c;
            }
        }
        wz_obrys[k] = (uint8_t)((najciemniejszy < 0) ? 0 : najciemniejszy);
        wz_gora[k] = -1;
        for(y = 0; y < h; y++)
        {
            int najl = -1;
            memset(wiersz, 0, sizeof(wiersz));
            for(x = 0; x < w; x++)
            {
                c = BM_PX(bm, h, x, y);
                if(c != ST_TRANSPARENT && c != wz_obrys[k])
                {
                    wiersz[c]++;
                }
            }
            for(c = 1; c < 256; c++)
            {
                if(wiersz[c] > 0 && (najl < 0 || wiersz[c] > wiersz[najl]))
                {
                    najl = c;
                }
            }
            if(najl >= 0)
            {
                if(wz_gora[k] < 0) { wz_gora[k] = y; }
                wz_kolor[k][y] = (uint8_t)najl;
            }
            else
            {
                wz_kolor[k][y] = (y > 0) ? wz_kolor[k][y - 1] : 0;
            }
        }
        if(wz_gora[k] < 0)
        {
            wz_gora[k] = 0;
        }
        /* wiersze nad pierwszym wierszem liter - kolor pierwszego */
        for(y = 0; y < wz_gora[k]; y++)
        {
            wz_kolor[k][y] = wz_kolor[k][wz_gora[k]];
        }
    }
    free(bm);
    if(xmax >= xmin)
    {
        wz_srodek = (xmin + xmax) / 2;
    }
    printf("[amiga] menu: wzorzec napisow %dx%d, gora %d/%d, obrys %d/%d, srodek %d\n",
           w, h, wz_gora[0], wz_gora[1], wz_obrys[0], wz_obrys[1], wz_srodek);
    fflush(stdout);
}

#define NAPISY_MAX 12

/* pamiec programu, nie dane z pliku - kolejnosc natywna (amiga_le.h) */
#ifdef AMIGA_LE_H
#pragma scalar_storage_order default
#endif
typedef struct
{
    char tekst[40];
    uint8_t * bm[2];
    int dx[2];     /* przesuniecie wzgledem (x, y) pozycji menu */
    int dy[2];
} amiga_napis_t;
#ifdef AMIGA_LE_H
#pragma scalar_storage_order little-endian
#endif

static amiga_napis_t amiga_napisy[NAPISY_MAX];
static int amiga_napisy_ile = 0;

static uint8_t * Amiga_Napis_Zrob(const char * tekst, int k, int * dx, int * dy)
{
    int w;
    int h;
    int x;
    int y;
    int gora = -1;
    int xmin = 1000;
    int xmax = -1;
    uint8_t * bm;
    uint8_t * maska;
    char bufor[40];

    strncpy(bufor, tekst, sizeof(bufor) - 1);
    bufor[sizeof(bufor) - 1] = '\0';

    Set_Font_Style(4, 4, 0, 0);
    /* Set_Font_Style NIE zeruje flagi cienia - maska dostawala obrys
       z poprzedniego wywolania i litery wychodzily pogrubione */
    SET_1B_OFS(font_style_data, FONT_HDR_POS_SHADOW_FLAG, e_Font_Shadow_None);
    w = Get_String_Width(bufor) + 6;
    h = wz_wys + 6;
    bm = (uint8_t *)malloc((size_t)(SZ_FLIC_HDR + w * h + 16));
    maska = (uint8_t *)malloc((size_t)(SZ_FLIC_HDR + w * h + 16));
    if(bm == NULL || maska == NULL)
    {
        free(bm);
        free(maska);
        return NULL;
    }
    /* same litery (maska wypelnienia) i litery z obrysem */
    Create_Picture((int16_t)w, (int16_t)h, maska);
    Print_To_Bitmap(2, 2, bufor, maska);
    /* cien 1 px w prawo i w dol (Shadow_Down) - jak w bitmapach menu
       (porownanie piksel w piksel z "Hall Of Fame", 2026-09-17).
       Kolor obrysu (outline_color) domyslnie 0 = przezroczysty - na czas
       rysowania kolor obrysu wzorca, potem poprzedni. */
    {
        uint16_t poprzedni = outline_color;
        outline_color = (wz_obrys[k] != 0) ? wz_obrys[k] : 1;
        Set_Font_Style_Shadow_Down(4, 4, 0, 0);
        Create_Picture((int16_t)w, (int16_t)h, bm);
        Print_To_Bitmap(2, 2, bufor, bm);
        outline_color = poprzedni;
    }

    for(y = 0; y < h && gora < 0; y++)
    {
        for(x = 0; x < w; x++)
        {
            if(BM_PX(maska, h, x, y) != ST_TRANSPARENT)
            {
                gora = y;
                break;
            }
        }
    }
    if(gora < 0)
    {
        gora = 0;
    }
    for(x = 0; x < w; x++)
    {
        for(y = 0; y < h; y++)
        {
            uint8_t * p = &BM_PX(bm, h, x, y);
            int wy;
            if(*p == ST_TRANSPARENT)
            {
                continue;
            }
            if(x < xmin) { xmin = x; }
            if(x > xmax) { xmax = x; }
            if(BM_PX(maska, h, x, y) == ST_TRANSPARENT)
            {
                *p = wz_obrys[k];
                continue;
            }
            wy = y - gora + wz_gora[k];
            if(wy < 0) { wy = 0; }
            if(wy >= wz_wys) { wy = wz_wys - 1; }
            *p = wz_kolor[k][wy];
        }
    }
    free(maska);
    *dy = wz_gora[k] - gora;
    *dx = wz_srodek - ((xmax >= xmin) ? (xmin + xmax) / 2 : w / 2);
    return bm;
}

/* Rysuje napis tak, jak stalaby bitmapa pozycji menu w (x, y).
   podswietlony: 1 = jak pozycja pod mysza. */
void Amiga_Rysuj_Napis_Menu(const char * tekst, int x, int y, int podswietlony)
{
    int i;
    int k = podswietlony ? 0 : 1;
    amiga_napis_t * n = NULL;

    if(!wz_gotowy)
    {
        Amiga_Wzorzec_Analizuj();
    }
    if(wz_wys <= 0)
    {
        return;
    }
    for(i = 0; i < amiga_napisy_ile; i++)
    {
        if(strcmp(amiga_napisy[i].tekst, tekst) == 0)
        {
            n = &amiga_napisy[i];
            break;
        }
    }
    if(n == NULL)
    {
        if(amiga_napisy_ile < NAPISY_MAX)
        {
            n = &amiga_napisy[amiga_napisy_ile++];
        }
        else
        {
            n = &amiga_napisy[NAPISY_MAX - 1];
            free(n->bm[0]);
            free(n->bm[1]);
        }
        memset(n, 0, sizeof(*n));
        strncpy(n->tekst, tekst, sizeof(n->tekst) - 1);
    }
    if(n->bm[k] == NULL)
    {
        n->bm[k] = Amiga_Napis_Zrob(tekst, k, &n->dx[k], &n->dy[k]);
        if(n->bm[k] == NULL)
        {
            return;
        }
    }
    Draw_Picture_Windowed((int16_t)(x + n->dx[k]), (int16_t)(y + n->dy[k]), n->bm[k]);
}


/* ------------------------------------------------------------------------ */
/*  Ekran "Amiga Options"                                                    */
/* ------------------------------------------------------------------------ */

#define OPC_WIERSZY 4
#define OPC_X 123
#define OPC_Y_TYTUL 117
#define OPC_Y0 135
#define OPC_Y_DONE 177

static void Amiga_Opcje_Tekst(int wiersz, char * bufor, int rozmiar)
{
    static const char * wideo[3] = { "Auto", "PAL", "NTSC" };
    switch(wiersz)
    {
        case 0: snprintf(bufor, rozmiar, "System Cursor: %s", amiga_opt_kursor ? "On" : "Off"); break;
        case 1: snprintf(bufor, rozmiar, "Screen Title Bar: %s", amiga_opt_pasek ? "On" : "Off"); break;
        case 2: snprintf(bufor, rozmiar, "Video Mode: %s", wideo[amiga_opt_wideo]); break;
        default: snprintf(bufor, rozmiar, "Done"); break;
    }
}

static int Amiga_Opcje_Y(int wiersz)
{
    return (wiersz == 3) ? OPC_Y_DONE : (OPC_Y0 + wiersz * 12);
}

static void Amiga_Opcje_Rysuj(int16_t pola[OPC_WIERSZY])
{
    int16_t pod_mysza;
    int logo;
    int i;
    char bufor[40];

    pod_mysza = Scan_Input();

    Set_Page_Off();
    Fill(SCREEN_XMIN, SCREEN_YMIN, SCREEN_XMAX, SCREEN_YMAX, ST_TRANSPARENT);
    logo = FLIC_Get_CurrentFrame(mainmenu_top);
    Set_Animation_Frame(mainmenu_top, 0);
    for(i = 0; i <= logo; i++)
    {
        FLIC_Draw(0, 0, mainmenu_top);
    }
    FLIC_Draw(0, 41, mainmenu_bot);

    Amiga_Rysuj_Napis_Menu("Amiga Options", OPC_X, OPC_Y_TYTUL, 1);
    for(i = 0; i < OPC_WIERSZY; i++)
    {
        Amiga_Opcje_Tekst(i, bufor, sizeof(bufor));
        Amiga_Rysuj_Napis_Menu(bufor, OPC_X, Amiga_Opcje_Y(i), (pod_mysza == pola[i]) ? 1 : 0);
    }
}

void Amiga_PFL_Przeotworz_Ekran(void);
void Amiga_PFL_Ustaw_Kursor(void);

void Amiga_Opcje_Ekran(void)
{
    int16_t pola[OPC_WIERSZY];
    int16_t esc;
    int16_t wejscie;
    int koniec = 0;
    int i;
    int pasek0;
    int wideo0;

    Amiga_Opcje_Wczytaj();
    pasek0 = amiga_opt_pasek;
    wideo0 = amiga_opt_wideo;

    Clear_Fields();
    for(i = 0; i < OPC_WIERSZY; i++)
    {
        int y = Amiga_Opcje_Y(i);
        pola[i] = Add_Hidden_Field(70, (int16_t)(y - 3), 250, (int16_t)(y + 8), 0, ST_UNDEFINED);
    }
    esc = Add_Hot_Key(27);
    Set_Mouse_List(1, mouse_list_default);
    Set_Input_Delay(4);

    while(!koniec)
    {
        Mark_Time();
        wejscie = Get_Input();
        if(wejscie == pola[0])
        {
            amiga_opt_kursor = !amiga_opt_kursor;
            Amiga_PFL_Ustaw_Kursor();
        }
        else if(wejscie == pola[1])
        {
            amiga_opt_pasek = !amiga_opt_pasek;
        }
        else if(wejscie == pola[2])
        {
            amiga_opt_wideo = (amiga_opt_wideo + 1) % 3;
        }
        else if(wejscie == pola[3] || wejscie == esc)
        {
            koniec = 1;
        }
        if(!koniec)
        {
            Amiga_Opcje_Rysuj(pola);
            Toggle_Pages();
            Release_Time(2);
        }
    }
    Clear_Fields();
    Amiga_Opcje_Zapisz();
    LOG_INFO(LOG_CAT_PFL, "[amiga] options: cursor=%d bar=%d video=%d", amiga_opt_kursor, amiga_opt_pasek, amiga_opt_wideo);
    /* pasek i tryb obrazu wymagaja nowego ekranu - jak w OpenXcomie przy
       wyjsciu z zakladki opcji, nie w trakcie przelaczania */
    if(amiga_opt_pasek != pasek0 || amiga_opt_wideo != wideo0)
    {
        Amiga_PFL_Przeotworz_Ekran();
    }
}
