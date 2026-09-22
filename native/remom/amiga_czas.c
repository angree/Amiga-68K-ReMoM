/*
 * amiga_czas.c - precyzyjny zegar i precyzyjne czekanie dla platformy ReMoM.
 *
 * DLACZEGO (profil 2026-09-17, mom-graj.uae): gra odmierza klatke zegarem
 * DOS - Release_Time() czeka do mark + 55 ms, pompujac zdarzenia i spiac
 * po 1 ms. clock_gettime() z libnix i Delay() maja rozdzielczosc VBlanku
 * (20 ms), wiec kazda klatka trwala 60 ms (PERF-LIVE: wszystkie czasy
 * klatek byly wielokrotnoscia 20), a CPU na mapie stal bezczynnie 66% czasu.
 *
 *  - zegar: ReadEClock() (CIA E-clock, 709 379 Hz PAL) - jedno wywolanie
 *    biblioteki, bez przelaczania zadan (jak native/amiga_uclock.c
 *    z portu OpenXcom);
 *  - czekanie: timer.device UNIT_MICROHZ, TR_ADDREQUEST przez DoIO -
 *    zadanie spi (procesor wolny), budzi sie z dokladnoscia CIA.
 *
 * Kompilowany NATYWNIE (bez amiga_le.h) - struktury systemu w kolejnosci
 * 68k. Gdy timer.device sie nie otworzy, wszystko wraca do DateStamp/Delay.
 */
#include <exec/types.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>
#include <proto/dos.h>

#include <stdlib.h>

struct Device *TimerBase = NULL;

static struct timerequest czas_eclock;
static struct timerequest czas_czekaj;
static struct MsgPort * czas_port = NULL;
static int czas_stan = 0;          /* 0 nie probowano, 1 otwarte, -1 brak */
static int czas_czekaj_stan = 0;
static ULONG czas_freq = 709379UL;
static ULONG czas_ostatni_lo = 0;
static unsigned long long czas_suma = 0;

void Amiga_Czas_Zamknij(void);

static int Czas_Otworz(void)
{
    struct EClockVal ev;
    if(czas_stan != 0)
    {
        return czas_stan > 0;
    }
    if(OpenDevice((CONST_STRPTR)"timer.device", UNIT_ECLOCK, (struct IORequest *)&czas_eclock, 0) != 0)
    {
        czas_stan = -1;
        return 0;
    }
    TimerBase = czas_eclock.tr_node.io_Device;
    czas_freq = ReadEClock(&ev);
    if(czas_freq == 0)
    {
        czas_freq = 709379UL;
    }
    czas_ostatni_lo = ev.ev_lo;
    czas_suma = 0;
    czas_stan = 1;
    atexit(Amiga_Czas_Zamknij);
    return 1;
}

/* mikrosekundy od pierwszego wywolania (0 = brak E-clock) */
unsigned long long Amiga_Czas_Us(void)
{
    struct EClockVal ev;
    ULONG d;
    unsigned long long sek;
    unsigned long long reszta;

    if(!Czas_Otworz())
    {
        return 0;
    }
    ReadEClock(&ev);
    d = ev.ev_lo - czas_ostatni_lo;     /* modulo 2^32 - przezywa zawiniecie */
    czas_ostatni_lo = ev.ev_lo;
    czas_suma += d;
    /* bez mnozenia calej sumy przez 10^6 - to przepelnia sie po ~7 h */
    sek = czas_suma / czas_freq;
    reszta = czas_suma - sek * czas_freq;
    /* +1 s: Platform_Get_Millies() traktuje 0 jako "punkt startu nieustawiony" */
    return 1000000ULL + sek * 1000000ULL + (reszta * 1000000ULL) / czas_freq;
}

int Amiga_Czas_Dostepny(void)
{
    return Czas_Otworz();
}

static int Czas_Czekaj_Otworz(void)
{
    if(czas_czekaj_stan != 0)
    {
        return czas_czekaj_stan > 0;
    }
    czas_czekaj_stan = -1;
    czas_port = CreateMsgPort();
    if(czas_port == NULL)
    {
        return 0;
    }
    czas_czekaj.tr_node.io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    czas_czekaj.tr_node.io_Message.mn_ReplyPort = czas_port;
    czas_czekaj.tr_node.io_Message.mn_Length = sizeof(czas_czekaj);
    if(OpenDevice((CONST_STRPTR)"timer.device", UNIT_MICROHZ, (struct IORequest *)&czas_czekaj, 0) != 0)
    {
        DeleteMsgPort(czas_port);
        czas_port = NULL;
        return 0;
    }
    czas_czekaj_stan = 1;
    return 1;
}

/* spi zadanie przez us mikrosekund; bez timer.device - Delay w tickach */
void Amiga_Czas_Czekaj_Us(unsigned long us)
{
    if(us == 0)
    {
        return;
    }
    if(!Czas_Czekaj_Otworz())
    {
        Delay((LONG)((us + 19999UL) / 20000UL));
        return;
    }
    czas_czekaj.tr_node.io_Command = TR_ADDREQUEST;
    czas_czekaj.tr_time.tv_secs = us / 1000000UL;
    czas_czekaj.tr_time.tv_micro = us % 1000000UL;
    DoIO((struct IORequest *)&czas_czekaj);
}

void Amiga_Czas_Zamknij(void)
{
    if(czas_czekaj_stan > 0)
    {
        CloseDevice((struct IORequest *)&czas_czekaj);
        DeleteMsgPort(czas_port);
        czas_port = NULL;
        czas_czekaj_stan = -1;  /* po zamknieciu juz nie otwierac (atexit) */
    }
    if(czas_stan > 0)
    {
        CloseDevice((struct IORequest *)&czas_eclock);
        TimerBase = NULL;
        czas_stan = -1;
    }
}
