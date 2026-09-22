/*
 * amiga_KD.c - klawiatura: surowe kody Amigi (IDCMP_RAWKEY) -> kody MoX.
 *
 * Przepisane z platform/sdl2/sdl2_KD.c. Semantyka zachowana:
 *
 *  - klawisz z odwzorowaniem (litery, Enter, Esc, strzalki, F1-F12, klawisze
 *    nawigacji, kierunki na klawiaturze numerycznej) trafia do bufora jako
 *    (MOX_KEY_*, modyfikatory, znak). Znak to NIEprzesuniety znak ASCII
 *    klawisza, o ile lezy w 32..122 - dokladnie tak jak SDL2, gdzie sym dla
 *    klawiszy ASCII jest zawsze "mala litera / cyfra" niezaleznie od Shift;
 *  - cyfry, spacja i interpunkcja NIE maja kodu MOX i poza trybem tekstowym
 *    nie trafiaja do bufora (jak w SDL2: key_xlat_key[] ich nie zna);
 *  - w trybie tekstowym (Hw_Textinput_Start) litery nie ida sciezka klawisza
 *    (MOX_KEY_OVERRUN), a kazdy drukowalny znak - juz z Shift/Caps Lock -
 *    idzie jako (MOX_KEY_UNKNOWN, 0, znak), jak SDL_TEXTINPUT.
 *
 * amigagfx_poll() oddaje tylko surowy kod (bez kwalifikatorow), wiec Shift,
 * Ctrl, Alt i Caps Lock sledzimy sami z kodow 0x60..0x65.
 *
 * Uklad klawiatury: US (tabela ponizej). keymap.library nie jest uzywana -
 * gra potrzebuje glownie liter, cyfr i klawiszy sterujacych.
 *
 * Kody rozszerzone (klawiatura A4000/CD32 i klawisze PC mapowane przez
 * WinUAE): 0x47 Insert, 0x48 PageUp, 0x49 PageDown, 0x4B F11, 0x6F F12,
 * 0x70 Home, 0x71 End. Na klawiaturze bez nich te funkcje sa niedostepne.
 *
 * ROZNICA wobec SDL2 (swiadoma): wyjscie z gry skrotem wymaga Ctrl+Q lub
 * Alt+Q. W SDL2 wystarczal tez Shift+Q, co zamykalo gre przy wpisywaniu
 * wielkiej litery Q w polu tekstowym.
 */

#include "Platform.h"
#include "Platform_Keys.h"

#include "amiga_PFL.h"

#include <stdlib.h>
#include <string.h>

struct s_KEYBOARD_BUFFER platform_keyboard_buffer;

static int amiga_textinput_active = 0;

/* stan modyfikatorow z surowych kodow */
static int amiga_mod_lshift = 0;
static int amiga_mod_rshift = 0;
static int amiga_mod_ctrl   = 0;
static int amiga_mod_lalt   = 0;
static int amiga_mod_ralt   = 0;
static int amiga_capslock   = 0;

#define AMIGA_RAW_COUNT 0x80

/* surowy kod -> MOX_KEY_* (0 = brak odwzorowania) */
static int amiga_xlat_key[AMIGA_RAW_COUNT];
/* surowy kod -> znak bez Shift / z Shift (0 = brak) */
static char amiga_xlat_lower[AMIGA_RAW_COUNT];
static char amiga_xlat_upper[AMIGA_RAW_COUNT];
/* 1 = klawiatura numeryczna (sciezka klawisza nie niesie znaku, jak SDL2) */
static char amiga_xlat_keypad[AMIGA_RAW_COUNT];

static void Amiga_Xlat_Chars(int first_raw, const char * lower, const char * upper)
{
    int i;
    for(i = 0; lower[i] != '\0'; i++)
    {
        amiga_xlat_lower[first_raw + i] = lower[i];
        amiga_xlat_upper[first_raw + i] = upper[i];
    }
}

static void Amiga_Xlat_Keypad(int raw, int mox_key, char ch)
{
    amiga_xlat_key[raw] = mox_key;
    amiga_xlat_lower[raw] = ch;
    amiga_xlat_upper[raw] = ch;
    amiga_xlat_keypad[raw] = 1;
}

void Amiga_Keyboard_Init(void)
{
    int i;

    memset(amiga_xlat_key, 0, sizeof(amiga_xlat_key));
    memset(amiga_xlat_lower, 0, sizeof(amiga_xlat_lower));
    memset(amiga_xlat_upper, 0, sizeof(amiga_xlat_upper));
    memset(amiga_xlat_keypad, 0, sizeof(amiga_xlat_keypad));

    /* rzad cyfr i trzy rzedy liter, uklad US */
    Amiga_Xlat_Chars(0x00, "`1234567890-=\\", "~!@#$%^&*()_+|");
    Amiga_Xlat_Chars(0x10, "qwertyuiop[]", "QWERTYUIOP{}");
    Amiga_Xlat_Chars(0x20, "asdfghjkl;'", "ASDFGHJKL:\"");
    Amiga_Xlat_Chars(0x31, "zxcvbnm,./", "ZXCVBNM<>?");
    amiga_xlat_lower[0x40] = ' ';
    amiga_xlat_upper[0x40] = ' ';

    /* litery -> MOX_KEY_a..z */
    for(i = 0; i < AMIGA_RAW_COUNT; i++)
    {
        char c = amiga_xlat_lower[i];
        if(c >= 'a' && c <= 'z')
        {
            amiga_xlat_key[i] = MOX_KEY_a + (c - 'a');
        }
    }

    /* klawisze sterujace */
    amiga_xlat_key[0x41] = MOX_KEY_BACKSPACE;
    amiga_xlat_key[0x42] = MOX_KEY_TAB;
    amiga_xlat_key[0x44] = MOX_KEY_ENTER;      /* Return */
    amiga_xlat_key[0x43] = MOX_KEY_ENTER;      /* Enter na numerycznej - SDL2 go nie mapowal */
    amiga_xlat_key[0x45] = MOX_KEY_ESCAPE;
    amiga_xlat_key[0x46] = MOX_KEY_DELETE;

    /* strzalki */
    amiga_xlat_key[0x4C] = MOX_KEY_UP;
    amiga_xlat_key[0x4D] = MOX_KEY_DOWN;
    amiga_xlat_key[0x4E] = MOX_KEY_RIGHT;
    amiga_xlat_key[0x4F] = MOX_KEY_LEFT;

    /* F1..F10, F11/F12 z klawiatur rozszerzonych */
    for(i = 0; i < 10; i++)
    {
        amiga_xlat_key[0x50 + i] = MOX_KEY_F1 + i;
    }
    amiga_xlat_key[0x4B] = MOX_KEY_F11;
    amiga_xlat_key[0x6F] = MOX_KEY_F12;

    /* nawigacja (klawiatury rozszerzone / WinUAE) */
    amiga_xlat_key[0x47] = MOX_KEY_INSERT;
    amiga_xlat_key[0x48] = MOX_KEY_PGUP;
    amiga_xlat_key[0x49] = MOX_KEY_PGDN;
    amiga_xlat_key[0x70] = MOX_KEY_HOME;
    amiga_xlat_key[0x71] = MOX_KEY_END;

    /* klawiatura numeryczna - kierunki jak w SDL2 (bez 0 i 5) */
    Amiga_Xlat_Keypad(0x1D, MOX_KEY_LEFTDOWN,  '1');
    Amiga_Xlat_Keypad(0x1E, MOX_KEY_DOWN,      '2');
    Amiga_Xlat_Keypad(0x1F, MOX_KEY_RIGHTDOWN, '3');
    Amiga_Xlat_Keypad(0x2D, MOX_KEY_LEFT,      '4');
    Amiga_Xlat_Keypad(0x2E, MOX_KEY_UNKNOWN,   '5');
    Amiga_Xlat_Keypad(0x2F, MOX_KEY_RIGHT,     '6');
    Amiga_Xlat_Keypad(0x3D, MOX_KEY_LEFTUP,    '7');
    Amiga_Xlat_Keypad(0x3E, MOX_KEY_UP,        '8');
    Amiga_Xlat_Keypad(0x3F, MOX_KEY_RIGHTUP,   '9');
    Amiga_Xlat_Keypad(0x0F, MOX_KEY_UNKNOWN,   '0');
    Amiga_Xlat_Keypad(0x3C, MOX_KEY_UNKNOWN,   '.');
    Amiga_Xlat_Keypad(0x4A, MOX_KEY_UNKNOWN,   '-');
    Amiga_Xlat_Keypad(0x5A, MOX_KEY_UNKNOWN,   '(');
    Amiga_Xlat_Keypad(0x5B, MOX_KEY_UNKNOWN,   ')');
    Amiga_Xlat_Keypad(0x5C, MOX_KEY_UNKNOWN,   '/');
    Amiga_Xlat_Keypad(0x5D, MOX_KEY_UNKNOWN,   '*');
    Amiga_Xlat_Keypad(0x5E, MOX_KEY_UNKNOWN,   '+');

    amiga_mod_lshift = amiga_mod_rshift = 0;
    amiga_mod_ctrl = 0;
    amiga_mod_lalt = amiga_mod_ralt = 0;
    amiga_capslock = 0;
}

static uint32_t Amiga_Mod_State(void)
{
    uint32_t mox_mod = 0;
    if(amiga_mod_lshift || amiga_mod_rshift) { mox_mod |= MOX_MOD_SHIFT; }
    if(amiga_mod_lalt || amiga_mod_ralt)     { mox_mod |= MOX_MOD_ALT;   }
    if(amiga_mod_ctrl)                       { mox_mod |= MOX_MOD_CTRL;  }
    return mox_mod;
}

static void Kbd_Set_Pressed(int mox_key, uint32_t mox_mod, int pressed)
{
    platform_keyboard_buffer.mox_mod = mox_mod;

    if((mox_key != MOX_KEY_UNKNOWN) && (mox_key < MOX_KEY_OVERRUN))
    {
        BOOLVEC_SET(platform_keyboard_buffer.pressed, mox_key, pressed);
    }
}

void Amiga_Keyboard_Raw_Event(int raw_code)
{
    int released = (raw_code & 0x80) != 0;
    int raw = raw_code & 0x7F;
    int mox_key;
    uint32_t mox_mod;
    char mox_character;

    /* modyfikatory - tylko stan, do bufora nie ida (jak w SDL2) */
    switch(raw)
    {
        case 0x60: amiga_mod_lshift = !released; platform_keyboard_buffer.mox_mod = Amiga_Mod_State(); return;
        case 0x61: amiga_mod_rshift = !released; platform_keyboard_buffer.mox_mod = Amiga_Mod_State(); return;
        case 0x62: amiga_capslock   = !released; return;   /* Caps Lock: kod "down" = wlaczony */
        case 0x63: amiga_mod_ctrl   = !released; platform_keyboard_buffer.mox_mod = Amiga_Mod_State(); return;
        case 0x64: amiga_mod_lalt   = !released; platform_keyboard_buffer.mox_mod = Amiga_Mod_State(); return;
        case 0x65: amiga_mod_ralt   = !released; platform_keyboard_buffer.mox_mod = Amiga_Mod_State(); return;
        case 0x66:
        case 0x67: return;   /* klawisze Amiga - skroty systemowe, grze nic nie dajemy */
        default: break;
    }

    /* 0x78 = reset warning, 0x79.. = kody specjalne klawiatury */
    if(raw >= 0x78)
    {
        return;
    }

    mox_key = amiga_xlat_key[raw];
    mox_mod = Amiga_Mod_State();

    if(released)
    {
        Kbd_Set_Pressed(mox_key, mox_mod, ST_FALSE);
        return;
    }

    /* Ctrl+Q / Alt+Q - wyjscie (SDL2: push SDL_QUIT -> exit(EXIT_SUCCESS)).
       Ekran zamyka procedura atexit z amiga_PFL.c. */
    if((mox_mod & (MOX_MOD_CTRL | MOX_MOD_ALT)) && (raw == 0x10))
    {
        exit(EXIT_SUCCESS);
    }

    if(amiga_xlat_keypad[raw])
    {
        mox_character = 0;   /* SDL2: klawisze spoza ASCII nie niosa znaku */
    }
    else
    {
        mox_character = amiga_xlat_lower[raw];
        if((mox_character < MOX_KEY_SPACE) || (mox_character > MOX_KEY_z))
        {
            mox_character = MOX_KEY_UNKNOWN;
        }
        if((amiga_textinput_active) && (mox_key >= MOX_KEY_SPACE) && (mox_key <= MOX_KEY_z))
        {
            mox_key = MOX_KEY_OVERRUN;
        }
    }

    if((mox_key != MOX_KEY_UNKNOWN) && (mox_key < MOX_KEY_OVERRUN))
    {
        Platform_Keyboard_Buffer_Add_Key_Press(mox_key, mox_mod, mox_character);
    }
    Kbd_Set_Pressed(mox_key, mox_mod, ST_TRUE);

    /* odpowiednik SDL_TEXTINPUT */
    if(amiga_textinput_active && ((mox_mod & (MOX_MOD_CTRL | MOX_MOD_ALT)) == 0))
    {
        char text_character;
        int shifted = (mox_mod & MOX_MOD_SHIFT) != 0;
        char lower = amiga_xlat_lower[raw];

        if(lower >= 'a' && lower <= 'z')
        {
            if(amiga_capslock)
            {
                shifted = !shifted;
            }
        }
        text_character = shifted ? amiga_xlat_upper[raw] : lower;
        if(text_character != 0)
        {
            Platform_Keyboard_Buffer_Add_Key_Press(MOX_KEY_UNKNOWN, MOX_MOD_NONE, text_character);
        }
    }
}



/* ========================================================================= */
/*  Bufor klawiatury - bez zmian wzgledem headless_KD.c / sdl2_KD.c          */
/* ========================================================================= */

void Platform_Keyboard_Buffer_Clear(void)
{
    platform_keyboard_buffer.key_write = 0;
    platform_keyboard_buffer.key_read = 0;
}

void Platform_Keyboard_Buffer_Add_Key_Press(int mox_key, uint32_t mox_mod, char mox_character)
{
    uint32_t packed_key = 0;

    packed_key = ((uint32_t)mox_key) | mox_mod | (((uint32_t)mox_character) << 8);

    if(mox_key == MOX_KEY_OVERRUN)
    {
        return;
    }

    key_pressed = ST_TRUE;

    platform_keyboard_buffer.packed_key[platform_keyboard_buffer.key_write] = packed_key;

    platform_keyboard_buffer.key_write = ((platform_keyboard_buffer.key_write + 1) % PLATFORM_KEYBOARD_BUFFER_LENGTH);
}

int Platform_Keyboard_Buffer_Pending_Count(void)
{
    return (platform_keyboard_buffer.key_write - platform_keyboard_buffer.key_read + PLATFORM_KEYBOARD_BUFFER_LENGTH) % PLATFORM_KEYBOARD_BUFFER_LENGTH;
}

uint32_t Platform_Keyboard_Buffer_Peek_Latest(void)
{
    if(platform_keyboard_buffer.key_write == platform_keyboard_buffer.key_read)
    {
        return 0;
    }
    return platform_keyboard_buffer.packed_key[(platform_keyboard_buffer.key_write - 1 + PLATFORM_KEYBOARD_BUFFER_LENGTH) % PLATFORM_KEYBOARD_BUFFER_LENGTH];
}

void Hw_Textinput_Start(void)
{
    amiga_textinput_active = 1;
}

void Hw_Textinput_Stop(void)
{
    amiga_textinput_active = 0;
}
