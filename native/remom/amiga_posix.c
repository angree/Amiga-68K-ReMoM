/*
 * amiga_posix.c - funkcje POSIX, ktorych ReMoM uzywa, a libnix nie ma.
 *
 * Tylko to, czego zazadal linker - kazda pozycja ponizej ma w komentarzu,
 * kto jej potrzebuje. Nic na zapas.
 */
#include <time.h>
#include <proto/dos.h>

/* platform/headless/headless_Timer.c: Platform_Sleep_Millies().
   dos.library Delay() liczy w tickach 1/50 s; krotsza pauza niz tick
   zaokrugla sie w gore do jednego ticka, zeby petla oczekiwania nie
   zamienila sie w aktywne czekanie. */
int nanosleep(const struct timespec *req, struct timespec *rem)
{
	unsigned long ms;
	long ticks;

	if (rem) {
		rem->tv_sec = 0;
		rem->tv_nsec = 0;
	}
	if (!req) {
		return 0;
	}
	ms = (unsigned long)req->tv_sec * 1000UL + (unsigned long)(req->tv_nsec / 1000000L);
	ticks = (long)((ms + 19UL) / 20UL);
	if (ticks > 0) {
		Delay(ticks);
	}
	return 0;
}
