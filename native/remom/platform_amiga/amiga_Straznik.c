/* amiga_Straznik.c - straznik zwisu (Ami MoM 0.7.6).
 *
 * Gracz: "gra sie zawiesila" - a w logu nic: ostatnia linia mowi tylko, w
 * jaki ekran gra weszla. Wywrotke lapie amiga_trap.c (CPU TRAP z PC), zwisu
 * nie lapalo nic.
 *
 * Dwie czesci:
 *  1. Serwer przerwania VBlank (50 Hz): gdy przerwany zostal glowny proces
 *     gry, zapamietuje jego PC w pierscieniu ostatnich 64 probek. Ramke
 *     wyjatku znajduje tak samo jak profiler (native/remom/amiga_profil.c).
 *  2. Osobny proces "Master of Magic watchdog": co sekunde sprawdza puls
 *     glownej petli (amiga_puls, Amiga_Drain_Events w amiga_PFL.c). Gdy puls
 *     stoi STRAZNIK_PROG sekund, zapisuje do mom-zwis.log (obok gry) faze gry
 *     (amiga_faza, amiga_trap.c) i probki PC w formacie profil.txt - host
 *     mapuje je na funkcje: winuae/harness/profil.py mom-zwis.log <binarka>.
 *     Gdy puls wroci, dopisuje "petla ruszyla po N s" (wtedy to byla tylko
 *     dluga tura AI albo wczytywanie, nie zwis).
 *
 * Plik pisze proces straznika przez dos.library (Open/Write) - NIE przez
 * stdio libnix, ktore nalezy do procesu gry. */

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/interrupts.h>
#include <hardware/intbits.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRAZNIK_PROG   30      /* sekund bez pulsu = zwis */
#define STRAZNIK_PROBEK 64

volatile unsigned long amiga_puls = 0;               /* ++ w kazdym obiegu petli zdarzen */
extern const char * volatile amiga_faza;     /* native/amiga_trap.c */
extern void Amiga_Profil_Start(void);        /* baza adresow dla profil.py */

static struct Task * straznik_gra = NULL;
static ULONG straznik_pc[STRAZNIK_PROBEK];
static volatile ULONG straznik_n = 0;
static volatile ULONG straznik_n_puls = 0;   /* straznik_n przy ostatnim pulsie */
static struct Interrupt straznik_int;
static int straznik_int_jest = 0;
static volatile int straznik_zyje = 0;
static volatile int straznik_stop = 0;
static struct Task * straznik_rodzic = NULL;

/* z przerwania: d0/d1/a0/a1 wolno niszczyc; zwraca 0 */
static ULONG Straznik_Serwer(void)
{
    UBYTE lokalna;
    UBYTE * sp = (UBYTE *)((ULONG)&lokalna & ~1UL);
    int i;
    if(SysBase->ThisTask != straznik_gra)
    {
        return 0;
    }
    for(i = 0; i < 400; i += 2)
    {
        UBYTE * f = sp + i;
        if(f[6] == 0x00 && f[7] == 0x6C)   /* ramka: SR(2) PC(4) format/wektor 0x006C */
        {
            UWORD sr = (UWORD)((f[0] << 8) | f[1]);
            if((sr & 0x0700) < 0x0300)
            {
                straznik_pc[straznik_n % STRAZNIK_PROBEK] =
                    ((ULONG)f[2] << 24) | ((ULONG)f[3] << 16) | ((ULONG)f[4] << 8) | f[5];
                straznik_n++;
                return 0;
            }
        }
    }
    return 0;
}

static void Straznik_Pisz(BPTR fh, const char * s)
{
    Write(fh, (APTR)s, (LONG)strlen(s));
}

static void Straznik_Raport(ULONG sekund)
{
    char b[96];
    BPTR fh;
    ULONG n = straznik_n, i, od;
    const char * faza = amiga_faza;
    fh = Open((CONST_STRPTR)"mom-zwis.log", MODE_NEWFILE);
    if(fh == 0)
    {
        return;
    }
    /* naglowek w formacie profil.txt (profil.py czyta 1. linie jako baze) */
    snprintf(b, sizeof(b), "baza Amiga_Profil_Start 0x%08lx\n", (unsigned long)(ULONG)Amiga_Profil_Start);
    Straznik_Pisz(fh, b);
    od = (n > STRAZNIK_PROBEK) ? (n - STRAZNIK_PROBEK) : 0;
    snprintf(b, sizeof(b), "probek %lu pominietych 0\n", (unsigned long)(n - od));
    Straznik_Pisz(fh, b);
    for(i = od; i < n; i++)
    {
        snprintf(b, sizeof(b), "%08lx\n", (unsigned long)straznik_pc[i % STRAZNIK_PROBEK]);
        Straznik_Pisz(fh, b);
    }
    snprintf(b, sizeof(b), "# probek PC od zatrzymania petli: %lu (0 = gra czeka - Wait/semafor, nie kreci sie)\n", (unsigned long)(n - straznik_n_puls));
    Straznik_Pisz(fh, b);
    snprintf(b, sizeof(b), "# ZWIS? petla gry stoi od %lu s, faza gry: ", (unsigned long)sekund);
    Straznik_Pisz(fh, b);
    Straznik_Pisz(fh, (faza != NULL) ? faza : "?");
    Straznik_Pisz(fh, "\n# PC (ostatnie probki VBlank glownego procesu) -> winuae/harness/profil.py mom-zwis.log remom\n");
    Close(fh);
}

static void Straznik_Dopisz(const char * s)
{
    BPTR fh = Open((CONST_STRPTR)"mom-zwis.log", MODE_READWRITE);
    if(fh == 0)
    {
        return;
    }
    Seek(fh, 0, OFFSET_END);
    Straznik_Pisz(fh, s);
    Close(fh);
}

static void Straznik_Petla(void)
{
    ULONG ostatni = amiga_puls, stoi = 0;
    int zgloszony = 0;
    char b[80];
    while(!straznik_stop)
    {
        Delay(50);   /* 1 s */
        if(amiga_puls != ostatni)
        {
            if(zgloszony)
            {
                snprintf(b, sizeof(b), "# petla gry ruszyla po %lu s (to nie byl zwis)\n", (unsigned long)stoi);
                Straznik_Dopisz(b);
                zgloszony = 0;
            }
            ostatni = amiga_puls;
            straznik_n_puls = straznik_n;
            stoi = 0;
            continue;
        }
        stoi++;
        if(stoi >= STRAZNIK_PROG && !zgloszony)
        {
            Straznik_Raport(stoi);
            zgloszony = 1;
        }
    }
    Forbid();   /* do konca procesu - rodzic nie zwolni kodu przed nami */
    straznik_zyje = 0;
    Signal(straznik_rodzic, SIGBREAKF_CTRL_F);
}

void Amiga_Straznik_Stop(void)
{
    if(straznik_zyje)
    {
        straznik_stop = 1;
        while(straznik_zyje)
        {
            Wait(SIGBREAKF_CTRL_F);
        }
    }
    if(straznik_int_jest)
    {
        RemIntServer(INTB_VERTB, &straznik_int);
        straznik_int_jest = 0;
    }
}

void Amiga_Straznik_Start(void)
{
    struct Process * pr;
    if(straznik_zyje || straznik_int_jest)
    {
        return;
    }
    straznik_gra = FindTask(NULL);
    straznik_rodzic = straznik_gra;
    straznik_int.is_Node.ln_Type = NT_INTERRUPT;
    straznik_int.is_Node.ln_Pri = -61;
    straznik_int.is_Node.ln_Name = (char *)"remom-straznik";
    straznik_int.is_Data = NULL;
    straznik_int.is_Code = (VOID (*)())Straznik_Serwer;
    AddIntServer(INTB_VERTB, &straznik_int);
    straznik_int_jest = 1;
    SetSignal(0, SIGBREAKF_CTRL_F);
    straznik_stop = 0;
    straznik_zyje = 1;
    pr = CreateNewProcTags(NP_Entry, (ULONG)Straznik_Petla,
                           NP_Name, (ULONG)"Master of Magic watchdog",
                           NP_Priority, (LONG)(straznik_gra->tc_Node.ln_Pri + 1),
                           NP_StackSize, 8192UL,
                           TAG_DONE);
    if(pr == NULL)
    {
        straznik_zyje = 0;
    }
    atexit(Amiga_Straznik_Stop);
    printf("[amiga] straznik zwisu: %s (raport po %d s bez petli gry -> mom-zwis.log)\n",
           straznik_zyje ? "dziala" : "NIE ruszyl", STRAZNIK_PROG);
    fflush(stdout);
}
