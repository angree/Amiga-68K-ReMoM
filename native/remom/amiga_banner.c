/*
 * amiga_banner.c - pierwsza linia wyjscia kazdego programu: znacznik buildu.
 *
 * "Czy uruchamiam to, co wlasnie zbudowalem?" ma sie dac rozstrzygnac
 * z logu, nigdy z zalozenia - na tym udziale SMB edycja pliku nie podbija
 * mtime widzianego przez WSL, a log starej binarki wyglada zupelnie zdrowo.
 *
 * build.sh kasuje obiekt tego pliku przed kazdym buildem, wiec __DATE__ /
 * __TIME__ oznaczaja chwile LINKOWANIA, a nie ostatniej kompilacji tego
 * jednego pliku (to byla pulapka starego buildu: znacznik siedzial
 * w lbxdump.c i klamal, gdy przekompilowal sie inny plik).
 *
 * Przy okazji sprawdza zegar, na ktorym stoi cala platforma bezekranowa:
 * clock_gettime(CLOCK_MONOTONIC) przed i po 0,2 s. Jesli nie rosnie, kazda
 * petla "czekaj N ms" w silniku kreci sie w nieskonczonosc.
 */
#include <stdio.h>
#include <time.h>
#include <proto/dos.h>

void Amiga_Banner(const char *program)
{
	struct timespec a, b;
	int ra, rb;

	printf("Ami MoM - %s (ReMoM a9cc082, AmigaOS 68k)\n", program);
	printf("built: %s %s\n\n", __DATE__, __TIME__);

	a.tv_sec = b.tv_sec = -1;
	a.tv_nsec = b.tv_nsec = -1;
	ra = clock_gettime(CLOCK_MONOTONIC, &a);
	Delay(10);
	rb = clock_gettime(CLOCK_MONOTONIC, &b);
	printf("[amiga] zegar MONOTONIC: rc=%d %ld.%09ld -> rc=%d %ld.%09ld\n",
	       ra, (long)a.tv_sec, (long)a.tv_nsec, rb, (long)b.tv_sec, (long)b.tv_nsec);
	fflush(stdout);
}
