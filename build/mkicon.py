#!/usr/bin/env python3
"""
mkicon.py - klasyczne ikony AmigaOS (.info) dla binarek gry.

Format przeniesiony z portu OpenXcom (build/mkicon.py tamze), ktory renderuje
sie poprawnie: jeden obraz (bez SelectRender - Intuition odwraca kolory przy
zaznaczeniu), flagi gadzetu GFLG_GADGIMAGE|GFLG_GADGHCOMP = 0x0004, 2 plany
(piora Workbencha 0..3). W ikonie narzedzia zapisany stos 1 MB - start
z Workbencha dostaje tyle samo co Work:run ("Stack 1000000").

Pioro 0 (tlo Workbencha) nie jest uzywane w ogole: kazdy piksel 0 wygladalby
jak dziura na wzorzystym pulpicie.

Rysunek: kapelusz czarodzieja z gwiazda (Master of Magic), pod nim podpis
gry (MOM) albo programu ustawien (PREFS). Zadnej grafiki z gry.

UZYCIE: mkicon.py <katalog wyjsciowy>
"""
import os
import struct
import sys

W, H, DEPTH = 48, 40, 2
NO_POS = 0x80000000

# Workbench 3.x: 0 szary (tlo), 1 czarny, 2 bialy, 3 niebieski
BLACK, WHITE, BLUE = 1, 2, 3

FONT = {
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "G": ("01110", "10001", "10000", "10011", "10001", "10001", "01110"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "F": ("11111", "10000", "10000", "11110", "10000", "10000", "10000"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
}


def text(px, s, x0, y0, colour):
    for ch in s:
        g = FONT[ch]
        for gy, row in enumerate(g):
            for gx, bit in enumerate(row):
                if bit == "1":
                    px[y0 + gy][x0 + gx] = colour
        x0 += 6


def star(px, cx, cy, colour, r):
    for i in range(-r, r + 1):
        px[cy][cx + i] = colour
        px[cy + i][cx] = colour
    if r >= 2:
        for d in (-1, 1):
            px[cy + d][cx + d] = colour
            px[cy + d][cx - d] = colour


def draw(label, tlo=BLUE, kapelusz=WHITE, napis=WHITE):
    """tlo - pole ikony, kapelusz - wypelnienie stozka i ronda, napis - podpis i gwiazdy"""
    px = [[tlo] * W for _ in range(H)]
    for x in range(W):
        px[0][x] = px[H - 1][x] = BLACK
    for y in range(H):
        px[y][0] = px[y][W - 1] = BLACK

    # stozek kapelusza: wierzcholek (27,3) przechylony w prawo, podstawa y=24
    top_x, top_y, base_y, left, right = 27, 3, 24, 12, 38
    for y in range(top_y, base_y + 1):
        t = (y - top_y) / float(base_y - top_y)
        xl = int(round(top_x + (left - top_x) * t))
        xr = int(round(top_x + (right - top_x) * t))
        for x in range(xl, xr + 1):
            edge = (x == xl or x == xr or y == top_y)
            px[y][x] = BLACK if edge else kapelusz
    # opaska kapelusza
    for y in (20, 21):
        for x in range(1, W - 1):
            if px[y][x] == kapelusz:
                px[y][x] = BLACK
    # rondo (elipsa) y 24..27
    for y in range(24, 28):
        half = {24: 18, 25: 19, 26: 18, 27: 15}[y]
        for x in range(24 - half, 25 + half):
            edge = (y in (24, 27)) or x in (24 - half, 24 + half)
            px[y][x] = BLACK if edge else kapelusz
    # gwiazda na stozku i dwie na niebie
    star(px, 25, 14, tlo if tlo != kapelusz else BLACK, 2)
    star(px, 7, 6, napis, 2)
    star(px, 41, 9, napis, 1)
    star(px, 9, 16, napis, 1)

    tw = len(label) * 6 - 1
    text(px, label, (W - tw) // 2, H - 10, napis)
    return px


def planes(px):
    row_bytes = ((W + 15) // 16) * 2
    out = bytearray()
    for p in range(DEPTH):
        for y in range(H):
            row = bytearray(row_bytes)
            for x in range(W):
                if (px[y][x] >> p) & 1:
                    row[x >> 3] |= 0x80 >> (x & 7)
            out += row
    return bytes(out)


def image(px):
    hdr = struct.pack(">hhhhhIBBI", 0, 0, W, H, DEPTH, 1, (1 << DEPTH) - 1, 0, 0)
    return hdr + planes(px)


def icon_tool(px, stack):
    gadget = struct.pack(">IhhhhHHHIIIIIHI",
                         0, 0, 0, W, H,
                         0x0004,       # GFLG_GADGIMAGE | GFLG_GADGHCOMP
                         0x0003,       # RELVERIFY | GADGIMMEDIATE
                         0x0001,       # BOOLGADGET
                         1, 0,         # GadgetRender jest, SelectRender brak
                         0, 0, 0, 0, 0)
    body = struct.pack(">HH", 0xE310, 1) + gadget
    body += struct.pack(">BBIIIIIIi", 3, 0,  # WBTOOL
                        0, 0, NO_POS, NO_POS, 0, 0, stack)
    assert len(body) == 78, len(body)
    return body + image(px)


# binarka -> (podpis, tlo, kapelusz, napis)
TOOLS = {
    "remom": ("MOM", BLUE, WHITE, WHITE),
    "remom-prefs": ("PREFS", WHITE, BLUE, BLACK),
}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    for name, (label, tlo, kap, nap) in TOOLS.items():
        stos = 1000000 if name != "remom-prefs" else 16384
        with open(os.path.join(out, name + ".info"), "wb") as f:
            f.write(icon_tool(draw(label, tlo, kap, nap), stos))
    if len(sys.argv) > 2:
        # podglad PPM (x4) do sprawdzenia rysunku na hoscie
        pal = {0: (170, 170, 170), BLACK: (0, 0, 0), WHITE: (255, 255, 255), BLUE: (102, 136, 187)}
        for name, (label, tlo, kap, nap) in TOOLS.items():
            px = draw(label, tlo, kap, nap)
            with open(os.path.join(sys.argv[2], name + "-ikona.ppm"), "wb") as f:
                f.write(b"P6\n%d %d\n255\n" % (W * 4, H * 4))
                for y in range(H * 4):
                    for x in range(W * 4):
                        f.write(bytes(pal[px[y // 4][x // 4]]))
    print("ikony zapisane w", out)


if __name__ == "__main__":
    main()
