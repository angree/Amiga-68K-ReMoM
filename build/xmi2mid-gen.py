#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
xmi2mid-gen.py - narzedzie HOSTOWE: wpis XMIDI z LBX gracza -> plik MIDI.

Wycina MECHANICZNIE z platform/sdl2/sdl2_Audio.c upstreamu konwerter
fmt_mus_convert_xmid() (z 1oom) razem z jego stalymi i funkcjami
pomocniczymi, dokleja main() i zapisuje xmi2mid.c. Ten sam kod, ktorego
ReMoM na PC uzywa przed oddaniem muzyki do SDL_mixer - wiec muzyka
wyrenderowana na hoscie jest ta sama co na PC.

UZYCIE:  python3 xmi2mid-gen.py <katalog zrodel ReMoM> <wyjscie.c>
Program: xmi2mid <wpis.bin> <wyjscie.mid>   (wpis z 16-bajtowym naglowkiem 0xDEAF)
"""

import io
import sys

root, cel = sys.argv[1], sys.argv[2]
t = io.open(root + "/platform/sdl2/sdl2_Audio.c", encoding="latin-1").read().replace("\r\n", "\n")

a = t.index("#define HDR_MIDI_LEN")
b = t.index("#define XMID_TICKSPERQ")
b = t.index("\n", b) + 1
stale = t[a:b]

c = t.index("static int8_t xmid_find_free_noteoff(")
d = t.index("bool fmt_mus_convert_xmid(const uint8_t *data_in, uint32_t len_in, uint8_t **data_out_ptr, uint32_t *len_out_ptr, bool *tune_loops)\n{")
e = t.index("\n}\n", d) + 3
funkcje = t[c:e]

zrodlo = """/* xmi2mid.c - WYGENEROWANE przez build/xmi2mid-gen.py z sdl2_Audio.c. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "PFL_Audio_Internal.h"
#define LOG_DEBUG(...) ((void)0)
#define STU_DEBUG_BREAK() ((void)0)
void Audio_Error__STUB(int16_t error) { (void)error; }

%s
%s
int main(int argc, char ** argv)
{
    FILE * f;
    long n;
    uint8_t * we;
    uint8_t * wy = NULL;
    uint32_t wy_len = 0;
    bool petla = false;
    if(argc != 3) { fprintf(stderr, "uzycie: xmi2mid wpis.bin wyjscie.mid\\n"); return 2; }
    f = fopen(argv[1], "rb");
    if(f == NULL) { return 2; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    we = malloc((size_t)n);
    if(fread(we, 1, (size_t)n, f) != (size_t)n) { return 2; }
    fclose(f);
    if(!fmt_mus_convert_xmid(we, (uint32_t)n, &wy, &wy_len, &petla)) { fprintf(stderr, "xmi2mid: konwersja nieudana\\n"); return 1; }
    f = fopen(argv[2], "wb");
    if(f == NULL) { return 2; }
    fwrite(wy, 1, wy_len, f);
    fclose(f);
    printf("%%u %%d\\n", (unsigned)wy_len, petla ? 1 : 0);
    return 0;
}
""" % (stale, funkcje)
if len(sys.argv) > 3 and sys.argv[3] == "biblioteka":
    # bez main() - do linkowania z native/remom/muzyka_konw.c (remom-prefs)
    zrodlo = zrodlo[:zrodlo.index("int main(")]
io.open(cel, "w", encoding="latin-1", newline="\n").write(zrodlo)
print("xmi2mid-gen: %s" % cel)
