/*
 * amiga_le.h - wymuszany (-include) przed KAZDYM plikiem ReMoM.
 *
 * DLACZEGO TEN PLIK ISTNIEJE
 *
 * ReMoM powstal dla x86 i czyta dane gry (SAVE.GAM, rekordy z LBX) wprost do
 * spakowanych struktur: stu_fread(_players, NUM_PLAYERS, 1224, f) i potem
 * _players[i].pole. Na 68k kazde pole wielobajtowe wyszloby odwrocone.
 *
 * Zamiast recznie odwracac setki pol, kazemy KOMPILATOROWI trzymac wszystkie
 * struktury silnika w kolejnosci little-endian (GCC >= 6,
 * #pragma scalar_storage_order). Kompilator sam zamienia bajty przy kazdym
 * odczycie i zapisie pola. Sprawdzone 2026-09-17 na emulowanym 68020
 * (mom-aga-nojit-040-40.uae): pola lezaly w pamieci jako 34 12 / 44 33 22 11,
 * odczyt wracal poprawnie, a surowe bajty 78 56 wczytane memcpy czytaly sie
 * jako 0x5678. Uklad pack(2) sie zgadzal (12 bajtow).
 *
 * Skutek uboczny wart odnotowania: zapisy gry sa BAJT W BAJT zgodne z PC.
 *
 * DWA OGRANICZENIA GCC, obslugiwane przez remom-patch.py:
 *  1. nie wolno wziac adresu pola skalarnego takiej struktury
 *     ("cannot take address of scalar with reverse storage order");
 *  2. struktury zainicjalizowanej statycznie WSKAZNIKIEM nie da sie wyemitowac
 *     ("initializer element is not constant") - relokacji linkera nie da sie
 *     odwrocic. Takie struktury to wewnetrzne tablice programu, nie dane
 *     z plikow, wiec wracaja do kolejnosci natywnej (AMIGA_NATIVE_ORDER).
 *
 * KOLEJNOSC MA ZNACZENIE: najpierw wszystkie naglowki systemowe, zeby ich
 * struktury (FILE!) zostaly big-endian - biblioteka libnix jest skompilowana
 * natywnie i czyta je natywnie. Dzieki straznikom #include ponowne wlaczenie
 * tych naglowkow w kodzie gry niczego juz nie deklaruje.
 */
#ifndef AMIGA_LE_H
#define AMIGA_LE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

/* Przechwytywanie wyjatkow CPU (native/amiga_trap.c, z portu OpenXcom).
   Wyjatek zamiast Guru daje raport z PC i rejestrami na stdout
   (Work:mom.log) - winuae/harness/trapmap.py mapuje go na symbole.
   Wolac w main(), ktorego ramka zyje do konca programu. */
#include "amiga_trap.h"
#define AMIGA_TRAP_ARM() do { \
    if (amiga_trap_arm()) { \
        static char amiga_trap_buf[2048]; \
        amiga_trap_describe(amiga_trap_buf, (int)sizeof(amiga_trap_buf)); \
        printf("%s\n", amiga_trap_buf); fflush(stdout); \
        exit(20); \
    } } while (0)

/* native/remom/amiga_banner.c - pierwsza linia wyjscia: znacznik buildu. */
void Amiga_Banner(const char *program);

/* native/remom/amiga_endian.c - odwraca `count` slow 16-bitowych w miejscu.
   Dla surowych (nie-strukturalnych) danych int16 z plikow: po wczytaniu
   i na czas zapisu. */
void Amiga_Swap16_Array(void *p, long count);
/* Diagnostyka: zrzut n bajtow na stdout. */
void Amiga_Hex(const char *tag, const void *p, int n);

/* Mapa swiata (_world_maps / p_world_map) lezy w pamieci NATYWNIE - patrz
   amiga_endian.c. Upstream siega do niej dwojako: natywnie przez
   p_world_map[wp][wy][wx] (373 miejsca) i makrem little-endian GET_2B_OFS
   (41 miejsc). Te drugie remom-patch.py przepina na ten dostep natywny.
   `ofs` to przesuniecie w BAJTACH, jak w oryginalnym wywolaniu. */
#define AMIGA_WM_GET(ofs) ((uint16_t)((int16_t *)_world_maps)[(ofs) / 2])

/* Znacznik postepu na stdout - wstawiany latkami w etapach startu. */
#define AMIGA_KROK(nazwa) do { printf("[amiga] %s\n", (nazwa)); fflush(stdout); } while (0)
/* To samo plus pierwsze 4 bajty wskazanej pamieci - do tropienia, kto ja nadpisuje. */
#define AMIGA_KROK_B(nazwa, p) do { unsigned char *amiga_kb_ = (unsigned char *)(p); \
    printf("[amiga] %s  @%p [%02X %02X %02X %02X]\n", (nazwa), (void *)amiga_kb_, \
           amiga_kb_[0], amiga_kb_[1], amiga_kb_[2], amiga_kb_[3]); fflush(stdout); } while (0)

/* sprintf() daje na tym libc bzdury (defekt toolchainu nr 2 z CLAUDE.md).
   snprintf jest sprawdzony - kazdy sprintf w silniku idzie przez niego. */
#undef sprintf
#define sprintf(buf, ...) snprintf((buf), 0x7FFFFFFF, __VA_ARGS__)

/* Adres pola struktury little-endian, liczony od adresu CALEJ struktury
   (ten jest dozwolony). Poprawny bez zastrzezen dla pol jednobajtowych
   i dla dostepu bajt po bajcie (bitmapy, Clear_Structure, porownanie z zerem):
   widzi dokladnie te same bajty, co kod na PC. NIE WOLNO czytac przez taki
   wskaznik natywnego int16/int32 - do tego sluza LE16_GET / LE16_SET. */
#define LE_FIELD_PTR(base_ptr, member) \
    ((void *)((unsigned char *)(base_ptr) + offsetof(__typeof__(*(base_ptr)), member)))

/* Tablica int16 lezaca w pamieci little-endian, adresowana wskaznikiem. */
#define LE16_GET(p, i) \
    ((int16_t)(uint16_t)(((unsigned char *)(p))[2 * (i)] | (((unsigned char *)(p))[2 * (i) + 1] << 8)))
#define LE16_SET(p, i, v) \
    do { int16_t le16_v_ = (int16_t)(v); \
         ((unsigned char *)(p))[2 * (i)]     = (unsigned char)(le16_v_ & 0xFF); \
         ((unsigned char *)(p))[2 * (i) + 1] = (unsigned char)((le16_v_ >> 8) & 0xFF); } while (0)

/* Struktury, ktore NIE sa danymi z plikow, wracaja do kolejnosci natywnej:
   AMIGA_NATIVE_ORDER przed deklaracja, AMIGA_LE_ORDER po niej. */
#define AMIGA_NATIVE_ORDER _Pragma("scalar_storage_order default")
#define AMIGA_LE_ORDER     _Pragma("scalar_storage_order little-endian")

#pragma scalar_storage_order little-endian

#endif /* AMIGA_LE_H */
