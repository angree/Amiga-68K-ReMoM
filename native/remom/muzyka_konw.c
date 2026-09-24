/*
 * muzyka_konw.c - konwerter muzyki na samej Amidze (remom-prefs "Convert music").
 *
 * Gracze prosili (2026-09-24): na PC nie ma dla nich konwertera, a wymuszone
 * 11 kHz nie kazdemu pasuje. Tu wszystko dzieje sie na Amidze, z LBX gracza:
 *   1. MUSIC.LBX / INTROSND.LBX / SNDDRV.LBX -> wpisy 0xDEAF typu 1 (XMIDI);
 *   2. fmt_mus_convert_xmid() - konwerter ReMoM (z 1oom), wyciety mechanicznie
 *      przez build/xmi2mid-gen.py - ten sam, ktorego uzywa host;
 *   3. NASZ syntezator: 16 glosow, fale z tablic 256 probek (sumy harmonicznych
 *      z cwiartki sinusa), obwiednia co CTRL probek, perkusja z szumu.
 *      Brzmi jak prosty moduł, nie jak soundfont - za to miesci sie w kilku KB
 *      i nie potrzebuje zadnego pliku z zewnatrz;
 *   4. IMA ADPCM mono, bloki 1024 B (jak build/muzyka-render.py), WAV.
 * ZERO float/double (CLAUDE.md): czestotliwosci, bend i sinus to stale
 * policzone raz na PC (liczby w tablicach ponizej), czas w liczbach 64-bit.
 * Nigdy sprintf - tylko snprintf.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "muzyka_konw.h"

bool fmt_mus_convert_xmid(const uint8_t *data_in, uint32_t len_in, uint8_t **data_out_ptr, uint32_t *len_out_ptr, bool *tune_loops);

#define CTRL 32                 /* probek na krok obwiedni */
#define MAXV 16                 /* glosow naraz */
#define SPB 2041                /* probek na blok ADPCM */
#define BLOK 1024
#define MAX_SEK (15 * 60)
#define OGON_SEK 2
#define MAX_UTW 256
#define MAX_SCIEZEK 16
#define NAGLOWEK 60

static const char * const LBXY[] = { "MUSIC.LBX", "INTROSND.LBX", "SNDDRV.LBX" };
#define ILE_LBX 3

/* czestotliwosc nut 0..11 * 2^26 (nuta n: BAZA[n%12] >> (10 - n/12), wynik Hz*65536) */
static const uint32_t BAZA[12] = { 548668578, 581294109, 615859655, 652480576, 691279090,
    732384684, 775934544, 822074013, 870957077, 922746880, 977616265, 1035748353 };
/* 2^(polton/12)*65536 dla bend -2..+2 poltonow co 1/8 */
static const uint32_t BEND[33] = { 58386, 58809, 59235, 59664, 60097, 60532, 60971, 61413,
    61858, 62306, 62757, 63212, 63670, 64132, 64596, 65065, 65536, 66011, 66489, 66971, 67456,
    67945, 68438, 68933, 69433, 69936, 70443, 70953, 71468, 71985, 72507, 73032, 73562 };
/* cwiartka sinusa, 32767*sin(pi/2*i/64) */
static const int16_t CWIARTKA[65] = { 0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179,
    7962, 8739, 9512, 10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846,
    17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811,
    25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956, 30273,
    30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678,
    32728, 32757, 32767 };

static const int16_t IMA_INDEKS[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
static const int16_t IMA_KROK[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28,
    31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
    230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
    1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767 };

/* fale */
enum { F_SIN, F_TRI, F_PIL, F_PROST, F_ORG, F_PIAN, F_ILE };
static const int16_t HARM[F_ILE][9] = {       /* amplituda harmonicznej 1..9, Q15 */
    { 32767 },
    { 32767, 0, -3641, 0, 1311, 0, -669 },
    { 32767, 16384, 10922, 8192, 6553, 5461, 4681, 4096 },
    { 32767, 0, 10922, 0, 6553, 0, 4681, 0, 3641 },
    { 32767, 16000, 8000, 8000, 0, 4000, 0, 3000 },
    { 32767, 14000, 7000, 4000, 2000 } };

/* instrument dla rodziny GM (program >> 3): fala, atak ms, zanik ms (0 = trzyma), poziom
   podtrzymania Q15, wybrzmienie ms. -1 jako fala = szum. */
typedef struct { int8_t fala; int16_t atak, zanik, podtrz, wybrz; } instr_t;
static const instr_t INSTR[16] = {
    { F_PIAN, 2, 1200, 0, 250 },   { F_SIN, 1, 500, 0, 200 },     { F_ORG, 10, 0, 26000, 120 },
    { F_PIL, 2, 900, 0, 200 },     { F_TRI, 3, 1000, 6000, 150 }, { F_PIL, 80, 0, 28000, 300 },
    { F_PIL, 120, 0, 26000, 400 }, { F_PIL, 30, 0, 26000, 150 },  { F_PROST, 20, 0, 24000, 120 },
    { F_TRI, 30, 0, 26000, 150 },  { F_PROST, 5, 0, 22000, 150 }, { F_TRI, 200, 0, 26000, 600 },
    { F_SIN, 100, 0, 22000, 600 }, { F_PIL, 2, 700, 0, 200 },     { F_SIN, 1, 300, 0, 100 },
    { -1, 5, 400, 0, 200 } };

typedef struct
{
    uint8_t stan;          /* 0 wolny, 1 atak, 2 zanik/podtrzymanie, 3 wybrzmienie */
    uint8_t kanal, nuta, szum, trzymany;
    uint32_t faza, krok, baza_krok, krok_min;
    uint32_t mnoz_krok;    /* opadanie wysokosci (beben), Q16; 0 = brak */
    int32_t poziom, atak, podtrz, gain;
    uint32_t mnoz_zanik, mnoz_wybrz;
    int16_t amp;
    const int16_t * fala;
} glos_t;

typedef struct { uint8_t program, glosnosc, ekspresja, pedal; uint16_t bend; } kanal_t;

typedef struct { const uint8_t * p; const uint8_t * kon; uint32_t tik; uint8_t status; uint8_t koniec; } sciezka_t;

typedef struct { uint8_t lbx; uint32_t off, dl; } utwor_t;

struct konw
{
    char kat[128];
    long rate;
    utwor_t utw[MAX_UTW];
    int ile, nast, zrobione;
    uint32_t fnv_zrobione[MAX_UTW];
    int ile_fnv;
    int16_t fale[F_ILE][256];

    /* biezacy utwor */
    FILE * wy;
    char cel[160], tmp[160];
    uint8_t * midi;
    sciezka_t sc[MAX_SCIEZEK];
    int ile_sc;
    uint32_t podzial, tempo, baza_tik;
    uint64_t baza_probka;      /* w probkach */
    uint32_t teraz;            /* probek wyrenderowanych */
    uint32_t ogon;
    kanal_t kan[16];
    glos_t g[MAXV];
    uint32_t szum;
    int16_t probki[SPB];
    int zapisane_bloki;
    int ima_indeks;
    int nr;                    /* numer biezacego utworu dla statusu */
};

/* ------------------------------------------------------------------------ */

static int sin256(int i)
{
    int r = i & 63;
    switch ((i >> 6) & 3) {
    case 0: return CWIARTKA[r];
    case 1: return CWIARTKA[64 - r];
    case 2: return -CWIARTKA[r];
    default: return -CWIARTKA[64 - r];
    }
}

static void zrob_fale(konw_t * k)
{
    int f, i, h;
    for (f = 0; f < F_ILE; f++) {
        int32_t tmp[256];
        int32_t max = 1;
        for (i = 0; i < 256; i++) {
            int32_t s = 0;
            for (h = 0; h < 9; h++) {
                if (HARM[f][h]) s += (HARM[f][h] * sin256((i * (h + 1)) & 255)) >> 15;
            }
            tmp[i] = s;
            if (s > max) max = s;
            if (-s > max) max = -s;
        }
        for (i = 0; i < 256; i++) k->fale[f][i] = (int16_t)((tmp[i] * 30000) / max);
    }
}

/* mnoznik Q16 na krok CTRL probek, ktory daje zanik o stalej czasowej ms */
static uint32_t mnoz_ms(konw_t * k, int ms)
{
    uint32_t d;
    if (ms <= 0) return 65536;
    d = (uint32_t)((65536ULL * CTRL * 1000) / ((uint64_t)ms * (uint64_t)k->rate));
    if (d > 60000) d = 60000;
    return 65536 - d;
}

static int32_t atak_ms(konw_t * k, int ms)
{
    int32_t a;
    if (ms <= 0) return 32767;
    a = (int32_t)((32767LL * CTRL * 1000) / ((int64_t)ms * k->rate));
    return a < 1 ? 1 : a;
}

static uint32_t krok_nuty(konw_t * k, int nuta)
{
    uint32_t f16;
    if (nuta < 0) nuta = 0;
    if (nuta > 127) nuta = 127;
    f16 = BAZA[nuta % 12] >> (10 - nuta / 12);
    return (uint32_t)(((uint64_t)f16 << 16) / (uint64_t)k->rate);
}

static uint32_t le32(const uint8_t * p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t be32(const uint8_t * p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | (p[2] << 8) | p[3]; }

static void put16(uint8_t * p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t * p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

/* ------------------------------------------------------------------------ */
/*  Kolejka utworow                                                          */
/* ------------------------------------------------------------------------ */

static void sciezka_lbx(konw_t * k, int nr, char * dst, int cap)
{
    (void)k;
    snprintf(dst, (size_t)cap, "%s", LBXY[nr]);
}

konw_t * Konw_Start(const char * katalog, long rate, int max_utworow, char * blad, int cap)
{
    konw_t * k = (konw_t *)calloc(1, sizeof(konw_t));
    int l, i;
    if (k == NULL) { snprintf(blad, (size_t)cap, "Not enough memory."); return NULL; }
    snprintf(k->kat, sizeof k->kat, "%s", katalog);
    k->rate = rate;
    k->szum = 0x12345678UL;
    zrob_fale(k);

    for (l = 0; l < ILE_LBX; l++) {
        char nazwa[64];
        uint8_t nag[8];
        FILE * f;
        int n;
        sciezka_lbx(k, l, nazwa, (int)sizeof nazwa);
        f = fopen(nazwa, "rb");
        if (f == NULL) continue;
        if (fread(nag, 1, 8, f) == 8 && (nag[2] | (nag[3] << 8)) == 0xFEAD) {
            uint8_t * off;
            n = nag[0] | (nag[1] << 8);
            off = (uint8_t *)malloc((size_t)(n + 1) * 4);
            if (off != NULL && fread(off, 4, (size_t)(n + 1), f) == (size_t)(n + 1)) {
                for (i = 0; i < n && k->ile < MAX_UTW; i++) {
                    uint8_t syg[4];
                    uint32_t o = le32(off + i * 4), e = le32(off + i * 4 + 4);
                    if (e <= o + 16) continue;
                    if (fseek(f, (long)o, SEEK_SET) != 0 || fread(syg, 1, 4, f) != 4) continue;
                    if (syg[0] != 0xAF || syg[1] != 0xDE || syg[2] != 1 || syg[3] != 0) continue;
                    k->utw[k->ile].lbx = (uint8_t)l;
                    k->utw[k->ile].off = o;
                    k->utw[k->ile].dl = e - o;
                    k->ile++;
                }
            }
            free(off);
        }
        fclose(f);
    }
    if (k->ile == 0) {
        snprintf(blad, (size_t)cap, "No MUSIC.LBX here - copy the game's LBX files first.");
        free(k);
        return NULL;
    }
    if (max_utworow > 0 && max_utworow < k->ile) k->ile = max_utworow;
    return k;
}

int Konw_Zrobione(konw_t * k) { return k->zrobione; }
int Konw_Ile(konw_t * k) { return k->ile; }

/* ------------------------------------------------------------------------ */
/*  MIDI                                                                     */
/* ------------------------------------------------------------------------ */

static uint32_t vlq(sciezka_t * s)
{
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4 && s->p < s->kon; i++) {
        uint8_t b = *s->p++;
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return v;
}

static int midi_start(konw_t * k, const uint8_t * m, uint32_t dl)
{
    const uint8_t * p = m, * kon = m + dl;
    int n, i;
    if (dl < 14 || memcmp(p, "MThd", 4) != 0) return 0;
    n = (p[10] << 8) | p[11];
    k->podzial = (uint32_t)((p[12] << 8) | p[13]);
    if (k->podzial == 0 || (k->podzial & 0x8000)) k->podzial = 60;
    p += 8 + be32(p + 4);
    k->ile_sc = 0;
    for (i = 0; i < n && p + 8 <= kon && k->ile_sc < MAX_SCIEZEK; i++) {
        uint32_t d = be32(p + 4);
        if (memcmp(p, "MTrk", 4) == 0) {
            sciezka_t * s = &k->sc[k->ile_sc++];
            s->p = p + 8;
            s->kon = (p + 8 + d <= kon) ? p + 8 + d : kon;
            s->status = 0;
            s->koniec = 0;
            s->tik = vlq(s);
        }
        p += 8 + d;
    }
    k->tempo = 500000;
    k->baza_tik = 0;
    k->baza_probka = 0;
    return k->ile_sc > 0;
}

static uint32_t tik_na_probke(konw_t * k, uint32_t tik)
{
    uint64_t d = (uint64_t)(tik - k->baza_tik) * k->tempo * (uint64_t)k->rate;
    return (uint32_t)(k->baza_probka + d / (1000000ULL * k->podzial));
}

static void glos_wyzeruj_kanal(konw_t * k, int ch)
{
    kanal_t * c = &k->kan[ch];
    c->program = 0; c->glosnosc = 100; c->ekspresja = 127; c->pedal = 0; c->bend = 8192;
}

static void ustaw_gain(konw_t * k, glos_t * g, int vel)
{
    kanal_t * c = &k->kan[g->kanal];
    g->gain = (int32_t)(((uint32_t)vel * c->glosnosc * c->ekspresja) >> 6);
}

static glos_t * wolny_glos(konw_t * k)
{
    int i, best = 0;
    int32_t min = 0x7FFFFFFF;
    for (i = 0; i < MAXV; i++) {
        int32_t w;
        if (k->g[i].stan == 0) return &k->g[i];
        w = k->g[i].poziom + (k->g[i].stan == 3 ? 0 : 40000);
        if (w < min) { min = w; best = i; }
    }
    return &k->g[best];
}

static void nuta_on(konw_t * k, int ch, int nuta, int vel)
{
    glos_t * g = wolny_glos(k);
    kanal_t * c = &k->kan[ch];
    memset(g, 0, sizeof *g);
    g->kanal = (uint8_t)ch;
    g->nuta = (uint8_t)nuta;
    if (ch == 9) {
        /* perkusja GM: beben i tomy jako opadajacy sinus, reszta szum */
        int zanik = 100, wys = 0, glosn = vel;
        g->szum = 1;
        if (nuta == 35 || nuta == 36) { g->szum = 0; wys = 43; zanik = 180; g->mnoz_krok = 60000; }
        else if (nuta == 41 || nuta == 43 || nuta == 45 || nuta == 47 || nuta == 48 || nuta == 50) {
            g->szum = 0; wys = nuta + 5; zanik = 250; g->mnoz_krok = 64000;
        }
        else if (nuta == 38 || nuta == 40) { zanik = 120; }
        else if (nuta == 42 || nuta == 44) { zanik = 40; glosn = vel * 2 / 3; }
        else if (nuta == 46) { zanik = 200; glosn = vel * 2 / 3; }
        else if (nuta == 49 || nuta == 57 || nuta == 51 || nuta == 59 || nuta == 52 || nuta == 55) {
            zanik = 700; glosn = vel / 2;
        }
        g->fala = k->fale[F_SIN];
        g->baza_krok = g->krok = krok_nuty(k, wys);
        g->krok_min = krok_nuty(k, wys - 18);
        g->atak = 32767;
        g->podtrz = 0;
        g->mnoz_zanik = mnoz_ms(k, zanik);
        g->mnoz_wybrz = g->mnoz_zanik;
        ustaw_gain(k, g, glosn);
    } else {
        const instr_t * in = &INSTR[c->program >> 3];
        g->szum = (in->fala < 0);
        g->fala = k->fale[in->fala < 0 ? F_SIN : in->fala];
        g->baza_krok = krok_nuty(k, nuta);
        g->krok = (uint32_t)(((uint64_t)g->baza_krok * BEND[c->bend >> 9]) >> 16);
        g->atak = atak_ms(k, in->atak);
        g->podtrz = in->podtrz;
        g->mnoz_zanik = mnoz_ms(k, in->zanik);
        g->mnoz_wybrz = mnoz_ms(k, in->wybrz);
        ustaw_gain(k, g, vel);
    }
    g->faza = k->szum;
    g->stan = 1;
}

static void nuta_off(konw_t * k, int ch, int nuta)
{
    int i;
    for (i = 0; i < MAXV; i++) {
        glos_t * g = &k->g[i];
        if (g->stan && g->stan != 3 && g->kanal == ch && g->nuta == nuta) {
            if (k->kan[ch].pedal) g->trzymany = 1;
            else g->stan = 3;
        }
    }
}

static void zdarzenie(konw_t * k, sciezka_t * s)
{
    uint8_t st;
    if (s->p >= s->kon) { s->koniec = 1; return; }
    st = *s->p;
    if (st & 0x80) s->p++;
    else st = s->status;               /* running status */
    if (st == 0xFF) {
        uint8_t typ;
        uint32_t d;
        if (s->p >= s->kon) { s->koniec = 1; return; }
        typ = *s->p++;
        d = vlq(s);
        if (typ == 0x2F) { s->koniec = 1; return; }
        if (typ == 0x51 && d == 3 && s->p + 3 <= s->kon) {
            uint32_t t = ((uint32_t)s->p[0] << 16) | (s->p[1] << 8) | s->p[2];
            k->baza_probka = tik_na_probke(k, s->tik);
            k->baza_tik = s->tik;
            if (t) k->tempo = t;
        }
        s->p += d;
    } else if (st == 0xF0 || st == 0xF7) {
        uint32_t d = vlq(s);
        s->p += d;
    } else if (st >= 0x80) {
        int ch = st & 15, a = 0, b = 0;
        s->status = st;
        if (s->p < s->kon) a = *s->p++ & 0x7F;
        if ((st & 0xF0) != 0xC0 && (st & 0xF0) != 0xD0 && s->p < s->kon) b = *s->p++ & 0x7F;
        switch (st & 0xF0) {
        case 0x90: if (b) { nuta_on(k, ch, a, b); break; }
                   /* fall through - nuta z predkoscia 0 to off */
        case 0x80: nuta_off(k, ch, a); break;
        case 0xC0: k->kan[ch].program = (uint8_t)a; break;
        case 0xB0:
            if (a == 7) k->kan[ch].glosnosc = (uint8_t)b;
            else if (a == 11) k->kan[ch].ekspresja = (uint8_t)b;
            else if (a == 64) {
                k->kan[ch].pedal = (uint8_t)(b >= 64);
                if (b < 64) {
                    int i;
                    for (i = 0; i < MAXV; i++)
                        if (k->g[i].stan && k->g[i].kanal == ch && k->g[i].trzymany) k->g[i].stan = 3;
                }
            } else if (a == 121) { glos_wyzeruj_kanal(k, ch); }
            else if (a == 123 || a == 120) {
                int i;
                for (i = 0; i < MAXV; i++) if (k->g[i].stan && k->g[i].kanal == ch) k->g[i].stan = 3;
            }
            break;
        case 0xE0: {
            int i;
            k->kan[ch].bend = (uint16_t)(a | (b << 7));
            for (i = 0; i < MAXV; i++) {
                glos_t * g = &k->g[i];
                if (g->stan && g->kanal == ch && !g->szum)
                    g->krok = (uint32_t)(((uint64_t)g->baza_krok * BEND[k->kan[ch].bend >> 9]) >> 16);
            }
            break; }
        default: break;
        }
    } else {
        s->koniec = 1;                  /* smiec - konczymy sciezke */
        return;
    }
    if (s->p >= s->kon) { s->koniec = 1; return; }
    s->tik += vlq(s);
}

/* ------------------------------------------------------------------------ */
/*  Synteza                                                                  */
/* ------------------------------------------------------------------------ */

static void obwiednia(glos_t * g)
{
    switch (g->stan) {
    case 1:
        g->poziom += g->atak;
        if (g->poziom >= 32767) { g->poziom = 32767; g->stan = 2; }
        break;
    case 2:
        if (g->poziom > g->podtrz) {
            g->poziom = (int32_t)(((uint32_t)g->poziom * g->mnoz_zanik) >> 16);
            if (g->poziom < g->podtrz) g->poziom = g->podtrz;
        }
        if (g->podtrz == 0 && g->poziom < 24) g->stan = 0;
        break;
    case 3:
        g->poziom = (int32_t)(((uint32_t)g->poziom * g->mnoz_wybrz) >> 16);
        if (g->poziom < 24) g->stan = 0;
        break;
    default:
        break;
    }
    g->amp = (int16_t)((g->poziom * g->gain) >> 15);
    if (g->mnoz_krok && g->krok > g->krok_min)
        g->krok = (uint32_t)(((uint64_t)g->krok * g->mnoz_krok) >> 16);
}

static int renderuj(konw_t * k, int16_t * wy, int n)
{
    int32_t mix[CTRL];
    int i, v, aktywne = 0;
    memset(mix, 0, sizeof(int32_t) * (size_t)n);
    for (v = 0; v < MAXV; v++) {
        glos_t * g = &k->g[v];
        int32_t a;
        if (!g->stan) continue;
        aktywne++;
        obwiednia(g);
        a = g->amp;
        if (g->szum) {
            uint32_t x = g->faza;
            for (i = 0; i < n; i++) {
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                mix[i] += ((int32_t)(int16_t)(x >> 16) * a) >> 15;
            }
            g->faza = x;
        } else {
            const int16_t * f = g->fala;
            uint32_t p = g->faza, s = g->krok;
            for (i = 0; i < n; i++) {
                mix[i] += ((int32_t)f[p >> 24] * a) >> 15;
                p += s;
            }
            g->faza = p;
        }
    }
    for (i = 0; i < n; i++) {
        int32_t s = (mix[i] * 3) >> 4;   /* 5/16 przesterowywalo (test PC 2026-09-24) */
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        wy[i] = (int16_t)s;
    }
    return aktywne;
}

/* ------------------------------------------------------------------------ */
/*  IMA ADPCM + WAV                                                          */
/* ------------------------------------------------------------------------ */

static void adpcm_blok(konw_t * k, const int16_t * pr, uint8_t * wy)
{
    int32_t pred = pr[0];
    int indeks = k->ima_indeks, i, nib_nr = 0;
    memset(wy, 0, BLOK);
    put16(wy, (uint32_t)(uint16_t)pr[0]);
    wy[2] = (uint8_t)indeks;
    for (i = 1; i < SPB; i++) {
        int krok = IMA_KROK[indeks], roz = pr[i] - pred, nib = 0, delta;
        if (roz < 0) { nib = 8; roz = -roz; }
        delta = krok >> 3;
        if (roz >= krok) { nib |= 4; roz -= krok; delta += krok; }
        krok >>= 1;
        if (roz >= krok) { nib |= 2; roz -= krok; delta += krok; }
        krok >>= 1;
        if (roz >= krok) { nib |= 1; delta += krok; }
        pred += (nib & 8) ? -delta : delta;
        if (pred > 32767) pred = 32767;
        if (pred < -32768) pred = -32768;
        indeks += IMA_INDEKS[nib];
        if (indeks < 0) indeks = 0;
        if (indeks > 88) indeks = 88;
        wy[4 + (nib_nr >> 1)] |= (uint8_t)((nib_nr & 1) ? (nib << 4) : nib);
        nib_nr++;
    }
    k->ima_indeks = indeks;
}

static void naglowek(konw_t * k, uint8_t * h, uint32_t probek, uint32_t dane)
{
    memcpy(h, "RIFF", 4); put32(h + 4, NAGLOWEK - 8 + dane);
    memcpy(h + 8, "WAVEfmt ", 8); put32(h + 16, 20);
    put16(h + 20, 0x11); put16(h + 22, 1); put32(h + 24, (uint32_t)k->rate);
    put32(h + 28, (uint32_t)(k->rate * BLOK / SPB)); put16(h + 32, BLOK); put16(h + 34, 4);
    put16(h + 36, 2); put16(h + 38, SPB);
    memcpy(h + 40, "fact", 4); put32(h + 44, 4); put32(h + 48, probek);
    memcpy(h + 52, "data", 4); put32(h + 56, dane);
}

/* ------------------------------------------------------------------------ */
/*  Krok pracy                                                               */
/* ------------------------------------------------------------------------ */

static uint32_t fnv1a(const uint8_t * p, uint32_t n)
{
    uint32_t h = 2166136261UL;
    while (n--) { h ^= *p++; h *= 16777619UL; }
    return h;
}

static void zamknij_biezacy(konw_t * k, int udany)
{
    if (k->wy != NULL) {
        fclose(k->wy);
        k->wy = NULL;
        if (udany) {
            remove(k->cel);
            if (rename(k->tmp, k->cel) == 0) k->zrobione++;
        } else {
            remove(k->tmp);
        }
    }
    free(k->midi);
    k->midi = NULL;
}

/* otwiera nastepny utwor; 1 = jest, 0 = kolejka pusta */
static int nastepny(konw_t * k)
{
    while (k->nast < k->ile) {
        utwor_t * u = &k->utw[k->nast++];
        char nazwa[64];
        uint8_t * we;
        uint8_t * midi = NULL;
        uint32_t midi_dl = 0, h;
        bool petla = false;
        FILE * f;
        int i, dup = 0;
        uint8_t nag[NAGLOWEK];

        sciezka_lbx(k, u->lbx, nazwa, (int)sizeof nazwa);
        f = fopen(nazwa, "rb");
        if (f == NULL) continue;
        we = (uint8_t *)malloc(u->dl);
        if (we == NULL || fseek(f, (long)u->off, SEEK_SET) != 0 || fread(we, 1, u->dl, f) != u->dl) {
            fclose(f); free(we); continue;
        }
        fclose(f);
        h = fnv1a(we, u->dl);
        for (i = 0; i < k->ile_fnv; i++) if (k->fnv_zrobione[i] == h) dup = 1;
        if (dup || !fmt_mus_convert_xmid(we, u->dl, &midi, &midi_dl, &petla)) { free(we); free(midi); continue; }
        free(we);
        k->fnv_zrobione[k->ile_fnv++] = h;
        if (!midi_start(k, midi, midi_dl)) { free(midi); continue; }
        k->midi = midi;

        snprintf(k->cel, sizeof k->cel, "%s/%08lx%s.wav", k->kat, (unsigned long)h, petla ? "p" : "1");
        /* wznowienie po przerwaniu: gotowy plik z ta sama czestotliwoscia zostaje */
        f = fopen(k->cel, "rb");
        if (f != NULL) {
            uint8_t n28[28];
            int ten_sam = (fread(n28, 1, 28, f) == 28 && le32(n28 + 24) == (uint32_t)k->rate);
            fclose(f);
            if (ten_sam) { free(midi); k->zrobione++; continue; }
        }
        /* wlasny plik tymczasowy: dwie konwersje naraz (np. Shell i okno) pisaly do
           jednego konwersja.tmp i druga dostawala blad zapisu (developer, 2026-09-24) */
        snprintf(k->tmp, sizeof k->tmp, "%s/konw%08lx.tmp", k->kat, (unsigned long)(uintptr_t)k);
        k->wy = fopen(k->tmp, "wb");
        if (k->wy == NULL) { free(midi); k->midi = NULL; return -1; }
        memset(nag, 0, sizeof nag);
        fwrite(nag, 1, NAGLOWEK, k->wy);
        for (i = 0; i < 16; i++) glos_wyzeruj_kanal(k, i);
        memset(k->g, 0, sizeof k->g);
        k->teraz = 0;
        k->ogon = 0;
        k->zapisane_bloki = 0;
        k->ima_indeks = 0;
        k->nr = k->nast;
        return 1;
    }
    return 0;
}

/* jeden blok ADPCM; 1 = utwor trwa, 0 = skonczony */
static int blok(konw_t * k)
{
    int pos = 0, i, koniec = 0;
    uint8_t wy[BLOK];
    while (pos < SPB) {
        int n = SPB - pos, grane = 1, akt;
        if (n > CTRL) n = CTRL;
        /* zdarzenia do tej chwili (kwantowane do CTRL probek) */
        while (grane) {
            sciezka_t * min = NULL;
            grane = 0;
            for (i = 0; i < k->ile_sc; i++)
                if (!k->sc[i].koniec && (min == NULL || k->sc[i].tik < min->tik)) min = &k->sc[i];
            if (min != NULL && tik_na_probke(k, min->tik) <= k->teraz) { zdarzenie(k, min); grane = 1; }
            if (min == NULL) grane = 0;
        }
        akt = renderuj(k, k->probki + pos, n);
        pos += n;
        k->teraz += (uint32_t)n;
        for (i = 0; i < k->ile_sc; i++) if (!k->sc[i].koniec) break;
        if (i == k->ile_sc) {
            k->ogon += (uint32_t)n;
            if (k->ogon >= (uint32_t)(k->rate * OGON_SEK) || (akt == 0 && k->ogon > (uint32_t)(k->rate / 4)))
                koniec = 1;
        }
        if (k->teraz >= (uint32_t)(k->rate * MAX_SEK)) koniec = 1;
        if (koniec) break;
    }
    if (pos < SPB) memset(k->probki + pos, 0, sizeof(int16_t) * (size_t)(SPB - pos));
    adpcm_blok(k, k->probki, wy);
    fwrite(wy, 1, BLOK, k->wy);
    k->zapisane_bloki++;
    return !koniec;
}

int Konw_Krok(konw_t * k, char * status, int cap)
{
    int b;
    if (k->wy == NULL) {
        int r = nastepny(k);
        if (r < 0) { snprintf(status, (size_t)cap, "Cannot write to %s - disk full or write protected?", k->kat); return -1; }
        if (r == 0) {
            snprintf(status, (size_t)cap, "Done: %d music files at %ld Hz.", k->zrobione, k->rate);
            return 0;
        }
    }
    for (b = 0; b < 2; b++) {
        if (!blok(k)) {
            uint8_t nag[NAGLOWEK];
            naglowek(k, nag, (uint32_t)k->zapisane_bloki * SPB, (uint32_t)k->zapisane_bloki * BLOK);
            fseek(k->wy, 0, SEEK_SET);
            fwrite(nag, 1, NAGLOWEK, k->wy);
            zamknij_biezacy(k, 1);
            break;
        }
    }
    snprintf(status, (size_t)cap, "Converting track %d of %d (%lu s)...", k->nr, k->ile,
             (unsigned long)(k->teraz / (uint32_t)k->rate));
    return 1;
}

void Konw_Koniec(konw_t * k)
{
    if (k == NULL) return;
    zamknij_biezacy(k, 0);
    free(k);
}
