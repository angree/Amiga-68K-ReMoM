/*
 * amiga_PFL.h - wewnetrzny naglowek backendu AmigaOS dla ReMoM.
 *
 * Backend implementuje caly interfejs platform/include/Platform.h na bazie
 * backendu headless (sciezka scenariusza/replay bez zmian) oraz warstwy
 * obrazu native/amiga_gfx.c (AGA c2p / RTG / okno na Workbenchu).
 *
 * Ten naglowek NIE wciaga naglowkow systemu Amigi - rozmowa z systemem idzie
 * wylacznie przez amiga_gfx.h (bez naglowkow systemowych) i amiga_Req.c.
 */
#ifndef AMIGA_PFL_H
#define AMIGA_PFL_H

#include <stdint.h>

/* Czy ekran/okno jest otwarte. 0 = tryb bez ekranu (REMOM_GFX=none). */
extern int amiga_pfl_display_open;

/* Nastepne Platform_Video_Update() przepisze i wyswietli CALY obraz,
   zamiast szukac zmienionych wierszy (po otwarciu ekranu, po zmianie
   rozmiaru okna). */
void Amiga_PFL_Force_Full_Present(void);

/* Kopiuje prostokat (wspolrzedne gry) ze strony widocznej do bufora chunky
   i wypycha go na ekran. Uzywane przez kursor - zamiast calej klatki. */
void Amiga_PFL_Present_Rect(int x, int y, int w, int h);

/* amiga_KD.c - klawiatura. */
void Amiga_Keyboard_Init(void);
/* Jeden surowy kod klawisza z IDCMP_RAWKEY (bit 7 = puszczenie). */
void Amiga_Keyboard_Raw_Event(int raw_code);

/* amiga_Req.c - systemowy requester (EasyRequest) na ekranie Workbencha. */
void Amiga_Req_Error(const char * title, const char * message);

/* Wybor backendu obrazu przed Startup_Platform(), np. z latki na argument
   "--gfx=rtg" w ReMoM.c. Nazwy: "aga", "rtg", "wb" (okno), "none".
   NULL = wroc do zmiennej srodowiskowej REMOM_GFX. */
void Amiga_Platform_Set_Backend(const char * name);

/* amiga_Opcje.c - opcje Amigi (Work:amiga.cfg) i ekran "Amiga Options". */
extern int amiga_opt_kursor;   /* 1 = wskaznik systemowy */
extern int amiga_opt_pasek;    /* 1 = pasek tytulowy ekranu */
extern int amiga_opt_wideo;    /* 0 auto, 1 PAL, 2 NTSC */
extern int amiga_opt_grafika;  /* 0 jak binarka, 1 AGA, 2 RTG, 3 okno */
extern int amiga_opt_muzyka;   /* 1 = muzyka */
extern int amiga_opt_fps;      /* 1 = fps i czas klatki na pasku ekranu */
void Amiga_Opcje_Wczytaj(void);
void Amiga_Opcje_Ekran(void);
void Amiga_Rysuj_Napis_Menu(const char * tekst, int x, int y, int podswietlony);

/* amiga_PFL.c */
extern int amiga_pfl_backend;
void Amiga_PFL_Ustaw_Kursor(void);
void Amiga_PFL_Przeotworz_Ekran(void);

/* native/remom/amiga_wb.c (natywnie): 1 = Workbench dziala */
int Amiga_Czekaj_Na_Workbench(int max_ms);

#endif /* AMIGA_PFL_H */
