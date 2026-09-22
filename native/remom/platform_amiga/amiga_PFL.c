/*
 * amiga_PFL.c - backend AmigaOS: cykl zycia, zdarzenia, mysz.
 *
 * Baza: platform/headless/headless_PFL.c (sciezka REPLAY i SCENARIUSZ .hms
 * bez zmian - na niej stoi testowanie bez czlowieka) + zachowanie myszy
 * i zdarzen z platform/sdl2/sdl2_PFL.c / sdl2_MD.c, z SDL zamienionym na
 * native/amiga_gfx.c.
 *
 * WYBOR EKRANU (Startup_Platform):
 *   zmienna REMOM_GFX - LOKALNA zmienna powloki (AmigaDOS: Set REMOM_GFX rtg;
 *   getenv() z libnix NIE widzi zmiennych globalnych z SetEnv, sprawdzone
 *   2026-09-17):
 *     "aga" (domyslnie) - ekran 320x200 lores, 8 bitplanow, c2p;
 *     "rtg"             - 8-bitowy ekran CyberGraphX/Picasso96; gdy tryb
 *                         320x200 nie istnieje, amiga_gfx.c sam wraca do AGA
 *                         (jedna linia w logu mowi dlaczego);
 *     "wb" / "window"   - okno na Workbenchu (paleta negocjowana);
 *     "none"            - bez ekranu, jak headless (testy).
 *   SDL_VIDEODRIVER=dummy/offscreen (to ustawia --headless w ReMoM.c) tez
 *   daje "none". Latka w ReMoM.c moze wybrac backend wprost:
 *   Amiga_Platform_Set_Backend() - wygrywa ze zmienna.
 *
 * KURSOR: programowy, jak domyslnie w SDL2 (Platform_HW_Cursor_Active() = 0):
 * MoX rysuje go do strony, systemowy wskaznik Intuition jest ukryty.
 * Ruch kursora w petlach oczekiwania (Release_Time -> Platform_Pump_Events)
 * wypycha na ekran tylko dwa prostokaty 16x16 (stary i nowy), nie cala
 * klatke jak SDL2 - na AGA to roznica jednego c2p 320x200 na kazdy ruch.
 *
 * TRYB SCENARIUSZA (--scenario, zarejestrowany frame callback): prawdziwa
 * mysz jest IGNOROWANA (ruch i przyciski), a Platform_Warp_Mouse nie rusza
 * wskaznika systemowego. W SDL2 prawdziwa mysz nadpisywala pointer_x/y
 * (komentarz w Artificial_Human_Player.c) - tu przebieg ma byc powtarzalny
 * niezaleznie od tego, czy ktos poruszy mysza nad oknem WinUAE.
 * Klawiatura dziala dalej (Ctrl+Q konczy przebieg).
 *
 * KOLEJKA: IDCMP oprozniamy przy kazdym Pump/Event_Handler (Intuition
 * wysyla IntuiTicks co 0,1 s - nieodbierany port by rosl). Ruch myszy
 * i stan przyciskow aktualizuja sie od razu (odpowiednik
 * SDL_GetMouseState po SDL_PumpEvents), a nacisniecia przyciskow, klawisze
 * i zamkniecie okna czekaja w kolejce na Platform_Event_Handler().
 *
 * LOG MYSZY: build ma -DMOUSE_DEBUG (znaczniki etapow na stdout). Linie
 * z kazdego wywolania Event_Handler i kazdego ruchu (HANDLER_START, POLL,
 * MAYBE_MOVE - w SDL2 sa pod samym MOUSE_DEBUG) zalalyby Work:mom.log,
 * wiec tu wymagaja dodatkowo -DAMIGA_MOUSE_TRACE. BTN_DOWN zostaje.
 */

#include "Platform.h"
#include "Platform_Keys.h"
#include "Platform_Replay.h"
#include "Platform_Perf.h"

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

#define AMIGA_CURSOR_DIM   16      /* MOM_DEF.h CURSOR_WIDTH / CURSOR_HEIGHT */
#define AMIGA_WARP_GUARD_MS 100    /* jak Platform_Set_Warp_Guard() w SDL2 */

#define AMIGA_TYTUL "Master of Magic"   /* pasek ekranu i okno */

#define AMIGA_GFX_DEFAULT  (-2)
#define AMIGA_GFX_NONE     (-1)

int amiga_pfl_display_open = 0;
int amiga_pfl_backend = 0;   /* backend z Startup_Platform - do ponownego otwarcia */
extern int amiga_domyslny_backend;   /* amiga_Domyslny_aga.c / amiga_Domyslny_rtg.c */

static int amiga_backend_request = AMIGA_GFX_DEFAULT;
static int amiga_atexit_registered = 0;

/* stan "sprzetowej" myszy - pozycja w pikselach gry, -1 = jeszcze nieznana */
static int amiga_hw_x = -1;
static int amiga_hw_y = -1;
static int amiga_hw_left = 0;
static int amiga_hw_right = 0;
static uint64_t amiga_warp_guard_until = 0;



/* ========================================================================= */
/*  Frame callback (scenariusz .hms) - jak headless                          */
/* ========================================================================= */

static void (*platform_frame_callback)(void) = NULL;

void Platform_Register_Frame_Callback(void (*callback)(void))
{
    platform_frame_callback = callback;
}

static int Amiga_Scenario_Mode(void)
{
    return platform_frame_callback != NULL;
}



/* ========================================================================= */
/*  Kolejka zdarzen                                                          */
/* ========================================================================= */

#define AMIGA_EVQ_LEN 64

static int amiga_evq_type[AMIGA_EVQ_LEN];
static int amiga_evq_x[AMIGA_EVQ_LEN];
static int amiga_evq_y[AMIGA_EVQ_LEN];
static int amiga_evq_code[AMIGA_EVQ_LEN];
static int amiga_evq_head = 0;   /* nastepny do odczytu */
static int amiga_evq_tail = 0;   /* nastepny do zapisu */

static void Amiga_Evq_Push(int type, int x, int y, int code)
{
    int next = (amiga_evq_tail + 1) % AMIGA_EVQ_LEN;
    if(next == amiga_evq_head)
    {
        return;   /* pelna - nowe zdarzenie przepada (jak nadmiar bufora BIOS) */
    }
    amiga_evq_type[amiga_evq_tail] = type;
    amiga_evq_x[amiga_evq_tail] = x;
    amiga_evq_y[amiga_evq_tail] = y;
    amiga_evq_code[amiga_evq_tail] = code;
    amiga_evq_tail = next;
}

static int Amiga_Evq_Pop(int * type, int * x, int * y, int * code)
{
    if(amiga_evq_head == amiga_evq_tail)
    {
        return 0;
    }
    *type = amiga_evq_type[amiga_evq_head];
    *x = amiga_evq_x[amiga_evq_head];
    *y = amiga_evq_y[amiga_evq_head];
    *code = amiga_evq_code[amiga_evq_head];
    amiga_evq_head = (amiga_evq_head + 1) % AMIGA_EVQ_LEN;
    return 1;
}

static void Amiga_Evq_Clear(void)
{
    amiga_evq_head = amiga_evq_tail = 0;
}

static int Amiga_Clamp(int v, int lo, int hi)
{
    if(v < lo) { return lo; }
    if(v > hi) { return hi; }
    return v;
}

/* amiga_Auto.c - zdarzenia z Work:autoinput.txt, w tym samym formacie co IDCMP */
int Amiga_Autoinput_Next(AmigaGfxEvent * ev);
/* amiga_Audio.c - muzyka strumieniowana z dysku */
void Amiga_Audio_Service(void);

static void Amiga_Handle_Gfx_Event(const AmigaGfxEvent * evp, int ignore_mouse)
{
    AmigaGfxEvent ev = *evp;
    {
        switch(ev.type)
        {
            case AMIGAGFX_EV_MOUSEMOVE:
            {
                if(ignore_mouse) { break; }
                if(Platform_Get_Millies() < amiga_warp_guard_until) { break; }
                amiga_hw_x = Amiga_Clamp(ev.x, PLATFORM_SCREEN_XMIN, PLATFORM_SCREEN_XMAX);
                amiga_hw_y = Amiga_Clamp(ev.y, PLATFORM_SCREEN_YMIN, PLATFORM_SCREEN_YMAX);
            } break;

            case AMIGAGFX_EV_MOUSEDOWN:
            case AMIGAGFX_EV_MOUSEUP:
            {
                int down = (ev.type == AMIGAGFX_EV_MOUSEDOWN);
                if(ignore_mouse) { break; }
                if(ev.code == AMIGAGFX_BUTTON_LEFT)  { amiga_hw_left = down; }
                if(ev.code == AMIGAGFX_BUTTON_RIGHT) { amiga_hw_right = down; }
                if(Platform_Get_Millies() >= amiga_warp_guard_until)
                {
                    amiga_hw_x = Amiga_Clamp(ev.x, PLATFORM_SCREEN_XMIN, PLATFORM_SCREEN_XMAX);
                    amiga_hw_y = Amiga_Clamp(ev.y, PLATFORM_SCREEN_YMIN, PLATFORM_SCREEN_YMAX);
                }
                if(down)
                {
                    Amiga_Evq_Push(ev.type, ev.x, ev.y, ev.code);
                }
            } break;

            case AMIGAGFX_EV_KEY:
            case AMIGAGFX_EV_QUIT:
            {
                Amiga_Evq_Push(ev.type, ev.x, ev.y, ev.code);
            } break;

            case AMIGAGFX_EV_RESIZE:
            {
                Amiga_PFL_Force_Full_Present();
            } break;

            default:
            {
                /* IntuiTicks itp. */
            } break;
        }
    }
}

/* Odbiera wszystko z IDCMP, potem co najwyzej jedno zdarzenie z autoinputu
   (odstepy miedzy nimi pilnuje amiga_Auto.c). Autoinput tylko z
   Platform_Event_Handler - tak jak scenariusz .hms - zeby klikniecie nie
   trafilo w petle oczekiwania w srodku tury AI (Platform_Pump_Events). */
static void Amiga_Drain_Events(int with_autoinput)
{
    AmigaGfxEvent ev;
    int ignore_mouse;

    /* muzyka: dolanie buforow Pauli (amiga_Audio.c) */
    Amiga_Audio_Service();

    if(!amiga_pfl_display_open)
    {
        return;
    }

    ignore_mouse = Amiga_Scenario_Mode();

    while(amigagfx_poll(&ev))
    {
        Amiga_Handle_Gfx_Event(&ev, ignore_mouse);
    }
    if(with_autoinput && Amiga_Autoinput_Next(&ev))
    {
        Amiga_Handle_Gfx_Event(&ev, ignore_mouse);
    }
}



/* ========================================================================= */
/*  Cykl zycia                                                               */
/* ========================================================================= */

/* porownanie bez wielkosci liter - bez polegania na stricmp z libnix */
static int Amiga_Name_Is(const char * a, const char * b)
{
    while(*a != '\0' && *b != '\0')
    {
        char ca = *a;
        char cb = *b;
        if(ca >= 'A' && ca <= 'Z') { ca = (char)(ca - 'A' + 'a'); }
        if(cb >= 'A' && cb <= 'Z') { cb = (char)(cb - 'A' + 'a'); }
        if(ca != cb) { return 0; }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static int Amiga_Backend_From_Name(const char * name)
{
    if(name == NULL || name[0] == '\0')          { return AMIGA_GFX_DEFAULT; }
    if(Amiga_Name_Is(name, "aga"))                { return AMIGAGFX_BACKEND_AGA; }
    if(Amiga_Name_Is(name, "rtg"))                { return AMIGAGFX_BACKEND_RTG; }
    if(Amiga_Name_Is(name, "cgx"))                { return AMIGAGFX_BACKEND_RTG; }
    if(Amiga_Name_Is(name, "wb"))                 { return AMIGAGFX_BACKEND_WB; }
    if(Amiga_Name_Is(name, "window"))             { return AMIGAGFX_BACKEND_WB; }
    if(Amiga_Name_Is(name, "none"))               { return AMIGA_GFX_NONE; }
    if(Amiga_Name_Is(name, "headless"))           { return AMIGA_GFX_NONE; }
    fprintf(stdout, "[amiga] unknown display \"%s\" - using AGA\n", name);
    return AMIGAGFX_BACKEND_AGA;
}

void Amiga_Platform_Set_Backend(const char * name)
{
    amiga_backend_request = Amiga_Backend_From_Name(name);
}

static int Amiga_Choose_Backend(void)
{
    int choice = amiga_backend_request;
    const char * env;

    if(choice == AMIGA_GFX_DEFAULT)
    {
        choice = Amiga_Backend_From_Name(getenv("REMOM_GFX"));
    }
    if(choice == AMIGA_GFX_DEFAULT)
    {
        env = getenv("SDL_VIDEODRIVER");
        if(env != NULL && (Amiga_Name_Is(env, "dummy") || Amiga_Name_Is(env, "offscreen")))
        {
            choice = AMIGA_GFX_NONE;
        }
    }
    if(choice == AMIGA_GFX_DEFAULT && amiga_opt_grafika != 0)
    {
        /* gfx= w Work:amiga.cfg (remom-prefs): 1 AGA, 2 RTG, 3 okno */
        choice = (amiga_opt_grafika == 1) ? AMIGAGFX_BACKEND_AGA :
                 (amiga_opt_grafika == 2) ? AMIGAGFX_BACKEND_RTG : AMIGAGFX_BACKEND_WB;
    }
    if(choice == AMIGA_GFX_DEFAULT)
    {
        /* amiga_Domyslny_aga.c (remom-aga) albo amiga_Domyslny_rtg.c (remom-rtg) */
        choice = amiga_domyslny_backend;
    }
    return choice;
}

static void Amiga_Close_Display(void)
{
    if(amiga_pfl_display_open)
    {
        amiga_pfl_display_open = 0;
        amigagfx_close();
    }
}

/* Kazde wyjscie z programu (Exit_With_Message, asercja przez STU_BRAK,
   Ctrl+Q, pulapka CPU z AMIGA_TRAP_ARM) musi zamknac ekran - inaczej zostaje
   w systemie z zajeta Chip RAM do resetu. */
static void Amiga_Platform_Atexit(void)
{
    Amiga_Close_Display();
}

void Startup_Platform(void)
{
    int backend;
    int rc;

    Amiga_Opcje_Wczytaj();   /* przed wyborem ekranu - gfx= z amiga.cfg */
    backend = Amiga_Choose_Backend();

    Amiga_Keyboard_Init();
    Amiga_Evq_Clear();

    if(!amiga_atexit_registered)
    {
        atexit(Amiga_Platform_Atexit);
        amiga_atexit_registered = 1;
    }

    if(backend == AMIGA_GFX_NONE)
    {
        LOG_INFO(LOG_CAT_PFL, "[amiga] Platform started without a display (REMOM_GFX=none)");
        fprintf(stdout, "[amiga] Startup_Platform: no display\n");
        fflush(stdout);
        amiga_pfl_display_open = 0;
        Platform_Mouse_Input_Enable();
        return;
    }

    /* Opcje Amigi (amiga_Opcje.c, Work:amiga.cfg): pasek systemowy ekranu
       (z gadzetem glebi) domyslnie WLACZONY, jak w portach OpenTTD
       i OpenXcom - amigagfx otwiera wtedy ekran o wysokosc paska wyzszy, wiec
       gra ma dalej pelne 320x200; tryb obrazu auto/PAL/NTSC jak w OpenXcomie.
       Workbench ma dzialac w tle (Work:run robi LoadWB) - czekamy, az
       wstanie, zeby jego ekran nie wyskoczyl potem przed gre. */
    Amiga_Opcje_Wczytaj();
    amiga_pfl_backend = backend;
    if(backend != AMIGAGFX_BACKEND_WB)
    {
        int wb = Amiga_Czekaj_Na_Workbench(4000);
        printf("[amiga] Workbench: %s\n", wb ?"dziala" : "nie wystartowal (4 s)");
        fflush(stdout);
    }
    amigagfx_set_video_mode(amiga_opt_wideo);
    /* tytul PRZED otwarciem - okno na Workbenchu (gfx=3) dostaje go przy
       otwarciu; wczesniej pokazywalo domyslny tytul warstwy z OpenXcoma */
    amigagfx_set_screen_title(AMIGA_TYTUL);
    rc = amigagfx_open(PLATFORM_SCREEN_WIDTH, PLATFORM_SCREEN_HEIGHT, amiga_opt_pasek, backend);
    if(rc != 0)
    {
        char message[160];
        snprintf(message, sizeof(message),
                 "Could not open a %dx%d display (error %d).",
                 PLATFORM_SCREEN_WIDTH, PLATFORM_SCREEN_HEIGHT, rc);
        Platform_Show_Error("ReMoM", message);
        exit(20);
    }
    amiga_pfl_display_open = 1;

    amigagfx_set_screen_title(AMIGA_TYTUL);
    Perf_Live_Set_Base_Title(AMIGA_TYTUL);
    /* kursor rysuje gra - wskaznik systemowy znika nad naszym oknem,
       chyba ze gracz wybral "System Cursor" */
    Amiga_PFL_Ustaw_Kursor();

    Amiga_PFL_Force_Full_Present();
    Platform_Mouse_Input_Enable();

    fprintf(stdout, "[amiga] Startup_Platform: display %s open, %dx%d, pitch %d\n",
            amigagfx_backend() == AMIGAGFX_BACKEND_RTG ? "RTG" :
            amigagfx_backend() == AMIGAGFX_BACKEND_WB  ? "WINDOW" :
            amigagfx_backend() == AMIGAGFX_BACKEND_EHB ? "EHB" : "AGA",
            amigagfx_game_width(), amigagfx_game_height(), amigagfx_pitch());
    fflush(stdout);
    LOG_INFO(LOG_CAT_PFL, "[amiga] Platform started, display backend %d", amigagfx_backend());
}

/* opcja "System Cursor": wskaznik Intuition zamiast kursora gry */
void Amiga_PFL_Ustaw_Kursor(void)
{
    amigagfx_set_hide_system_pointer(amiga_opt_kursor ? 0 : 1);
}

/* Nowy ekran z biezacymi opcjami (pasek, PAL/NTSC) - po wyjsciu z ekranu
   opcji. Paleta i caly obraz ida od nowa przy nastepnej klatce. */
void Amiga_PFL_Przeotworz_Ekran(void)
{
    int rc;

    if(!amiga_pfl_display_open)
    {
        return;
    }
    amiga_pfl_display_open = 0;
    amigagfx_close();
    amigagfx_set_video_mode(amiga_opt_wideo);
    rc = amigagfx_open(PLATFORM_SCREEN_WIDTH, PLATFORM_SCREEN_HEIGHT, amiga_opt_pasek, amiga_pfl_backend);
    if(rc != 0)
    {
        char message[160];
        snprintf(message, sizeof(message), "Could not reopen the %dx%d display (error %d).",
                 PLATFORM_SCREEN_WIDTH, PLATFORM_SCREEN_HEIGHT, rc);
        Platform_Show_Error("ReMoM", message);
        exit(20);
    }
    amiga_pfl_display_open = 1;
    amigagfx_set_screen_title(AMIGA_TYTUL);
    Amiga_PFL_Ustaw_Kursor();
    Amiga_PFL_Force_Full_Present();
    printf("[amiga] ekran otwarty ponownie: pasek %d, obraz %s\n", amiga_opt_pasek,
           amigagfx_is_ntsc() ? "NTSC" : "PAL");
    fflush(stdout);
}

void Shutdown_Platform(void)
{
    LOG_INFO(LOG_CAT_PFL, "[amiga] Platform shutdown");
    Amiga_Close_Display();
}

int Platform_Get_Scale(void)
{
    return 1;
}

/* PFL_Perf.c ustawia co sekunde "<tytul> - N fps (worst M ms)". Opcja
   fps=0 (remom-prefs "FPS on title bar") zostawia na pasku sam tytul. */
void Platform_Set_Window_Title(const char * title)
{
    static int zalogowane = 0;
    const char * pokaz;
    if(title == NULL) { return; }
    pokaz = amiga_opt_fps ? title : AMIGA_TYTUL;
    if(zalogowane < 3)
    {
        zalogowane++;
        printf("[amiga] pasek: \"%s\"\n", pokaz);
        fflush(stdout);
    }
    amigagfx_set_screen_title(pokaz);
}

void Platform_Show_Error(const char * title, const char * message)
{
    fprintf(stderr, "%s: %s\n", title, message);
    fprintf(stdout, "%s: %s\n", title, message);
    fflush(stdout);
    /* Requester otwiera sie na ekranie Workbencha. Przy otwartym ekranie gry
       bylby pod nim niewidoczny, a program czekalby na klikniecie - wiec
       wtedy tylko log. */
    if(!amiga_pfl_display_open)
    {
        Amiga_Req_Error(title, message);
    }
}



/* ========================================================================= */
/*  Mysz                                                                     */
/* ========================================================================= */

/* sdl2_PFL.c Platform_Update_Mouse_Position(), skala 1 */
static void Amiga_Update_Mouse_Position(int gx, int gy)
{
    if(gx < PLATFORM_SCREEN_XMIN || gy < PLATFORM_SCREEN_YMIN || gx > PLATFORM_SCREEN_XMAX || gy > PLATFORM_SCREEN_YMAX)
    {
        return;
    }
    pointer_x = gx;
    pointer_y = gy;
    if(mouse_interrupt_active == ST_FALSE)
    {
        mouse_interrupt_active = ST_TRUE;
        if(mouse_enabled == ST_TRUE)
        {
            mouse_enabled = ST_FALSE;
            if(current_mouse_list_count >= 2)
            {
                Check_Mouse_Shape(gx, gy);
            }
            Restore_Mouse_On_Page();
            Save_Mouse_On_Page(gx, gy);
            Draw_Mouse_On_Page(gx, gy);
            mouse_enabled = ST_TRUE;
        }
        mouse_interrupt_active = ST_FALSE;
    }
}

static int16_t amiga_synthetic_mouse_button = 0;
static int16_t amiga_synthetic_mouse_button_hold = 0;

void Platform_Set_Synthetic_Mouse_Button(int16_t buttons)
{
    amiga_synthetic_mouse_button = buttons;
    amiga_synthetic_mouse_button_hold = (buttons != 0) ? PLATFORM_SYNTHETIC_MOUSE_HOLD : 0;
}

int16_t Platform_Get_Mouse_Button_State(void)
{
    int16_t l_mouse_button = 0;

    if(!platform_mouse_input_enabled)
    {
        return 0;
    }

    if(amiga_hw_left)
    {
        l_mouse_button = ST_LEFT_BUTTON;
    }
    if(amiga_hw_right)
    {
        l_mouse_button = ST_RIGHT_BUTTON;
    }

    /* jak sdl2/headless: najpierw oddaj wcisniecie, potem zmniejsz licznik */
    if(amiga_synthetic_mouse_button != 0)
    {
        l_mouse_button |= amiga_synthetic_mouse_button;
        if(amiga_synthetic_mouse_button_hold > 0)
        {
            amiga_synthetic_mouse_button_hold--;
            if(amiga_synthetic_mouse_button_hold == 0)
            {
                amiga_synthetic_mouse_button = 0;
            }
        }
    }

    return l_mouse_button;
}

void Platform_Warp_Mouse(int16_t game_x, int16_t game_y)
{
    int gx = Amiga_Clamp(game_x, PLATFORM_SCREEN_XMIN, PLATFORM_SCREEN_XMAX);
    int gy = Amiga_Clamp(game_y, PLATFORM_SCREEN_YMIN, PLATFORM_SCREEN_YMAX);

    amiga_hw_x = gx;
    amiga_hw_y = gy;
    /* zdarzenia ruchu sprzed skoku, jeszcze w kolejce, nie moga cofnac kursora */
    amiga_warp_guard_until = Platform_Get_Millies() + AMIGA_WARP_GUARD_MS;

    if(amiga_pfl_display_open && !Amiga_Scenario_Mode())
    {
        amigagfx_warp_pointer(gx, gy);
    }
}

int Platform_Get_Window_Width(void)
{
    /* skala 1: User_Mouse_Handler liczy window_width / SCREEN_WIDTH = 1.0 */
    return PLATFORM_SCREEN_WIDTH;
}

void Platform_Get_Mouse_Position_XY(int *mx, int *my)
{
    if(amiga_hw_x < 0)
    {
        *mx = (int)pointer_x;
        *my = (int)pointer_y;
    }
    else
    {
        *mx = amiga_hw_x;
        *my = amiga_hw_y;
    }
}

/* sdl2_PFL.c: przerysuj kursor, gdy mysz sie ruszyla - ale na ekran idzie
   tylko stary i nowy prostokat kursora. */
void Platform_Maybe_Move_Mouse(void)
{
    int gx;
    int gy;
    int old_x;
    int old_y;

    if(Get_Pointer_Image_Number() == 0)  /* crsr_None */
    {
        return;
    }

    if(amiga_hw_x < 0 || Platform_Get_Millies() < amiga_warp_guard_until)
    {
        gx = pointer_x;
        gy = pointer_y;
    }
    else
    {
        gx = amiga_hw_x;
        gy = amiga_hw_y;
    }

    /* wskaznik systemowy: tylko pozycja i ksztalt dla gry, bez rysowania */
    if(Platform_HW_Cursor_Active())
    {
        pointer_x = gx;
        pointer_y = gy;
        if(current_mouse_list_count >= 2)
        {
            Check_Mouse_Shape(gx, gy);
        }
        return;
    }

    if(gx == pointer_x && gy == pointer_y)
    {
        return;
    }

#if defined(MOUSE_DEBUG) && defined(AMIGA_MOUSE_TRACE)
    MOUSE_LOG("MOUSEt=%llu MAYBE_MOVE ptr=%d,%d -> %d,%d img=%d\n", (unsigned long long)Platform_Get_Millies(), pointer_x, pointer_y, gx, gy, Get_Pointer_Image_Number());
#endif

    old_x = pointer_x;
    old_y = pointer_y;

    if(mouse_enabled == ST_TRUE)
    {
        Restore_Mouse_On_Page();
    }
    pointer_x = gx;
    pointer_y = gy;
    if(mouse_enabled == ST_TRUE)
    {
        if(current_mouse_list_count >= 2)
        {
            Check_Mouse_Shape(gx, gy);
        }
        Save_Mouse_On_Page(gx, gy);
        Draw_Mouse_On_Page(gx, gy);
        Amiga_PFL_Present_Rect(old_x, old_y, AMIGA_CURSOR_DIM, AMIGA_CURSOR_DIM);
        Amiga_PFL_Present_Rect(gx, gy, AMIGA_CURSOR_DIM, AMIGA_CURSOR_DIM);
        Restore_Mouse_On_Page();
    }
}

/* Kursor programowy, chyba ze gracz wlaczyl "System Cursor" (opcje Amigi):
   wtedy MoX nie rysuje kursora, a wskaznik Intuition chodzi sam. */
int Platform_HW_Cursor_Active(void)
{
    return (amiga_pfl_display_open && amiga_opt_kursor) ? 1 : 0;
}

void Platform_HW_Cursor_Refresh(void)
{
}

void Platform_Mouse_Input_Enable(void)
{
    platform_mouse_input_enabled = ST_TRUE;
}

void Platform_Mouse_Input_Disable(void)
{
    platform_mouse_input_enabled = ST_FALSE;
}



/* ========================================================================= */
/*  Zdarzenia                                                                */
/* ========================================================================= */

void Platform_Pump_Events(void)
{
    Amiga_Drain_Events(0);
    if(amiga_pfl_display_open)
    {
        Platform_Maybe_Move_Mouse();
    }
}

static int amiga_event_handler_first_call = 1;

void Platform_Event_Handler(void)
{
    int type;
    int x;
    int y;
    int code;

    if(amiga_event_handler_first_call)
    {
        amiga_event_handler_first_call = 0;
        if(Platform_Replay_Active())
        {
            LOG_INFO(LOG_CAT_PFL, "[amiga] Platform_Event_Handler: input mode = REPLAY (.RMR)");
        }
        else if(platform_frame_callback != NULL)
        {
            LOG_INFO(LOG_CAT_PFL, "[amiga] Platform_Event_Handler: input mode = SCENARIO (.hms artificial human player)");
        }
        else
        {
            LOG_INFO(LOG_CAT_PFL, "[amiga] Platform_Event_Handler: input mode = %s", amiga_pfl_display_open ? "LIVE (mouse + keyboard)" : "NONE (no input source)");
        }
    }

#if defined(MOUSE_DEBUG) && defined(AMIGA_MOUSE_TRACE)
    MOUSE_LOG("MOUSEt=%llu HANDLER_START ptr=%d,%d\n", (unsigned long long)Platform_Get_Millies(), pointer_x, pointer_y);
#endif

    /* platform_frame_mouse_buttons NIE jest tu zerowane - dopiero na koncu,
       po zapisie klatki (jak sdl2/headless). */

    /* Replay: nacisniecie klawisza lub przycisku przerywa odtwarzanie (SDL2),
       reszta zdarzen jest wyrzucana; potem wstrzykniecie nagranej klatki. */
    if(Platform_Replay_Active())
    {
        Amiga_Drain_Events(0);
        while(Amiga_Evq_Pop(&type, &x, &y, &code))
        {
            if((type == AMIGAGFX_EV_KEY && (code & 0x80) == 0) || type == AMIGAGFX_EV_MOUSEDOWN)
            {
                LOG_INFO(LOG_CAT_PFL, "REPLAY: cancelled by user input");
                Platform_Replay_Stop();
                Amiga_Evq_Clear();
                return;
            }
            if(type == AMIGAGFX_EV_QUIT)
            {
                Platform_Replay_Stop();
                exit(EXIT_SUCCESS);
            }
        }

        if(Platform_Replay_Active())
        {
            if(Replay_Inject_Frame())
            {
                platform_frame_mouse_buttons = 0;
                return;
            }
            /* koniec nagrania - dalej zywe wejscie */
        }
    }

    Amiga_Drain_Events(1);

    while(Amiga_Evq_Pop(&type, &x, &y, &code))
    {
        switch(type)
        {
            case AMIGAGFX_EV_KEY:
            {
                Amiga_Keyboard_Raw_Event(code);
            } break;

            case AMIGAGFX_EV_MOUSEDOWN:
            {
#ifdef MOUSE_DEBUG
                MOUSE_LOG("MOUSEt=%llu BTN_DOWN btn=%d wx=%d wy=%d\n", (unsigned long long)Platform_Get_Millies(), code, x, y);
#endif
                if(code == AMIGAGFX_BUTTON_LEFT)
                {
                    platform_frame_mouse_buttons |= 1;
                    User_Mouse_Handler(1, (int16_t)x, (int16_t)y);
                }
                if(code == AMIGAGFX_BUTTON_RIGHT)
                {
                    platform_frame_mouse_buttons |= 2;
                    User_Mouse_Handler(2, (int16_t)x, (int16_t)y);
                }
            } break;

            case AMIGAGFX_EV_QUIT:
            {
                exit(EXIT_SUCCESS);
            } break;

            default:
            {
            } break;
        }
    }

    /* ~== MWA WM_MOUSEMOVE: pozycja z "sprzetu" do silnika + kursor.
       W scenariuszu jedynym zrodlem pozycji jest silnik (Set_Pointer_Position
       woła warp tylko przy mouse_driver_installed), wiec kursor rysujemy tam,
       gdzie silnik go ma, i niczego nie nadpisujemy. */
    if(platform_mouse_input_enabled && amiga_pfl_display_open && Amiga_Scenario_Mode())
    {
        Amiga_Update_Mouse_Position(pointer_x, pointer_y);
    }
    else if(platform_mouse_input_enabled && amiga_pfl_display_open && amiga_hw_x >= 0)
    {
        Amiga_Update_Mouse_Position(amiga_hw_x, amiga_hw_y);
#if defined(MOUSE_DEBUG) && defined(AMIGA_MOUSE_TRACE)
        MOUSE_LOG("MOUSEt=%llu POLL wx=%d wy=%d gx=%d gy=%d\n", (unsigned long long)Platform_Get_Millies(), amiga_hw_x, amiga_hw_y, pointer_x, pointer_y);
#endif
    }

    /* scenariusz .hms */
    if(platform_frame_callback != NULL)
    {
        platform_frame_callback();
    }

    STU_Log_Pump();

    if(Platform_Record_Active())
    {
        Replay_Capture_Frame();
    }

    platform_frame_mouse_buttons = 0;
}
