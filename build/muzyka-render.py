#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
muzyka-render.py - muzyka Master of Magic dla Amigi, renderowana NA HOSCIE
z plikow gracza.

68020 nie udzwignie syntezy MIDI z wieloma glosami, wiec kazdy utwor XMIDI
z LBX gracza jest tu zamieniany na plik WAV IMA ADPCM (mono, 11025 Hz),
ktory Amiga odtwarza strumieniowo z dysku (native/amiga_adpcm.c,
AmigaAudio_MusicStart - jak muzyka w porcie OpenXcom).

  1. LBX gracza -> wpisy dzwiekowe typu 1 (0xDEAF, XMIDI);
  2. xmi2mid (konwerter upstreamu, build/xmi2mid-gen.py) -> MIDI;
  3. libfluidsynth (ctypes) + soundfont GM -> PCM 16 bit stereo 11025 Hz;
  4. miks do mono, IMA ADPCM, WAV;
  5. nazwa pliku = FNV-1a 32 CALEGO wpisu (tak, jak go dostaje
     Platform_Audio_Play_Sound) + znacznik petli z XMIDI: <fnv>p.wav gra
     w petli, <fnv>1.wav raz - native/remom/platform_amiga/amiga_Audio.c
     liczy to samo i szuka obu nazw w Work:muzyka/.

Pliki powstaja lokalnie z danych gracza i NIGDY nie trafiaja do repozytorium.

UZYCIE:
  python3 muzyka-render.py <xmi2mid> <soundfont.sf2> <katalog wyjsciowy> <plik.LBX> [...]
"""

import ctypes
import os
import struct
import subprocess
import sys
import tempfile

RATE = int(os.environ.get("RATE", "11025"))   # RATE=22050 dla 22 kHz (2026-09-24)
BLOCK_ALIGN = 1024
SPB = (BLOCK_ALIGN - 4) * 2 + 1        # 2041 probek na blok
MAX_SEKUND = 15 * 60
OGON_SEKUND = 2

IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]
IMA_STEP = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
    41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173,
    190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
    7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767]


def fnv1a32(dane):
    h = 2166136261
    for b in dane:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def wpisy_lbx(sciezka):
    d = open(sciezka, "rb").read()
    n, magia = struct.unpack_from("<HH", d, 0)
    if magia != 0xFEAD:
        return []
    off = struct.unpack_from("<%dI" % (n + 1), d, 8)
    return [(i, d[off[i]:off[i + 1]]) for i in range(n)]


def adpcm_blok(probki, stan_indeks):
    """Jeden blok IMA ADPCM mono: naglowek (pierwsza probka, indeks) + nible."""
    pred = probki[0]
    indeks = stan_indeks
    wyj = bytearray(struct.pack("<hBB", pred, indeks, 0))
    nible = []
    for s in probki[1:]:
        krok = IMA_STEP[indeks]
        roz = s - pred
        nib = 0
        if roz < 0:
            nib = 8
            roz = -roz
        delta = krok >> 3
        if roz >= krok:
            nib |= 4
            roz -= krok
            delta += krok
        krok >>= 1
        if roz >= krok:
            nib |= 2
            roz -= krok
            delta += krok
        krok >>= 1
        if roz >= krok:
            nib |= 1
            delta += krok
        if nib & 8:
            pred -= delta
        else:
            pred += delta
        pred = max(-32768, min(32767, pred))
        indeks = max(0, min(88, indeks + IMA_INDEX[nib]))
        nible.append(nib)
    for i in range(0, len(nible), 2):
        lo = nible[i]
        hi = nible[i + 1] if i + 1 < len(nible) else 0
        wyj.append(lo | (hi << 4))
    wyj.extend(b"\x00" * (BLOCK_ALIGN - len(wyj)))
    return bytes(wyj), indeks


def zapisz_wav_adpcm(sciezka, mono):
    bloki = []
    indeks = 0
    for i in range(0, len(mono), SPB):
        kawal = mono[i:i + SPB]
        if len(kawal) < SPB:
            kawal = kawal + [0] * (SPB - len(kawal))
        blok, indeks = adpcm_blok(kawal, indeks)
        bloki.append(blok)
    dane = b"".join(bloki)
    fmt = struct.pack("<HHIIHHHH", 0x11, 1, RATE, RATE * BLOCK_ALIGN // SPB, BLOCK_ALIGN, 4, 2, SPB)
    fact = struct.pack("<I", len(mono))
    riff = (b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt
            + b"fact" + struct.pack("<I", 4) + fact
            + b"data" + struct.pack("<I", len(dane)) + dane)
    with open(sciezka, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(riff)) + riff)


class Syntezator:
    def __init__(self, soundfont):
        fs = ctypes.CDLL("libfluidsynth.so.3")
        fs.new_fluid_settings.restype = ctypes.c_void_p
        fs.new_fluid_synth.restype = ctypes.c_void_p
        fs.new_fluid_synth.argtypes = [ctypes.c_void_p]
        fs.new_fluid_player.restype = ctypes.c_void_p
        fs.new_fluid_player.argtypes = [ctypes.c_void_p]
        fs.fluid_settings_setnum.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_double]
        fs.fluid_settings_setint.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        fs.fluid_synth_sfload.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        fs.fluid_player_add.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        fs.fluid_player_play.argtypes = [ctypes.c_void_p]
        fs.fluid_player_get_status.argtypes = [ctypes.c_void_p]
        fs.fluid_synth_write_s16.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                             ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                             ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        fs.delete_fluid_player.argtypes = [ctypes.c_void_p]
        fs.delete_fluid_synth.argtypes = [ctypes.c_void_p]
        fs.delete_fluid_settings.argtypes = [ctypes.c_void_p]
        fs.fluid_set_log_function.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p]
        for poziom in range(5):
            fs.fluid_set_log_function(poziom, None, None)
        self.fs = fs
        self.sf = soundfont.encode()

    def renderuj(self, midi):
        fs = self.fs
        ust = fs.new_fluid_settings()
        fs.fluid_settings_setnum(ust, b"synth.sample-rate", float(RATE))
        fs.fluid_settings_setnum(ust, b"synth.gain", 0.5)
        fs.fluid_settings_setint(ust, b"player.reset-synth", 1)
        syn = fs.new_fluid_synth(ust)
        if fs.fluid_synth_sfload(syn, self.sf, 1) < 0:
            raise RuntimeError("soundfont")
        gracz = fs.new_fluid_player(syn)
        fs.fluid_player_add(gracz, midi.encode())
        fs.fluid_player_play(gracz)
        n = 1024
        lewy = (ctypes.c_int16 * n)()
        prawy = (ctypes.c_int16 * n)()
        mono = []
        ogon = 0
        while len(mono) < RATE * MAX_SEKUND:
            gra = fs.fluid_player_get_status(gracz) == 1   # FLUID_PLAYER_PLAYING
            if not gra:
                ogon += n
                if ogon > RATE * OGON_SEKUND:
                    break
            fs.fluid_synth_write_s16(syn, n, lewy, 0, 1, prawy, 0, 1)
            mono.extend((lewy[i] + prawy[i]) // 2 for i in range(n))
        fs.delete_fluid_player(gracz)
        fs.delete_fluid_synth(syn)
        fs.delete_fluid_settings(ust)
        return mono


def main():
    xmi2mid, soundfont, wyjscie = sys.argv[1:4]
    lbxy = sys.argv[4:]
    os.makedirs(wyjscie, exist_ok=True)
    syn = Syntezator(soundfont)
    zrobione = 0
    with tempfile.TemporaryDirectory() as tmp:
        for lbx in lbxy:
            for nr, dane in wpisy_lbx(lbx):
                if len(dane) < 16 or dane[0:4] != b"\xaf\xde\x01\x00":
                    continue
                fnv = "%08x" % fnv1a32(dane)
                if (os.path.exists(os.path.join(wyjscie, fnv + "p.wav"))
                        or os.path.exists(os.path.join(wyjscie, fnv + "1.wav"))):
                    continue
                we = os.path.join(tmp, "w.bin")
                mid = os.path.join(tmp, "w.mid")
                open(we, "wb").write(dane)
                r = subprocess.run([xmi2mid, we, mid], capture_output=True, text=True)
                if r.returncode != 0:
                    print("%s[%d]: xmi2mid nie przeszedl" % (os.path.basename(lbx), nr))
                    continue
                petla = r.stdout.split()[1] == "1"
                nazwa = fnv + ("p.wav" if petla else "1.wav")
                cel = os.path.join(wyjscie, nazwa)
                mono = syn.renderuj(mid)
                zapisz_wav_adpcm(cel, mono)
                zrobione += 1
                print("%s[%d] -> %s  %.1f s  petla=%s" % (os.path.basename(lbx), nr, nazwa,
                                                          len(mono) / RATE, r.stdout.split()[1]))
    print("muzyka-render: nowych plikow %d" % zrobione)


if __name__ == "__main__":
    main()
