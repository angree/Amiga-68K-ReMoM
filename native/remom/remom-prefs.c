/*
 * remom-prefs - ustawienia portu Master of Magic dla AmigaOS. Okno Intuition
 * (GadTools) na Workbenchu i linia polecen dla maszyn bez uzywalnej myszy.
 * Wzor: tools/gtaprefs.c z portu AmiGTA (developer, 2026-09-17: "mozesz
 * zgapic").
 *
 *     remom-prefs                    okno
 *     remom-prefs SHOW               wypisz ustawienia i co ma maszyna
 *     remom-prefs GFX=RTG VIDEO=PAL  zapisz bez okna
 *     remom-prefs ?                  pomoc
 *
 * DLACZEGO OSOBNY PROGRAM, skoro gra ma "Amiga Options": wybor grafiki
 * decyduje, czy gra w ogole cos pokaze. Gracz, ktoremu ekran gry nie wstaje,
 * nie dojdzie do menu w grze - wybor musi dac sie zrobic PRZED gra.
 *
 * Plik: PROGDIR:amiga.cfg (ten sam czyta i pisze gra -
 * native/remom/platform_amiga/amiga_Opcje.c), tekst "klucz=liczba":
 *   gfx=0..3     0 auto (= AGA), 1 AGA, 2 RTG, 3 okno na Workbenchu
 *   video=0..2   0 jak maszyna, 1 PAL, 2 NTSC
 *   bar=0|1      pasek tytulowy ekranu gry (gadzet glebi)
 *   fps=0|1      kl./s i najdluzsza klatka w tytule na pasku
 *   cursor=0|1   1 = wskaznik systemowy zamiast kursora gry
 *   music=0|1    muzyka
 *   musicrate=0|1  jakosc konwersji muzyki: 11025 / 22050 Hz (gra czyta Hz z WAV)
 *
 * "Convert music" (i CONVERT w Shellu): muzyka z MUSIC.LBX gracza do muzyka/
 * na tej Amidze - native/remom/muzyka_konw.c (2026-09-24: gracze nie mieli
 * konwertera na PC).
 *
 * Tekst w oknie po angielsku (to widzi gracz), komentarze po polsku.
 * Nigdy sprintf (CLAUDE.md, defekt 2) - tylko snprintf.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "muzyka_konw.h"

static const char verstag[] __attribute__((used)) =
    "$VER: remom-prefs 0.4.2 (26.09.2026)";

#define PLIK "PROGDIR:amiga.cfg"

struct Library *GadToolsBase = NULL;

/* ------------------------------------------------------------------------ */
/*  Ustawienia                                                              */
/* ------------------------------------------------------------------------ */

enum { O_GFX, O_VIDEO, O_BAR, O_FPS, O_CURSOR, O_MUSIC, O_MRATE, O_SYNTH, O_CONSOLE, O_COUNT };

typedef struct
{
    const char *klucz;      /* w pliku */
    const char *etykieta;   /* w oknie */
    const char *arg;        /* w linii polecen */
    char klawisz;
    int ile;
    const char *nazwy[5];   /* wartosci w oknie (i slowa w linii polecen) */
    const char *podpowiedz[4];
    int domyslna;
} opcja_t;

static const opcja_t OPCJE[O_COUNT] = {
    { "gfx", "Graphics:", "GFX", 'G', 4,
      { "Auto", "AGA", "RTG", "Window", NULL },
      { "An AGA screen - the safe default.",
        "Planar AGA screen. Real Amiga chipset.",
        "CyberGraphX/Picasso96 8-bit screen. Needs a graphics card.",
        "A window on Workbench. Slowest, works anywhere." }, 0 },
    { "video", "Video mode:", "VIDEO", 'V', 3,
      { "Auto", "PAL", "NTSC", NULL, NULL },
      { "Follow the machine's display standard.",
        "Force a 50 Hz PAL screen.",
        "Force a 60 Hz NTSC screen.", NULL }, 0 },
    { "bar", "Title bar:", "BAR", 'B', 2,
      { "Off", "On", NULL, NULL, NULL },
      { "Full screen, no bar.",
        "Screen title bar with the depth gadget (flip to Workbench).", NULL, NULL }, 1 },
    { "fps", "FPS on bar:", "FPS", 'F', 2,
      { "Off", "On", NULL, NULL, NULL },
      { "The title bar shows only the game's name.",
        "Title bar shows frames per second and the longest frame.", NULL, NULL }, 1 },
    { "cursor", "Pointer:", "CURSOR", 'P', 2,
      { "Game", "System", NULL, NULL, NULL },
      { "The game's own mouse cursor.",
        "The Workbench pointer - smooth at 50 Hz.", NULL, NULL }, 0 },
    { "music", "Music:", "MUSIC", 'M', 4,
      { "Off", "Files", "MIDI", "AdLib live", NULL },
      { "No music (sound effects stay). Saves some CPU.",
        "Converted music files from the muzyka drawer.",
        "MIDI via camd.library to an external synth (GM or MT-32 module).", 
        "AdLib computed live in the game - needs a fast CPU (68060); needs FAT.AD." }, 1 },
    { "musicrate", "Music quality:", "MUSICRATE", 'R', 2,
      { "11kHz", "22kHz", NULL, NULL, NULL },
      { "Convert at 11 kHz: needs about 32 MB of disk space.",
        "Convert at 22 kHz: clearer, needs about 63 MB of disk space.", NULL, NULL }, 0 },
    { "synth", "Music synth:", "SYNTH", 'Y', 2,
      { "Simple", "AdLib", NULL, NULL, NULL },
      { "Convert with the small built-in synthesiser.",
        "Convert with AdLib FM and the game's own instruments (needs FAT.AD).", NULL, NULL }, 0 },
    { "console", "Console:", "CONSOLE", 'O', 2,
      { "Off", "On", NULL, NULL, NULL },
      { "No text window when started from the icon (log: mom-wb.log).",
        "Show the game's text output in a window when started from the icon.", NULL, NULL }, 0 },
};

static int wart[O_COUNT];

static void wczytaj(void)
{
    FILE *f;
    char linia[80];
    int i;
    int v;

    for (i = 0; i < O_COUNT; i++) wart[i] = OPCJE[i].domyslna;
    f = fopen(PLIK, "r");
    if (f == NULL) return;
    while (fgets(linia, sizeof linia, f) != NULL) {
        for (i = 0; i < O_COUNT; i++) {
            size_t n = strlen(OPCJE[i].klucz);
            if (strncmp(linia, OPCJE[i].klucz, n) == 0 && linia[n] == '='
                && sscanf(linia + n + 1, "%d", &v) == 1
                && v >= 0 && v < OPCJE[i].ile) {
                wart[i] = v;
            }
        }
    }
    fclose(f);
}

static int zapisz(void)
{
    FILE *f = fopen(PLIK, "w");
    int i;
    if (f == NULL) return 0;
    for (i = 0; i < O_COUNT; i++) fprintf(f, "%s=%d\n", OPCJE[i].klucz, wart[i]);
    fclose(f);
    return 1;
}

/* ------------------------------------------------------------------------ */
/*  Co ma maszyna - tylko do odczytu, nic nie otwiera                        */
/* ------------------------------------------------------------------------ */

static int ma_aga(void)
{
    struct GfxBase *g = (struct GfxBase *)GfxBase;
    return (g != NULL && (g->ChipRevBits0 & GFXF_AA_ALICE)) ? 1 : 0;
}

static int ma_rtg(void)
{
    struct Library *b = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 0L);
    if (b == NULL) return 0;
    CloseLibrary(b);
    return 1;
}

static int ma_ntsc(void)
{
    struct GfxBase *g = (struct GfxBase *)GfxBase;
    return (g != NULL && (g->DisplayFlags & NTSC)) ? 1 : 0;
}

static void opis_maszyny(char *dst, int cap)
{
    snprintf(dst, (size_t)cap, "This machine:  AGA %s   RTG %s   %s",
             ma_aga() ? "yes" : "no", ma_rtg() ? "yes" : "no",
             ma_ntsc() ? "NTSC" : "PAL");
    dst[cap - 1] = 0;
}

/* ------------------------------------------------------------------------ */
/*  Okno                                                                     */
/* ------------------------------------------------------------------------ */

#define GID_OPCJA 10      /* 10..14 cykle, 20..24 podpowiedzi */
#define GID_PODP  20
#define GID_SAVE  30
#define GID_CANCEL 31

#define KLAWISZE "Keys: G V B F P M R Y O change  C convert  D delete  S save"
#define GID_KONW 32
#define GID_KASUJ 33
#define GID_STATUS 42

/* ------------------------------------------------------------------------ */
/*  Muzyka: konwersja na miejscu (native/remom/muzyka_konw.c)               */
/* ------------------------------------------------------------------------ */

static int konw_max = 0;          /* CONVERTMAX=n - tylko do testow */
static int rowne(const char *a, const char *b);

static long wybrana_czestotliwosc(void)
{
    return wart[O_MRATE] ? 22050L : 11025L;
}

/* katalog biezacy = katalog programu (tam leza LBX i muzyka/), tworzy muzyka/ */
static BPTR przejdz_do_gry(void)
{
    BPTR stary = CurrentDir(GetProgramDir());
    BPTR l = Lock((CONST_STRPTR)"muzyka", ACCESS_READ);
    if (l == 0) l = CreateDir((CONST_STRPTR)"muzyka");
    if (l != 0) UnLock(l);
    return stary;
}

static void stan_muzyki(char *dst, int cap)
{
    BPTR stary = CurrentDir(GetProgramDir());
    BPTR l = Lock((CONST_STRPTR)"muzyka", ACCESS_READ);
    int n = 0;
    if (l != 0) {
        struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
        if (fib != NULL && Examine(l, fib)) {
            while (ExNext(l, fib)) {
                size_t d = strlen((char *)fib->fib_FileName);
                if (d > 4 && rowne((char *)fib->fib_FileName + d - 4, ".wav")) n++;
            }
        }
        if (fib != NULL) FreeDosObject(DOS_FIB, fib);
        UnLock(l);
    }
    CurrentDir(stary);
    if (n > 0) snprintf(dst, (size_t)cap, "Music: %d files. Converting again needs 32 MB (11 kHz) or 63 MB (22 kHz).", n);
    else snprintf(dst, (size_t)cap, "Music: not converted yet. Needs 32 MB (11 kHz) or 63 MB (22 kHz).");
    dst[cap - 1] = 0;
}

/* zrzut ekranu Workbencha z oknem (test: czy okno sie miesci) - PPM */
static void zrzut(struct Screen *scr, const char *plik)
{
    FILE *f = fopen(plik, "wb");
    int x, y;
    ULONG rgb[3];
    UBYTE pal[256][3];
    int ile = 1 << scr->RastPort.BitMap->Depth;
    if (f == NULL) return;
    if (ile > 256) ile = 256;
    for (x = 0; x < ile; x++) {
        GetRGB32(scr->ViewPort.ColorMap, (ULONG)x, 1, rgb);
        pal[x][0] = (UBYTE)(rgb[0] >> 24); pal[x][1] = (UBYTE)(rgb[1] >> 24); pal[x][2] = (UBYTE)(rgb[2] >> 24);
    }
    fprintf(f, "P6\n%d %d\n255\n", (int)scr->Width, (int)scr->Height);
    for (y = 0; y < scr->Height; y++)
        for (x = 0; x < scr->Width; x++) {
            LONG c = ReadPixel(&scr->RastPort, (LONG)x, (LONG)y);
            fwrite(pal[(c < 0 || c >= ile) ? 0 : c], 1, 3, f);
        }
    fclose(f);
}

/* Konwersja w oknie; w trakcie okno dalej odpowiada, Esc / Convert / zamkniecie
   przerywa (niedokonczony plik jest kasowany, gotowe zostaja). */
static void konwertuj_okno(struct Window *win, struct Gadget *st)
{
    static char tekst[128];
    char blad[128];
    char poprzedni[128];
    BPTR stary = przejdz_do_gry();
    konw_t *k = Konw_Start("muzyka", wybrana_czestotliwosc(), wart[O_SYNTH], konw_max, blad, (int)sizeof blad);
    int r = 1, przerwij = 0;
    if (k == NULL) {
        CurrentDir(stary);
        snprintf(tekst, sizeof tekst, "%s", blad);
        GT_SetGadgetAttrs(st, win, NULL, GTTX_Text, (ULONG)tekst, TAG_END);
        return;
    }
    poprzedni[0] = 0;
    while (r > 0 && !przerwij) {
        struct IntuiMessage *msg;
        r = Konw_Krok(k, tekst, (int)sizeof tekst);
        if (strcmp(tekst, poprzedni) != 0) {
            strcpy(poprzedni, tekst);
            GT_SetGadgetAttrs(st, win, NULL, GTTX_Text, (ULONG)tekst, TAG_END);
        }
        while ((msg = GT_GetIMsg(win->UserPort)) != NULL) {
            ULONG cls = msg->Class;
            UWORD code = msg->Code;
            struct Gadget *src = (struct Gadget *)msg->IAddress;
            GT_ReplyIMsg(msg);
            if (cls == IDCMP_CLOSEWINDOW) przerwij = 1;
            else if (cls == IDCMP_VANILLAKEY && (code == 27 || code == 'c' || code == 'C')) przerwij = 1;
            else if (cls == IDCMP_GADGETUP && src->GadgetID == GID_KONW) przerwij = 1;
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); }
        }
    }
    if (przerwij) snprintf(tekst, sizeof tekst, "Stopped at %d files - press Convert music again to continue.", Konw_Zrobione(k));
    Konw_Koniec(k);
    CurrentDir(stary);
    GT_SetGadgetAttrs(st, win, NULL, GTTX_Text, (ULONG)tekst, TAG_END);
}

/* ---- kasowanie muzyki (0.3.0) ------------------------------------------
   Tylko muzyka/#?.wav i pozostalosci konw*.tmp - nic innego z katalogu gry.
   Nazwy zbierane najpierw, kasowane potem: DeleteFile w trakcie ExNext
   psuje przegladanie katalogu. */
static int muzyka_pliki(int kasuj)
{
    BPTR stary = CurrentDir(GetProgramDir());
    BPTR l = Lock((CONST_STRPTR)"muzyka", ACCESS_READ);
    int n = 0;
    if (l != 0) {
        struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
        char (*nazwy)[32] = (char (*)[32])malloc(512 * 32);
        int ile = 0, i;
        if (fib != NULL && nazwy != NULL && Examine(l, fib)) {
            while (ExNext(l, fib) && ile < 512) {
                char *nm = (char *)fib->fib_FileName;
                size_t d = strlen(nm);
                if (fib->fib_DirEntryType < 0 && d > 4 && d < 32
                    && (rowne(nm + d - 4, ".wav") || rowne(nm + d - 4, ".tmp")))
                    strcpy(nazwy[ile++], nm);
            }
        }
        if (kasuj) {
            BPTR st2 = CurrentDir(l);
            for (i = 0; i < ile; i++) if (DeleteFile((CONST_STRPTR)nazwy[i])) n++;
            CurrentDir(st2);
        } else {
            n = ile;
        }
        free(nazwy);
        if (fib != NULL) FreeDosObject(DOS_FIB, fib);
        UnLock(l);
    }
    CurrentDir(stary);
    return n;
}

/* pytanie przed kasowaniem - zeby missclick nie zabral 40 minut konwersji.
   test: requester pokazany, zrzut prefs-kasuj.ppm, zamkniety bez kasowania. */
static int potwierdz_kasowanie(struct Window *win, int n, int test)
{
    struct EasyStruct es;
    LONG arg[1];
    es.es_StructSize = sizeof es;
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)"Master of Magic - Delete music";
    es.es_TextFormat = (UBYTE *)"Delete all %ld converted music files?\nThis cannot be undone.";
    es.es_GadgetFormat = (UBYTE *)"Delete|Cancel";
    arg[0] = n;
    if (test) {
        struct Window *rw = BuildEasyRequestArgs(win, &es, 0, arg);
        if (rw != NULL && (ULONG)rw > 1) {
            Delay(50);
            zrzut(win->WScreen, "prefs-kasuj.ppm");
            FreeSysRequest(rw);
        }
        return 0;
    }
    return EasyRequestArgs(win, &es, NULL, arg) == 1;
}

static void kasuj_okno(struct Window *win, struct Gadget *st, int test)
{
    static char tekst[96];
    int n = muzyka_pliki(0);
    if (n == 0) snprintf(tekst, sizeof tekst, "No converted music to delete.");
    else if (!potwierdz_kasowanie(win, n, test)) snprintf(tekst, sizeof tekst, "Delete cancelled - the music files stay.");
    else snprintf(tekst, sizeof tekst, "Deleted %d music files.", muzyka_pliki(1));
    GT_SetGadgetAttrs(st, win, NULL, GTTX_Text, (ULONG)tekst, TAG_END);
}

/* linia polecen: CONVERT */
static int konwertuj_shell(void)
{
    char tekst[128];
    char blad[128];
    int ostatni = -1, r;
    BPTR stary = przejdz_do_gry();
    konw_t *k = Konw_Start("muzyka", wybrana_czestotliwosc(), wart[O_SYNTH], konw_max, blad, (int)sizeof blad);
    if (k == NULL) { CurrentDir(stary); printf("remom-prefs: %s\n", blad); return 20; }
    printf("Converting %d music tracks at %ld Hz (Ctrl-C stops)...\n", Konw_Ile(k), wybrana_czestotliwosc());
    while ((r = Konw_Krok(k, tekst, (int)sizeof tekst)) > 0) {
        if (Konw_Zrobione(k) != ostatni) { ostatni = Konw_Zrobione(k); printf("%s\n", tekst); fflush(stdout); }
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) { printf("*** Break\n"); r = -1; break; }
    }
    printf("%s\n", tekst);
    Konw_Koniec(k);
    CurrentDir(stary);
    return r < 0 ? 10 : 0;
}

static int szer(struct Screen *scr, const char *s)
{
    return (int)TextLength(&scr->RastPort, (CONST_STRPTR)s, (ULONG)strlen(s));
}

/* 1 = zapisz, 0 = anuluj, -1 = okno sie nie otworzylo.
   test_ms > 0: okno otwiera sie, po tym czasie zamyka bez zapisu (test). */
static int okno(int test_ms, int test_konw, int test_kasuj)
{
    struct Screen *scr;
    APTR vi;
    struct Gadget *glist = NULL, *gad;
    struct Gadget *cykl[O_COUNT], *podp[O_COUNT];
    struct Gadget *status;
    char stan[128];
    struct Window *win;
    struct NewGadget ng;
    STRPTR etyk[O_COUNT][6];
    char maszyna[96];
    int i, j, cw, fh, gh, lm, gap, labw, gadw, hintw, innerw, innerh;
    int leftb, topb, y, btnw;
    int wynik = -1, koniec = 0, czekano = 0;

    scr = LockPubScreen(NULL);
    if (scr == NULL) return -1;
    vi = GetVisualInfo(scr, TAG_END);
    if (vi == NULL) { UnlockPubScreen(NULL, scr); return -1; }

    opis_maszyny(maszyna, (int)sizeof maszyna);
    stan_muzyki(stan, (int)sizeof stan);

    /* wszystko liczone z fontu ekranu - Workbench moze miec dowolny */
    cw = scr->RastPort.TxWidth;  if (cw < 6) cw = 6;
    fh = scr->RastPort.TxHeight; if (fh < 8) fh = 8;
    gh = fh + 6;
    lm = cw * 2;
    gap = fh / 2; if (gap < 4) gap = 4;

    labw = 0; gadw = 0; hintw = 0;
    for (i = 0; i < O_COUNT; i++) {
        int t = szer(scr, OPCJE[i].etykieta) + cw;
        if (t > labw) labw = t;
        for (j = 0; j < OPCJE[i].ile; j++) {
            etyk[i][j] = (STRPTR)OPCJE[i].nazwy[j];
            t = szer(scr, OPCJE[i].nazwy[j]) + cw * 2 + 24;
            if (t > gadw) gadw = t;
            t = szer(scr, OPCJE[i].podpowiedz[j]);
            if (t > hintw) hintw = t;
        }
        etyk[i][OPCJE[i].ile] = NULL;
    }
    /* DWIE KOLUMNY i JEDNA wspolna linia podpowiedzi (2026-09-24): z podpowiedzia
       pod kazda opcja okno mialo 371 px wysokosci i nie miescilo sie na
       Workbenchu 640x256 - a doszly jeszcze jakosc muzyki i konwersja. */
    innerw = lm + (labw + gadw) * 2 + cw * 3 + lm;
    if (lm + hintw + lm > innerw) innerw = lm + hintw + lm;
    if (lm + szer(scr, maszyna) + lm > innerw) innerw = lm + szer(scr, maszyna) + lm;
    if (lm + szer(scr, KLAWISZE) + lm > innerw) innerw = lm + szer(scr, KLAWISZE) + lm;
    if (lm + szer(scr, stan) + lm > innerw) innerw = lm + szer(scr, stan) + lm;
    {   /* cztery przyciski w jednym rzedzie: Save, Convert, Delete, Cancel */
        int b = szer(scr, "Cancel") + cw * 4;
        int rzad;
        if (b < cw * 10) b = cw * 10;
        rzad = lm * 2 + b * 2 + szer(scr, "Convert music") + szer(scr, "Delete music") + cw * 14;
        if (rzad > innerw) innerw = rzad;
    }

    leftb = scr->WBorLeft;
    topb = scr->WBorTop + scr->Font->ta_YSize + 1;

    gad = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
    y = gap;

    for (i = 0; i < O_COUNT; i++) {
        int kol = (i < 5) ? 0 : 1, wiersz = (i < 5) ? i : i - 5;   /* 5 z lewej, reszta z prawej */
        ng.ng_LeftEdge = leftb + lm + labw + kol * (labw + gadw + cw * 3);
        ng.ng_TopEdge = topb + y + wiersz * (gh + 2);
        ng.ng_Width = gadw;
        ng.ng_Height = gh;
        ng.ng_GadgetText = (STRPTR)OPCJE[i].etykieta;
        ng.ng_GadgetID = GID_OPCJA + i;
        ng.ng_Flags = PLACETEXT_LEFT;
        gad = CreateGadget(CYCLE_KIND, gad, &ng,
                           GTCY_Labels, (ULONG)etyk[i],
                           GTCY_Active, (ULONG)wart[i], TAG_END);
        cykl[i] = gad;
    }
    y += 5 * (gh + 2) + gap;

    /* wspolna podpowiedz: opis opcji zmienionej ostatnio */
    ng.ng_LeftEdge = leftb + lm;
    ng.ng_TopEdge = topb + y;
    ng.ng_Width = innerw - lm * 2;
    ng.ng_Height = fh;
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID = GID_PODP;
    ng.ng_Flags = 0;
    gad = CreateGadget(TEXT_KIND, gad, &ng,
                       GTTX_Text, (ULONG)OPCJE[0].podpowiedz[wart[0]], TAG_END);
    for (i = 0; i < O_COUNT; i++) podp[i] = gad;
    y += fh + gap + 2;

    ng.ng_LeftEdge = leftb + lm;
    ng.ng_TopEdge = topb + y;
    ng.ng_Width = innerw - lm * 2;
    ng.ng_Height = fh;
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID = 40;
    ng.ng_Flags = 0;
    gad = CreateGadget(TEXT_KIND, gad, &ng, GTTX_Text, (ULONG)maszyna, TAG_END);
    y += fh + 2;
    ng.ng_TopEdge = topb + y;
    ng.ng_GadgetID = 41;
    gad = CreateGadget(TEXT_KIND, gad, &ng, GTTX_Text, (ULONG)KLAWISZE, TAG_END);
    y += fh + 2;
    ng.ng_TopEdge = topb + y;
    ng.ng_GadgetID = GID_STATUS;
    gad = CreateGadget(TEXT_KIND, gad, &ng, GTTX_Text, (ULONG)stan, TAG_END);
    status = gad;
    y += fh + gap + gap;

    btnw = szer(scr, "Cancel") + cw * 4;
    if (btnw < cw * 10) btnw = cw * 10;
    ng.ng_LeftEdge = leftb + lm;
    ng.ng_TopEdge = topb + y;
    ng.ng_Width = btnw;
    ng.ng_Height = gh;
    ng.ng_GadgetText = (STRPTR)"Save";
    ng.ng_GadgetID = GID_SAVE;
    ng.ng_Flags = PLACETEXT_IN;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    ng.ng_LeftEdge = leftb + innerw - lm - btnw;
    ng.ng_GadgetText = (STRPTR)"Cancel";
    ng.ng_GadgetID = GID_CANCEL;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    ng.ng_Width = szer(scr, "Convert music") + cw * 4;
    ng.ng_LeftEdge = leftb + lm + btnw + cw * 2;
    ng.ng_GadgetText = (STRPTR)"Convert music";
    ng.ng_GadgetID = GID_KONW;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    ng.ng_Width = szer(scr, "Delete music") + cw * 4;
    ng.ng_LeftEdge = leftb + innerw - lm - btnw - cw * 2 - ng.ng_Width;
    ng.ng_GadgetText = (STRPTR)"Delete music";
    ng.ng_GadgetID = GID_KASUJ;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    y += gh + gap;
    innerh = y;

    if (gad == NULL) {   /* jeden nieudany CreateGadget -> NULL do konca */
        FreeGadgets(glist); FreeVisualInfo(vi); UnlockPubScreen(NULL, scr);
        return -1;
    }

    win = OpenWindowTags(NULL,
        WA_Title, (ULONG)"Master of Magic - Amiga Settings",
        WA_InnerWidth, (ULONG)innerw,
        WA_InnerHeight, (ULONG)innerh,
        WA_Left, (ULONG)(scr->Width > innerw ? (scr->Width - innerw) / 2 : 0),
        WA_Top, (ULONG)(scr->Height > innerh ? (scr->Height - innerh) / 3 : 0),
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_Activate, TRUE,
        WA_SmartRefresh, TRUE,
        WA_PubScreen, (ULONG)scr,
        WA_Gadgets, (ULONG)glist,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW
                  | IDCMP_VANILLAKEY | IDCMP_INTUITICKS,
        TAG_END);
    if (win == NULL) {
        FreeGadgets(glist); FreeVisualInfo(vi); UnlockPubScreen(NULL, scr);
        return -1;
    }
    GT_RefreshWindow(win, NULL);
    if (test_ms > 0) {
        printf("remom-prefs: okno otwarte %dx%d na ekranie %dx%d\n",
               (int)win->Width, (int)win->Height, (int)scr->Width, (int)scr->Height);
        fflush(stdout);
    }
    if (test_ms > 0) zrzut(scr, "prefs-okno.ppm");
    if (test_kasuj) kasuj_okno(win, status, 1);
    if (test_konw) {
        printf("remom-prefs: test konwersji - musicrate=%d, %ld Hz\n", wart[O_MRATE], wybrana_czestotliwosc());
        konwertuj_okno(win, status);
        zrzut(scr, "prefs-konw.ppm");
        printf("remom-prefs: po konwersji w oknie\n");
    }

    while (!koniec) {
        struct IntuiMessage *msg;
        WaitPort(win->UserPort);
        while ((msg = GT_GetIMsg(win->UserPort)) != NULL) {
            ULONG cls = msg->Class;
            UWORD code = msg->Code;
            struct Gadget *src = (struct Gadget *)msg->IAddress;
            GT_ReplyIMsg(msg);
            switch (cls) {
            case IDCMP_INTUITICKS:   /* co 0,1 s - tylko dla trybu testu */
                if (test_ms > 0) {
                    czekano += 100;
                    if (czekano >= test_ms) { wynik = 0; koniec = 1; }
                }
                break;
            case IDCMP_CLOSEWINDOW:
                wynik = 0; koniec = 1;
                break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(win);
                GT_EndRefresh(win, TRUE);
                break;
            case IDCMP_GADGETUP:
                i = (int)src->GadgetID - GID_OPCJA;
                if (i >= 0 && i < O_COUNT) {
                    wart[i] = (int)code;
                    GT_SetGadgetAttrs(podp[i], win, NULL,
                                      GTTX_Text, (ULONG)OPCJE[i].podpowiedz[wart[i]], TAG_END);
                } else if (src->GadgetID == GID_SAVE) {
                    wynik = 1; koniec = 1;
                } else if (src->GadgetID == GID_KASUJ) {
                    kasuj_okno(win, status, 0);
                } else if (src->GadgetID == GID_KONW) {
                    konwertuj_okno(win, status);
                } else if (src->GadgetID == GID_CANCEL) {
                    wynik = 0; koniec = 1;
                }
                break;
            case IDCMP_VANILLAKEY:
                /* klawiatura - na maszynie bez dzialajacej myszy to jedyna droga */
                for (i = 0; i < O_COUNT; i++) {
                    if (code == (UWORD)OPCJE[i].klawisz || code == (UWORD)(OPCJE[i].klawisz + 32)) {
                        wart[i] = (wart[i] + 1) % OPCJE[i].ile;
                        GT_SetGadgetAttrs(cykl[i], win, NULL, GTCY_Active, (ULONG)wart[i], TAG_END);
                        GT_SetGadgetAttrs(podp[i], win, NULL,
                                          GTTX_Text, (ULONG)OPCJE[i].podpowiedz[wart[i]], TAG_END);
                    }
                }
                if (code == 's' || code == 'S' || code == 13) { wynik = 1; koniec = 1; }
                if (code == 27) { wynik = 0; koniec = 1; }
                if (code == 'c' || code == 'C') konwertuj_okno(win, status);
                if (code == 'd' || code == 'D') kasuj_okno(win, status, 0);
                break;
            default:
                break;
            }
        }
    }

    CloseWindow(win);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    return wynik;
}

/* ------------------------------------------------------------------------ */
/*  Linia polecen                                                            */
/* ------------------------------------------------------------------------ */

static int rowne(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a == ' ') { a++; continue; }   /* "AdLib live" = ADLIBLIVE w Shellu */
        if (*b == ' ') { b++; continue; }
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static void pokaz(void)
{
    char maszyna[96];
    int i;
    opis_maszyny(maszyna, (int)sizeof maszyna);
    printf("%s\n", maszyna);
    for (i = 0; i < O_COUNT; i++) {
        printf("%-7s %-7s - %s\n", OPCJE[i].arg, OPCJE[i].nazwy[wart[i]],
               OPCJE[i].podpowiedz[wart[i]]);
    }
}

static void pomoc(void)
{
    int i, j;
    printf("remom-prefs - Amiga settings for Master of Magic\n\n");
    printf("  remom-prefs              open the window\n");
    printf("  remom-prefs SHOW         print the settings and this machine\n");
    printf("  remom-prefs CONVERT      convert the music (add MUSICRATE=22kHz for 22 kHz)\n");
    for (i = 0; i < O_COUNT; i++) {
        printf("  remom-prefs %s=", OPCJE[i].arg);
        for (j = 0; j < OPCJE[i].ile; j++) printf("%s%s", j ? "|" : "", OPCJE[i].nazwy[j]);
        printf("\n");
    }
    printf("\nAny KEY=VALUE saves at once without a window. File: " PLIK "\n");
}

int main(int argc, char **argv)
{
    int i, j, zmiana = 0, show = 0, test_ms = 0, test_konw = 0, test_kasuj = 0, konw = 0, kasuj = 0, r;

    wczytaj();

    /* argc == 0: start z Workbencha - argv to wtedy komunikat WBStartup */
    for (i = 1; i < argc; i++) {
        char *eq;
        if (argv[i][0] == '?' || rowne(argv[i], "HELP")) { pomoc(); return 0; }
        if (rowne(argv[i], "SHOW")) { show = 1; continue; }
        if (rowne(argv[i], "TESTWINDOW")) { test_ms = 3000; continue; }
        if (rowne(argv[i], "TESTCONVERT")) { test_ms = 3000; test_konw = 1; continue; }
        if (rowne(argv[i], "TESTDELETE")) { test_ms = 3000; test_kasuj = 1; continue; }
        if (rowne(argv[i], "DELETEMUSIC")) { kasuj = 1; continue; }
        if (rowne(argv[i], "CONVERT")) { konw = 1; continue; }
        if (strncmp(argv[i], "CONVERTMAX=", 11) == 0) { konw_max = atoi(argv[i] + 11); continue; }
        eq = strchr(argv[i], '=');
        if (eq != NULL) {
            int ok = 0;
            *eq = 0;
            for (j = 0; j < O_COUNT && !ok; j++) {
                if (rowne(argv[i], OPCJE[j].arg)) {
                    int k;
                    for (k = 0; k < OPCJE[j].ile; k++) {
                        if (rowne(eq + 1, OPCJE[j].nazwy[k])) { wart[j] = k; ok = 1; zmiana = 1; }
                    }
                    if (!ok) {
                        printf("remom-prefs: bad value \"%s\" for %s\n\n", eq + 1, OPCJE[j].arg);
                        pomoc();
                        return 20;
                    }
                }
            }
            if (ok) continue;
            *eq = '=';
        }
        printf("remom-prefs: do not understand \"%s\"\n\n", argv[i]);
        pomoc();
        return 20;
    }

    if (zmiana) {
        if (!zapisz()) {
            printf("remom-prefs: COULD NOT WRITE " PLIK " - is the drawer write protected?\n");
            return 20;
        }
        pokaz();
        printf("saved to " PLIK "\n");
        if (!konw) return 0;
    }
    if (kasuj) { printf("Deleted %d music files.\n", muzyka_pliki(1)); if (!konw) return 0; }
    if (konw) return konwertuj_shell();
    if (show) { pokaz(); return 0; }

    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 37L);
    if (GadToolsBase == NULL) {
        printf("remom-prefs: no gadtools.library v37 - use the command line:\n\n");
        pomoc();
        return 20;
    }
    r = okno(test_ms, test_konw, test_kasuj);
    CloseLibrary(GadToolsBase);
    GadToolsBase = NULL;

    if (r < 0) {
        printf("remom-prefs: could not open the window - use the command line:\n\n");
        pomoc();
        return 20;
    }
    if (r == 1) {
        if (!zapisz()) {
            printf("remom-prefs: COULD NOT WRITE " PLIK "\n");
            return 20;
        }
        if (argc > 0) {   /* z ikony (argc == 0) bez tekstu - inaczej libnix otwiera okno CLI (gracz 0.4.0) */
            pokaz();
            printf("saved to " PLIK "\n");
        }
    }
    if (test_ms > 0) {
        printf("remom-prefs: okno zamkniete (wynik %d)\n", r);
    }
    return 0;
}
