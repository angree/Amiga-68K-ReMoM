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

static const char verstag[] __attribute__((used)) =
    "$VER: remom-prefs 1.0 (17.09.2026)";

#define PLIK "PROGDIR:amiga.cfg"

struct Library *GadToolsBase = NULL;

/* ------------------------------------------------------------------------ */
/*  Ustawienia                                                              */
/* ------------------------------------------------------------------------ */

enum { O_GFX, O_VIDEO, O_BAR, O_FPS, O_CURSOR, O_MUSIC, O_COUNT };

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
    { "music", "Music:", "MUSIC", 'M', 2,
      { "Off", "On", NULL, NULL, NULL },
      { "No music (sound effects stay). Saves some CPU.",
        "Music streamed from the muzyka drawer.", NULL, NULL }, 1 },
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

#define KLAWISZE "Keys:  G V B F P M change   S save   Esc cancel"

static int szer(struct Screen *scr, const char *s)
{
    return (int)TextLength(&scr->RastPort, (CONST_STRPTR)s, (ULONG)strlen(s));
}

/* 1 = zapisz, 0 = anuluj, -1 = okno sie nie otworzylo.
   test_ms > 0: okno otwiera sie, po tym czasie zamyka bez zapisu (test). */
static int okno(int test_ms)
{
    struct Screen *scr;
    APTR vi;
    struct Gadget *glist = NULL, *gad;
    struct Gadget *cykl[O_COUNT], *podp[O_COUNT];
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
    innerw = lm + labw + gadw + lm;
    if (lm + hintw + lm > innerw) innerw = lm + hintw + lm;
    if (lm + szer(scr, maszyna) + lm > innerw) innerw = lm + szer(scr, maszyna) + lm;
    if (lm + szer(scr, KLAWISZE) + lm > innerw) innerw = lm + szer(scr, KLAWISZE) + lm;

    leftb = scr->WBorLeft;
    topb = scr->WBorTop + scr->Font->ta_YSize + 1;

    gad = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
    y = gap;

    for (i = 0; i < O_COUNT; i++) {
        ng.ng_LeftEdge = leftb + lm + labw;
        ng.ng_TopEdge = topb + y;
        ng.ng_Width = gadw;
        ng.ng_Height = gh;
        ng.ng_GadgetText = (STRPTR)OPCJE[i].etykieta;
        ng.ng_GadgetID = GID_OPCJA + i;
        ng.ng_Flags = PLACETEXT_LEFT;
        gad = CreateGadget(CYCLE_KIND, gad, &ng,
                           GTCY_Labels, (ULONG)etyk[i],
                           GTCY_Active, (ULONG)wart[i], TAG_END);
        cykl[i] = gad;
        y += gh + 2;

        ng.ng_LeftEdge = leftb + lm;
        ng.ng_TopEdge = topb + y;
        ng.ng_Width = innerw - lm * 2;
        ng.ng_Height = fh;
        ng.ng_GadgetText = NULL;
        ng.ng_GadgetID = GID_PODP + i;
        ng.ng_Flags = 0;
        gad = CreateGadget(TEXT_KIND, gad, &ng,
                           GTTX_Text, (ULONG)OPCJE[i].podpowiedz[wart[i]], TAG_END);
        podp[i] = gad;
        y += fh + gap;
    }
    y += gap;

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
    for (i = 0; i < O_COUNT; i++) {
        printf("  remom-prefs %s=", OPCJE[i].arg);
        for (j = 0; j < OPCJE[i].ile; j++) printf("%s%s", j ? "|" : "", OPCJE[i].nazwy[j]);
        printf("\n");
    }
    printf("\nAny KEY=VALUE saves at once without a window. File: " PLIK "\n");
}

int main(int argc, char **argv)
{
    int i, j, zmiana = 0, show = 0, test_ms = 0, r;

    wczytaj();

    /* argc == 0: start z Workbencha - argv to wtedy komunikat WBStartup */
    for (i = 1; i < argc; i++) {
        char *eq;
        if (argv[i][0] == '?' || rowne(argv[i], "HELP")) { pomoc(); return 0; }
        if (rowne(argv[i], "SHOW")) { show = 1; continue; }
        if (rowne(argv[i], "TESTWINDOW")) { test_ms = 3000; continue; }
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
        return 0;
    }
    if (show) { pokaz(); return 0; }

    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 37L);
    if (GadToolsBase == NULL) {
        printf("remom-prefs: no gadtools.library v37 - use the command line:\n\n");
        pomoc();
        return 20;
    }
    r = okno(test_ms);
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
        pokaz();
        printf("saved to " PLIK "\n");
    }
    if (test_ms > 0) {
        printf("remom-prefs: okno zamkniete (wynik %d)\n", r);
    }
    return 0;
}
