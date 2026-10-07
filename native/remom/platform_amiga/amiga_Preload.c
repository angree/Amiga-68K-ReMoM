/* amiga_Preload.c - preload=1 (remom-prefs, 0.4.4): pliki LBX gry kopiowane
 * przy starcie do RAM:MoM-preload, a STU_GRAF_Open_Asset czyta je stamtad.
 *
 * Gracz (Warp, 224 MB): wczytywanie z dysku przycina muzyke i trwa. Kopia
 * w RAM: to prosty "ram disk" - gra dalej uzywa fopen/fread/fseek, wiec zadna
 * sciezka czytania LBX sie nie zmienia. Pomijane: intro i zakonczenia (raz
 * na gre) oraz muzyka z plikow (katalog muzyka, nie LBX).
 *
 * Wlacza sie tylko gdy wolnej FastRAM jest co najmniej rozmiar danych plus
 * AMIGA_PRELOAD_ZAPAS (gra sama potrzebuje kilku MB). Pliki w RAM: sa
 * kasowane przy wyjsciu; po wywrotce zostaja i nastepny start ich uzywa
 * (kopiuje tylko te, ktorych rozmiar sie nie zgadza). */

#include <proto/exec.h>
#include <exec/execbase.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/exall.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AMIGA_PRELOAD_KAT   "RAM:MoM-preload"
#define AMIGA_PRELOAD_ZAPAS (16UL * 1024UL * 1024UL)
#define AMIGA_PRELOAD_BUF   (64L * 1024L)
#define AMIGA_PRELOAD_MAX   160

extern int amiga_opt_preload;                                        /* amiga_Opcje.c */
extern FILE * (*amiga_preload_otworz)(const char *, const char *);  /* STU_GRAF.c (latka) */
extern FILE * lbxload_fptr;                                          /* LBX_Load.c */

static const char * const amiga_preload_pomin[] = {
    "INTRO.LBX", "INTROSFX.LBX", "INTROSND.LBX", "WIN.LBX", "LOSE.LBX", "SPLMASTR.LBX", NULL
};

static char amiga_preload_nazwy[AMIGA_PRELOAD_MAX][32];
static long amiga_preload_rozm[AMIGA_PRELOAD_MAX];
static int amiga_preload_ile = 0;

static int Amiga_Preload_Rowne(const char * a, const char * b)
{
    while(*a && *b)
    {
        char x = *a++, y = *b++;
        if(x >= 'a' && x <= 'z') { x = (char)(x - 32); }
        if(y >= 'a' && y <= 'z') { y = (char)(y - 32); }
        if(x != y) { return 0; }
    }
    return (*a == 0 && *b == 0);
}

static int Amiga_Preload_Lbx(const char * n)
{
    size_t l = strlen(n);
    int i;
    if(l < 5 || !Amiga_Preload_Rowne(n + l - 4, ".LBX")) { return 0; }
    for(i = 0; amiga_preload_pomin[i] != NULL; i++)
    {
        if(Amiga_Preload_Rowne(n, amiga_preload_pomin[i])) { return 0; }
    }
    return 1;
}

static FILE * Amiga_Preload_Otworz(const char * nazwa, const char * tryb)
{
    char sc[64];
    if(tryb[0] != 'r' || strchr(tryb, '+') != NULL) { return NULL; }
    if(strchr(nazwa, ':') != NULL || strchr(nazwa, '/') != NULL) { return NULL; }
    if(!Amiga_Preload_Lbx(nazwa)) { return NULL; }
    snprintf(sc, sizeof(sc), "%s/%s", AMIGA_PRELOAD_KAT, nazwa);
    return fopen(sc, tryb);   /* brak w RAM: -> NULL -> gra czyta z dysku */
}

static void Amiga_Preload_Sprzatnij(void)
{
    char sc[64];
    int i;
    amiga_preload_otworz = NULL;
    if(lbxload_fptr != NULL)   /* otwarty plik w RAM: nie da sie skasowac */
    {
        fclose(lbxload_fptr);
        lbxload_fptr = NULL;
    }
    for(i = 0; i < amiga_preload_ile; i++)
    {
        snprintf(sc, sizeof(sc), "%s/%s", AMIGA_PRELOAD_KAT, amiga_preload_nazwy[i]);
        DeleteFile((CONST_STRPTR)sc);
    }
    DeleteFile((CONST_STRPTR)AMIGA_PRELOAD_KAT);
}

/* 1 = plik jest w RAM: (skopiowany teraz albo zostal z poprzedniego startu) */
static int Amiga_Preload_Kopiuj(const char * nazwa, long rozmiar, UBYTE * buf)
{
    char sc[64];
    BPTR we, wy;
    struct FileInfoBlock * fib;
    long n;
    int ok = 1;

    snprintf(sc, sizeof(sc), "%s/%s", AMIGA_PRELOAD_KAT, nazwa);
    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if(fib != NULL)
    {
        BPTR l = Lock((CONST_STRPTR)sc, ACCESS_READ);
        if(l != 0)
        {
            if(Examine(l, fib) && fib->fib_Size == rozmiar) { ok = 2; }
            UnLock(l);
        }
        FreeDosObject(DOS_FIB, fib);
    }
    if(ok == 2) { return 1; }

    we = Open((CONST_STRPTR)nazwa, MODE_OLDFILE);
    if(we == 0) { return 0; }
    wy = Open((CONST_STRPTR)sc, MODE_NEWFILE);
    if(wy == 0) { Close(we); return 0; }
    while((n = Read(we, buf, AMIGA_PRELOAD_BUF)) > 0)
    {
        if(Write(wy, buf, n) != n) { ok = 0; break; }
    }
    if(n < 0) { ok = 0; }
    Close(wy);
    Close(we);
    if(!ok) { DeleteFile((CONST_STRPTR)sc); }
    return ok;
}

extern void (*amiga_pool_pamiec)(const char *);   /* Allocate_Pool.c (latka) */

/* Rezerwa Chip na czas tworzenia puli (0.4.4): pula bierze reszte takze z
   Chip, a dzwiek (probki, bufory muzyki) i ekran potrzebuja Chip pozniej. */
#define AMIGA_CHIP_REZERWA (512UL * 1024UL)
static void * amiga_chip_rezerwa = NULL;

static void Amiga_Pamiec_Wypisz(const char * kiedy)
{
    if(strcmp(kiedy, "przed pula") == 0 && amiga_chip_rezerwa == NULL)
    {
        amiga_chip_rezerwa = AllocMem(AMIGA_CHIP_REZERWA, MEMF_CHIP);
    }
    else if(strcmp(kiedy, "po puli") == 0 && amiga_chip_rezerwa != NULL)
    {
        FreeMem(amiga_chip_rezerwa, AMIGA_CHIP_REZERWA);
        amiga_chip_rezerwa = NULL;
    }
    printf("[amiga] pamiec %s: Fast wolne %lu KB (najwiekszy blok %lu KB), Chip wolne %lu KB (najwiekszy %lu KB)\n", kiedy,
           AvailMem(MEMF_FAST) / 1024UL, AvailMem(MEMF_FAST | MEMF_LARGEST) / 1024UL,
           AvailMem(MEMF_CHIP) / 1024UL, AvailMem(MEMF_CHIP | MEMF_LARGEST) / 1024UL);
    fflush(stdout);
}

void Amiga_Preload_Start(void)
{
    struct FileInfoBlock * fib;
    BPTR kat, l;
    UBYTE * buf;
    unsigned long suma = 0, wolne;
    struct DateStamp t0, t1;
    long ms;
    int i;

    amiga_pool_pamiec = Amiga_Pamiec_Wypisz;
    { extern void Amiga_Straznik_Start(void); Amiga_Straznik_Start(); }  /* 0.7.6: straznik zwisu */
    Amiga_Pamiec_Wypisz("na starcie");
    {   /* 0.7.5: procesor do logu - pierwsze pytanie przy zgloszeniach o muzyke/predkosc */
        UWORD a = ((struct ExecBase *)SysBase)->AttnFlags;
        printf("[amiga] procesor: %s%s\n",
               (a & 0x80) ? "68060" : (a & 0x08) ? "68040" : (a & 0x04) ? "68030" : (a & 0x02) ? "68020" : (a & 0x01) ? "68010" : "68000",
               (a & 0x70) ? " + FPU" : "");
        fflush(stdout);
    }
    if(!amiga_opt_preload) { return; }

    /* lista LBX w katalogu gry (biezacy katalog = tam, gdzie gra je otwiera) */
    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    kat = Lock((CONST_STRPTR)"", ACCESS_READ);
    if(fib == NULL || kat == 0 || !Examine(kat, fib))
    {
        if(kat != 0) { UnLock(kat); }
        if(fib != NULL) { FreeDosObject(DOS_FIB, fib); }
        printf("[amiga] preload: nie da sie przeczytac katalogu gry - wylaczony\n");
        return;
    }
    while(ExNext(kat, fib) && amiga_preload_ile < AMIGA_PRELOAD_MAX)
    {
        if(fib->fib_DirEntryType < 0 && Amiga_Preload_Lbx((const char *)fib->fib_FileName)
           && strlen((const char *)fib->fib_FileName) < sizeof(amiga_preload_nazwy[0]))
        {
            strcpy(amiga_preload_nazwy[amiga_preload_ile], (const char *)fib->fib_FileName);
            amiga_preload_rozm[amiga_preload_ile] = fib->fib_Size;
            suma += (unsigned long)fib->fib_Size;
            amiga_preload_ile++;
        }
    }
    UnLock(kat);
    FreeDosObject(DOS_FIB, fib);

    wolne = AvailMem(MEMF_FAST);
    if(amiga_preload_ile == 0 || wolne < suma + AMIGA_PRELOAD_ZAPAS)
    {
        printf("[amiga] preload: za malo FastRAM (wolne %lu KB, trzeba %lu KB) - wylaczony\n",
               wolne / 1024UL, (suma + AMIGA_PRELOAD_ZAPAS) / 1024UL);
        amiga_preload_ile = 0;
        return;
    }

    buf = (UBYTE *)AllocVec((ULONG)AMIGA_PRELOAD_BUF, MEMF_ANY);
    if(buf == NULL) { amiga_preload_ile = 0; return; }
    l = CreateDir((CONST_STRPTR)AMIGA_PRELOAD_KAT);
    if(l != 0) { UnLock(l); }

    DateStamp(&t0);
    atexit(Amiga_Preload_Sprzatnij);
    for(i = 0; i < amiga_preload_ile; i++)
    {
        if(!Amiga_Preload_Kopiuj(amiga_preload_nazwy[i], amiga_preload_rozm[i], buf))
        {
            printf("[amiga] preload: blad kopiowania %s - wylaczony\n", amiga_preload_nazwy[i]);
            FreeVec(buf);
            Amiga_Preload_Sprzatnij();
            amiga_preload_ile = 0;
            return;
        }
    }
    FreeVec(buf);
    DateStamp(&t1);
    ms = ((t1.ds_Days - t0.ds_Days) * 86400L * 1000L) + ((t1.ds_Minute - t0.ds_Minute) * 60000L)
         + ((t1.ds_Tick - t0.ds_Tick) * 20L);
    amiga_preload_otworz = Amiga_Preload_Otworz;
    printf("[amiga] preload: %d plikow LBX, %lu KB w %s, %ld ms (wolne FastRAM przedtem %lu KB)\n",
           amiga_preload_ile, suma / 1024UL, AMIGA_PRELOAD_KAT, ms, wolne / 1024UL);
    fflush(stdout);
}
