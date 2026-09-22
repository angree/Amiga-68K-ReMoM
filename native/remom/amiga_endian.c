/*
 * amiga_endian.c - zamiana kolejnosci bajtow dla SUROWYCH danych z plikow.
 *
 * Struktury silnika leza w pamieci little-endian (amiga_le.h), wiec ich nie
 * dotyczy. Ten plik jest dla tego, co pragma nie obejmuje: zwyklych zmiennych
 * i tablic int16 wczytywanych fread-em (liczniki z SAVE.GAM, mapa swiata).
 * Takie dane trzymamy w pamieci NATYWNIE, bo kod czyta je natywnie
 * (p_world_map[wp][wy][wx] - 373 miejsca), a zamieniamy bajty tylko na
 * granicy pliku: po wczytaniu i na czas zapisu.
 *
 * Objaw, ktory to wykryl (2026-09-17): "[_unit ASSIGN] 1280 -> 0" w logu -
 * 1280 = 0x0500, czyli odwrocone 5 - i zaraz potem asercja
 * itr_units < MAX_UNIT_COUNT w Next_Unit_Nearest_Available.
 */

void Amiga_Swap16_Array(void *p, long count)
{
	unsigned char *b = (unsigned char *)p;
	unsigned char t;

	while (count-- > 0) {
		t = b[0];
		b[0] = b[1];
		b[1] = t;
		b += 2;
	}
}

/* Diagnostyka: "[amiga] <tag> @adres: xx xx xx ..." na stdout. */
#include <stdio.h>
void Amiga_Hex(const char *tag, const void *p, int n)
{
	const unsigned char *b = (const unsigned char *)p;
	int i;

	printf("[amiga] %s @%p:", tag, p);
	for (i = 0; i < n; i++) {
		printf("%s%02X", (i % 4) ? "" : " ", b[i]);
	}
	printf("\n");
	fflush(stdout);
}
