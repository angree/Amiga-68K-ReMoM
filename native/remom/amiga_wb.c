/*
 * amiga_wb.c - czekanie na Workbench przed otwarciem ekranu gry.
 *
 * DLACZEGO (developer, 2026-09-17): Workbench ma dzialac w tle - do
 * sprawdzenia zajetej pamieci i do ustawien systemu przez gadzet glebi.
 * Work:run robi teraz LoadWB przed gra, ale LoadWB wraca od razu, a
 * Workbench laduje sie w tle. Gdyby gra otworzyla ekran pierwsza, pozno
 * startujacy Workbench moglby wysunac swoj ekran przed gre. Czekamy wiec
 * (najwyzej max_ms) az proces Workbencha istnieje, i jeszcze chwile, zeby
 * zdazyl otworzyc okno tla.
 *
 * Kompilowany NATYWNIE (bez amiga_le.h).
 */
#include <exec/types.h>
#include <exec/ports.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdlib.h>

/* libnix: komunikat startowy Workbencha (NULL przy starcie z CLI) */
extern struct WBStartup * _WBenchMsg;

static BPTR amiga_wb_stary_katalog = 0;
static int amiga_wb_katalog_zmieniony = 0;

static void Amiga_WB_Koniec(void)
{
    if(amiga_wb_katalog_zmieniony)
    {
        CurrentDir(amiga_wb_stary_katalog);
        amiga_wb_katalog_zmieniony = 0;
    }
}

/* Start z ikony (argc == 0): katalog biezacy = katalog programu (dane gry,
   zapisy i opcje leza obok binarki, jak Work: przy starcie z CLI), a stdout
   - bez okna konsoli - do pliku mom-wb.log obok gry. Blokady nie zwalniamy:
   nalezy do Workbencha. */
void Amiga_WB_Start(int argc)
{
    if(argc != 0 || _WBenchMsg == NULL || _WBenchMsg->sm_NumArgs < 1)
    {
        return;
    }
    if(_WBenchMsg->sm_ArgList[0].wa_Lock != 0)
    {
        amiga_wb_stary_katalog = CurrentDir(_WBenchMsg->sm_ArgList[0].wa_Lock);
        amiga_wb_katalog_zmieniony = 1;
        atexit(Amiga_WB_Koniec);
    }
    freopen("mom-wb.log", "w", stdout);
    printf("[amiga] start z Workbencha: %s\n", (char *)_WBenchMsg->sm_ArgList[0].wa_Name);
    fflush(stdout);
}

static int Amiga_WB_Jest(void)
{
    int jest;
    Forbid();
    jest = (FindTask((CONST_STRPTR)"Workbench") != NULL) || (FindPort((CONST_STRPTR)"WORKBENCH") != NULL);
    Permit();
    return jest;
}

/* 1 = Workbench dziala (od razu albo po czekaniu), 0 = nie doczekalismy sie */
int Amiga_Czekaj_Na_Workbench(int max_ms)
{
    int czekano = 0;

    if(Amiga_WB_Jest())
    {
        return 1;
    }
    while(czekano < max_ms)
    {
        Delay(5);
        czekano += 100;
        if(Amiga_WB_Jest())
        {
            /* okno tla i ikony - zeby ekran Workbencha nie wyskoczyl pozniej */
            Delay(50);
            return 1;
        }
    }
    return 0;
}
