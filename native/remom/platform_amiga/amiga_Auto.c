/*
 * amiga_Auto.c - sterowanie gra od SRODKA emulowanej maszyny.
 *
 * DLACZEGO: syntetyczna mysz/klawiatura na hoscie jest zakazana (CLAUDE.md -
 * WinUAE po cichu gubi przechwycenie myszy i klikniecia trafiaja w to, co
 * developer ma na ekranie). Scenariusz .hms tez nie wystarcza: podaje
 * wejscie wprost silnikowi i OMIJA tlumaczenie zdarzen Intuition
 * (amiga_PFL.c ignoruje w nim mysz). Ten modul udaje IDCMP: czyta
 * Work:autoinput.txt i oddaje zdarzenia AmigaGfxEvent - ruch, przyciski,
 * surowe kody klawiszy - dokladnie ta sama sciezka, ktora ida zdarzenia
 * z prawdziwej myszy i klawiatury. Wzor: sdlmini_autoinput.c z portu
 * OpenXcom.
 *
 * WLACZENIE: tylko gdy istnieje Work:autoinput.on (sprawdzane raz).
 * Bez tego pliku modul nie zaglada nawet po skrypt - zostawiony skrypt nie
 * moze ruszac kursorem komus, kto gra.
 *
 * FORMAT (linia = polecenie, '#' komentarz, wspolrzedne gry 320x200):
 *     move X Y        ruch wskaznika
 *     click X Y       ruch, lewy przycisk w dol, po 120 ms w gore
 *     rclick X Y      to samo prawym
 *     key NAZWA       nacisniecie i puszczenie; NAZWA to jeden znak
 *                     (wielka litera = z Shiftem) albo: escape return enter
 *                     space tab backspace delete up down left right f1..f10
 *     type TEKST      znaki TEKSTU po kolei (reszta linii, ze spacjami)
 *     wait MS         przerwa przed nastepnym zdarzeniem
 *     snap NAZWA      zrzut najblizszej klatki do Work:NAZWA.ppm
 *     quit            zamkniecie jak przyciskiem okna
 * Plik jest wczytywany w calosci (host pisze go atomowo:
 * winuae/harness/autoinput.ps1) i KASOWANY po wykonaniu ostatniego
 * zdarzenia - host wie wtedy, ze moze wyslac nastepna porcje.
 * Kazde polecenie idzie do logu jako "[autoinput] ...".
 */

#include "Platform.h"

#include "../STU/src/STU_LOG.h"

#include "amiga_PFL.h"

/* patrz komentarz w amiga_Video.c: AmigaGfxEvent w kolejnosci natywnej */
#ifdef AMIGA_LE_H
#pragma scalar_storage_order default
#endif
#include "amiga_gfx.h"
#ifdef AMIGA_LE_H
#pragma scalar_storage_order little-endian
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AMIGA_AUTO_FILE     "PROGDIR:autoinput.txt"
#define AMIGA_AUTO_ON_FILE  "PROGDIR:autoinput.on"
#define AMIGA_AUTO_MAX_TEXT 8192
#define AMIGA_AUTO_MAX_STEP 1024

#define AMIGA_RAW_LSHIFT    0x60
#define AMIGA_RAW_UP        0x80

/* "snap NAZWA" - nie zdarzenie, tylko prosba o zrzut klatki (amiga_Video.c) */
#define AMIGA_AUTO_SNAP     100
#define AMIGA_AUTO_MAX_SNAP 64
static char amiga_auto_snap_name[AMIGA_AUTO_MAX_SNAP][24];
static int  amiga_auto_snap_count = 0;
extern char amiga_snap_request[40];

/* jedno zdarzenie i przerwa (ms) przed nim, liczona od poprzedniego */
static long amiga_auto_wait[AMIGA_AUTO_MAX_STEP];
static int  amiga_auto_type[AMIGA_AUTO_MAX_STEP];
static int  amiga_auto_x[AMIGA_AUTO_MAX_STEP];
static int  amiga_auto_y[AMIGA_AUTO_MAX_STEP];
static int  amiga_auto_code[AMIGA_AUTO_MAX_STEP];
/* numer linii skryptu - do logu przy pierwszym zdarzeniu z tej linii */
static int  amiga_auto_line[AMIGA_AUTO_MAX_STEP];
static int  amiga_auto_count = 0;
static int  amiga_auto_next = 0;
static long amiga_auto_pending_wait = 0;
static uint64_t amiga_auto_due = 0;
static uint64_t amiga_auto_last_poll = 0;
static int  amiga_auto_enabled = -1;
static int  amiga_auto_active = 0;

static char amiga_auto_text[AMIGA_AUTO_MAX_TEXT];
static char * amiga_auto_lines[AMIGA_AUTO_MAX_STEP];
static int  amiga_auto_line_count = 0;

/* Tablice ze wskaznikami musza byc w kolejnosci natywnej - GCC nie
   inicjalizuje statycznie wskaznikow w strukturach little-endian. */
#ifdef AMIGA_LE_H
#pragma scalar_storage_order default
#endif

/* Uklad US - te same rzedy, co w amiga_KD.c. */
static const struct
{
    int raw;
    const char * lower;
    const char * upper;
} amiga_auto_rows[] = {
    { 0x00, "`1234567890-=\\", "~!@#$%^&*()_+|" },
    { 0x10, "qwertyuiop[]",    "QWERTYUIOP{}" },
    { 0x20, "asdfghjkl;'",     "ASDFGHJKL:\"" },
    { 0x31, "zxcvbnm,./",      "ZXCVBNM<>?" },
};

/* znak -> surowy kod; *shift = 1, gdy trzeba trzymac Shift. -1 = brak */
static int Amiga_Auto_Char_Raw(char c, int * shift)
{
    int r;
    const char * p;

    *shift = 0;
    if(c == ' ')
    {
        return 0x40;
    }
    for(r = 0; r < (int)(sizeof(amiga_auto_rows) / sizeof(amiga_auto_rows[0])); r++)
    {
        p = strchr(amiga_auto_rows[r].lower, c);
        if(p != NULL && c != '\0')
        {
            return amiga_auto_rows[r].raw + (int)(p - amiga_auto_rows[r].lower);
        }
        p = strchr(amiga_auto_rows[r].upper, c);
        if(p != NULL && c != '\0')
        {
            *shift = 1;
            return amiga_auto_rows[r].raw + (int)(p - amiga_auto_rows[r].upper);
        }
    }
    return -1;
}

static int Amiga_Auto_Name_Raw(const char * name)
{
    static const struct { const char * n; int raw; } names[] = {
        { "escape", 0x45 }, { "esc", 0x45 }, { "return", 0x44 }, { "enter", 0x44 },
        { "space", 0x40 }, { "tab", 0x42 }, { "backspace", 0x41 }, { "delete", 0x46 },
        { "up", 0x4C }, { "down", 0x4D }, { "right", 0x4E }, { "left", 0x4F },
        { "f1", 0x50 }, { "f2", 0x51 }, { "f3", 0x52 }, { "f4", 0x53 }, { "f5", 0x54 },
        { "f6", 0x55 }, { "f7", 0x56 }, { "f8", 0x57 }, { "f9", 0x58 }, { "f10", 0x59 },
    };
    int i;
    for(i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
    {
        if(strcmp(names[i].n, name) == 0)
        {
            return names[i].raw;
        }
    }
    return -1;
}

#ifdef AMIGA_LE_H
#pragma scalar_storage_order little-endian
#endif

static void Amiga_Auto_Add(int line_no, long wait, int type, int x, int y, int code)
{
    if(amiga_auto_count >= AMIGA_AUTO_MAX_STEP)
    {
        return;
    }
    amiga_auto_wait[amiga_auto_count] = amiga_auto_pending_wait + wait;
    amiga_auto_type[amiga_auto_count] = type;
    amiga_auto_x[amiga_auto_count] = x;
    amiga_auto_y[amiga_auto_count] = y;
    amiga_auto_code[amiga_auto_count] = code;
    amiga_auto_line[amiga_auto_count] = line_no;
    amiga_auto_count++;
    amiga_auto_pending_wait = 0;
}

/* nacisniecie i puszczenie jednego klawisza, z Shiftem wokol, jesli trzeba */
static void Amiga_Auto_Add_Key(int line_no, int raw, int shift)
{
    if(shift)
    {
        Amiga_Auto_Add(line_no, 0, AMIGAGFX_EV_KEY, 0, 0, AMIGA_RAW_LSHIFT);
    }
    Amiga_Auto_Add(line_no, shift ? 30 : 0, AMIGAGFX_EV_KEY, 0, 0, raw);
    Amiga_Auto_Add(line_no, 80, AMIGAGFX_EV_KEY, 0, 0, raw | AMIGA_RAW_UP);
    if(shift)
    {
        Amiga_Auto_Add(line_no, 30, AMIGAGFX_EV_KEY, 0, 0, AMIGA_RAW_LSHIFT | AMIGA_RAW_UP);
    }
    amiga_auto_pending_wait += 60;
}

static void Amiga_Auto_Parse_Line(int line_no, char * line)
{
    char cmd[16];
    char arg1[32];
    int x;
    int y;
    int n;
    int raw;
    int shift;
    char * rest;

    while(*line == ' ' || *line == '\t')
    {
        line++;
    }
    if(*line == '\0' || *line == '#')
    {
        return;
    }
    n = sscanf(line, "%15s %31s %d", cmd, arg1, &y);

    if(strcmp(cmd, "wait") == 0 && n >= 2)
    {
        amiga_auto_pending_wait += atol(arg1);
        return;
    }
    if((strcmp(cmd, "move") == 0 || strcmp(cmd, "click") == 0 || strcmp(cmd, "rclick") == 0) && n >= 3)
    {
        int button = (cmd[0] == 'r') ? AMIGAGFX_BUTTON_RIGHT : AMIGAGFX_BUTTON_LEFT;
        x = atoi(arg1);
        Amiga_Auto_Add(line_no, 0, AMIGAGFX_EV_MOUSEMOVE, x, y, 0);
        if(cmd[0] != 'm')
        {
            Amiga_Auto_Add(line_no, 60, AMIGAGFX_EV_MOUSEDOWN, x, y, button);
            Amiga_Auto_Add(line_no, 120, AMIGAGFX_EV_MOUSEUP, x, y, button);
        }
        amiga_auto_pending_wait += 60;
        return;
    }
    if(strcmp(cmd, "key") == 0 && n >= 2)
    {
        raw = Amiga_Auto_Name_Raw(arg1);
        shift = 0;
        if(raw < 0 && arg1[0] != '\0' && arg1[1] == '\0')
        {
            raw = Amiga_Auto_Char_Raw(arg1[0], &shift);
        }
        if(raw < 0)
        {
            LOG_INFO(LOG_CAT_PFL, "[autoinput] line %d: unknown key \"%s\"", line_no, arg1);
            return;
        }
        Amiga_Auto_Add_Key(line_no, raw, shift);
        return;
    }
    if(strcmp(cmd, "type") == 0)
    {
        rest = strstr(line, "type") + 4;
        if(*rest == ' ')
        {
            rest++;
        }
        for(; *rest != '\0'; rest++)
        {
            raw = Amiga_Auto_Char_Raw(*rest, &shift);
            if(raw >= 0)
            {
                Amiga_Auto_Add_Key(line_no, raw, shift);
            }
        }
        return;
    }
    if(strcmp(cmd, "snap") == 0 && n >= 2 && amiga_auto_snap_count < AMIGA_AUTO_MAX_SNAP)
    {
        strncpy(amiga_auto_snap_name[amiga_auto_snap_count], arg1, 23);
        amiga_auto_snap_name[amiga_auto_snap_count][23] = '\0';
        Amiga_Auto_Add(line_no, 0, AMIGA_AUTO_SNAP, 0, 0, amiga_auto_snap_count);
        amiga_auto_snap_count++;
        return;
    }
    if(strcmp(cmd, "quit") == 0)
    {
        Amiga_Auto_Add(line_no, 0, AMIGAGFX_EV_QUIT, 0, 0, 0);
        return;
    }
    LOG_INFO(LOG_CAT_PFL, "[autoinput] line %d: unknown command \"%s\"", line_no, cmd);
}

static void Amiga_Auto_Load(void)
{
    FILE * f;
    size_t n;
    char * p;

    f = fopen(AMIGA_AUTO_FILE, "r");
    if(f == NULL)
    {
        return;
    }
    n = fread(amiga_auto_text, 1, AMIGA_AUTO_MAX_TEXT - 1, f);
    fclose(f);
    amiga_auto_text[n] = '\0';

    amiga_auto_count = 0;
    amiga_auto_snap_count = 0;
    amiga_auto_next = 0;
    amiga_auto_pending_wait = 0;
    amiga_auto_line_count = 0;

    p = amiga_auto_text;
    while(*p != '\0' && amiga_auto_line_count < AMIGA_AUTO_MAX_STEP)
    {
        char * nl = strchr(p, '\n');
        amiga_auto_lines[amiga_auto_line_count] = p;
        if(nl != NULL)
        {
            *nl = '\0';
            if(nl > p && nl[-1] == '\r')
            {
                nl[-1] = '\0';
            }
        }
        amiga_auto_line_count++;
        if(nl == NULL)
        {
            break;
        }
        p = nl + 1;
    }
    {
        int i;
        for(i = 0; i < amiga_auto_line_count; i++)
        {
            char kopia[256];
            strncpy(kopia, amiga_auto_lines[i], sizeof(kopia) - 1);
            kopia[sizeof(kopia) - 1] = '\0';
            Amiga_Auto_Parse_Line(i, kopia);
        }
    }

    LOG_INFO(LOG_CAT_PFL, "[autoinput] script loaded: %d lines, %d events", amiga_auto_line_count, amiga_auto_count);
    printf("[autoinput] script loaded: %d lines, %d events\n", amiga_auto_line_count, amiga_auto_count);
    fflush(stdout);

    if(amiga_auto_count == 0)
    {
        remove(AMIGA_AUTO_FILE);
        return;
    }
    amiga_auto_active = 1;
    amiga_auto_due = Platform_Get_Millies() + (uint64_t)amiga_auto_wait[0];
}

int Amiga_Autoinput_Next(AmigaGfxEvent * ev)
{
    uint64_t now;
    int i;

    if(amiga_auto_enabled < 0)
    {
        FILE * f = fopen(AMIGA_AUTO_ON_FILE, "r");
        amiga_auto_enabled = (f != NULL) ? 1 : 0;
        if(f != NULL)
        {
            fclose(f);
        }
        LOG_INFO(LOG_CAT_PFL, "[autoinput] %s", amiga_auto_enabled ? "ENABLED (Work:autoinput.on present)" : "disabled (no Work:autoinput.on)");
    }
    if(!amiga_auto_enabled)
    {
        return 0;
    }

    now = Platform_Get_Millies();

    if(!amiga_auto_active)
    {
        if(now - amiga_auto_last_poll < 500)
        {
            return 0;
        }
        amiga_auto_last_poll = now;
        Amiga_Auto_Load();
        if(!amiga_auto_active)
        {
            return 0;
        }
    }

    if(now < amiga_auto_due)
    {
        return 0;
    }

    i = amiga_auto_next;
    if(i == 0 || amiga_auto_line[i] != amiga_auto_line[i - 1])
    {
        LOG_INFO(LOG_CAT_PFL, "[autoinput] t=%lu %s", (unsigned long)now, amiga_auto_lines[amiga_auto_line[i]]);
        printf("[autoinput] t=%lu %s\n", (unsigned long)now, amiga_auto_lines[amiga_auto_line[i]]);
        fflush(stdout);
    }
    ev->type = amiga_auto_type[i];
    ev->x = amiga_auto_x[i];
    ev->y = amiga_auto_y[i];
    ev->code = amiga_auto_code[i];
    if(ev->type == AMIGA_AUTO_SNAP)
    {
        strncpy(amiga_snap_request, amiga_auto_snap_name[ev->code], sizeof(amiga_snap_request) - 1);
        amiga_snap_request[sizeof(amiga_snap_request) - 1] = '\0';
        ev->type = AMIGAGFX_EV_NONE;  /* dla amiga_PFL.c: nic do obslugi */
    }

    amiga_auto_next++;
    if(amiga_auto_next >= amiga_auto_count)
    {
        amiga_auto_active = 0;
        amiga_auto_last_poll = now;
        remove(AMIGA_AUTO_FILE);
        LOG_INFO(LOG_CAT_PFL, "[autoinput] script done, file removed");
        printf("[autoinput] script done, file removed\n");
        fflush(stdout);
    }
    else
    {
        amiga_auto_due = now + (uint64_t)amiga_auto_wait[amiga_auto_next];
    }
    return 1;
}
