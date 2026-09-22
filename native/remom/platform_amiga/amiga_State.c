/*
 * amiga_State.c - wspolne zmienne warstwy platformy (Platform.h: "Shared State").
 *
 * Kopia platform/headless/headless_State.c. Silnik je czyta, backend pisze.
 * To zwykle skalary i tablica bajtow - pragma little-endian z amiga_le.h ich
 * nie dotyczy (dziala tylko na typy struct/union).
 */

#include "Platform.h"

int quit_game_flag;

int8_t key_pressed;

uint16_t scan_code_char_code;

PFL_Color platform_palette_buffer[256];

int lock_mouse_button_status_flag = 0;

int platform_mouse_input_enabled = 0;

int16_t platform_frame_mouse_buttons = 0;
