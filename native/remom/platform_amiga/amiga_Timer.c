/*
 * amiga_Timer.c - zegar platformy (Platform.h: "Timer").
 *
 * Wziete wprost z platform/headless/headless_Timer.c (galaz POSIX), bo ta
 * sciezka jest juz sprawdzona na Amidze: HeMoM przechodzi na niej ture AI.
 * clock_gettime(CLOCK_MONOTONIC) dziala w libnix; nanosleep() dostarcza
 * native/remom/amiga_posix.c (Delay() w tickach 1/50 s, krotsza pauza
 * zaokrugla sie w gore do jednego ticka).
 *
 * Swiadomie NIE amigagfx_millis(): ten liczy z DateStamp co 20 ms, a zegar
 * musi dzialac takze bez otwartego ekranu (REMOM_GFX=none) i przed nim.
 */

#include "Platform.h"

#include <time.h>
#include <unistd.h>

/* libnix nie deklaruje nanosleep - definicja w native/remom/amiga_posix.c */
int nanosleep(const struct timespec *req, struct timespec *rem);

/* native/remom/amiga_czas.c - E-clock i timer.device (kompilowany natywnie) */
unsigned long long Amiga_Czas_Us(void);
int Amiga_Czas_Dostepny(void);
void Amiga_Czas_Czekaj_Us(unsigned long us);

static uint64_t amiga_ticks_startup = 0;
static uint64_t amiga_micros_startup = 0;

static uint64_t Amiga_Get_Ticks_Us(void);

/* PRECYZJA (2026-09-17): clock_gettime z libnix ma rozdzielczosc 20 ms -
   klatka gry (55 ms) wychodzila 60 ms. E-clock daje mikrosekundy. */
static uint64_t Amiga_Get_Ticks_Ms(void)
{
    if(Amiga_Czas_Dostepny())
    {
        return (uint64_t)(Amiga_Czas_Us() / 1000ULL);
    }
    return Amiga_Get_Ticks_Us() / 1000ULL;
}

static uint64_t Amiga_Get_Ticks_Us_Libnix(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

static uint64_t Amiga_Get_Ticks_Us(void)
{
    if(Amiga_Czas_Dostepny())
    {
        return (uint64_t)Amiga_Czas_Us();
    }
    return Amiga_Get_Ticks_Us_Libnix();
}

void Platform_Sleep_Millies(uint64_t ms)
{
    struct timespec ts;
    /* timer.device UNIT_MICROHZ - 1 ms to 1 ms, nie caly VBlank */
    if(ms < 1000ULL && Amiga_Czas_Dostepny())
    {
        Amiga_Czas_Czekaj_Us((unsigned long)ms * 1000UL);
        return;
    }
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000);
    nanosleep(&ts, NULL);
}

uint64_t Platform_Get_Millies(void)
{
    if(amiga_ticks_startup == 0)
    {
        amiga_ticks_startup = Amiga_Get_Ticks_Ms();
    }
    return Amiga_Get_Ticks_Ms() - amiga_ticks_startup;
}

uint64_t Platform_Get_Micros(void)
{
    if(amiga_micros_startup == 0)
    {
        amiga_micros_startup = Amiga_Get_Ticks_Us();
    }
    return Amiga_Get_Ticks_Us() - amiga_micros_startup;
}

/* INT 1A,0 - licznik w tickach BIOS (~18,2 Hz), jak w headless. */
uint64_t Read_System_Clock_Timer(void)
{
    uint64_t ms = Amiga_Get_Ticks_Ms();
    return ms / 55;
}
