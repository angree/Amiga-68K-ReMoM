/*
 * amiga_Audio.c - dzwiek (Platform.h: Platform_Audio_Play_Sound) na Pauli.
 *
 * Silnik podaje surowy wpis dzwiekowy z LBX: 16 B naglowka MoX (0xDEAF,
 * typ: 1 = XMIDI, 2 = VOC), a dalej plik. Jak w sdl2_Audio.c:
 *
 *   VOC  - 26 B naglowka Creative, potem bloki; blok 1 (dane, kodek 0 =
 *          8 bit bez znaku, czestosc 1000000/(256-TC)). Probki trafiaja
 *          do Chip RAM jako 8 bit ZE znakiem (x ^ 0x80) i graja przez DMA
 *          na wolnym kanale Pauli (native/amiga_audio.c z portu OpenXcom).
 *          Gdy wszystkie kanaly graja, nowy efekt przepada (bez miksera -
 *          CPU jest drogie, a gra jest turowa).
 *   XMI  - muzyka: 68020 nie udzwignie syntezy, wiec utwory sa renderowane
 *          NA HOSCIE z LBX gracza (build/muzyka-host.sh) do
 *          Work:muzyka/<FNV-1a 32 calego wpisu>.wav (IMA ADPCM mono) i tu
 *          tylko strumieniowane z dysku (native/amiga_adpcm.c) na kanalach
 *          2 i 3 (AmigaAudio_MusicStart), w petli. Efekty ida wtedy na
 *          kanaly 0 i 1. Brak pliku = cisza i jedna linia w logu.
 *
 * Nic tu nie siega do generatora losowego - slad RNG gry sie nie zmienia.
 */

#include "Platform.h"

#include "../STU/src/STU_LOG.h"

#include "amiga_PFL.h"
#include "amiga_audio.h"
#include "amiga_adpcm.h"
#include "amiga_camd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define AMIGA_SND_HDR      16          /* naglowek wpisu dzwiekowego MoX */
#define AMIGA_VOC_HDR      26          /* "Creative Voice File" */
#define AMIGA_PAL_CLOCK    3546895L    /* zegar Pauli PAL */
#define AMIGA_SFX_VOLUME   48          /* 0..64, jak 0,75 w sdl2 */
#define AMIGA_PAULA_MAXLEN 131070UL

static int amiga_audio_stan = 0;       /* 0 nie probowano, 1 dziala, -1 brak */
static void * amiga_audio_bufor[AMIGA_AUDIO_CHANNELS];
static int amiga_audio_nastepny = 0;
static int amiga_audio_muzyka_zgloszona = 0;

/* muzyka */
#define AMIGA_MUS_CHUNK    4096        /* probek na bufor Pauli (~0,37 s przy 11 kHz) */
#define AMIGA_MUS_VOLUME   40
extern void (*amiga_pompa_muzyki)(void);   /* MoX/src/random.c (latka) */
void Amiga_Audio_Service(void);

static AdpcmStream * amiga_muzyka = NULL;
static unsigned long amiga_muzyka_fnv = 0;
static int amiga_muzyka_petla = 0;

static int Amiga_Muzyka_Refill(void * ud, signed char * dst, int max)
{
    AdpcmStream * s = (AdpcmStream *)ud;
    int n = Adpcm_Decode(s, dst, max);
    if(n <= 0 && amiga_muzyka_petla)
    {
        /* koniec utworu z petla (XMIDI FOR/NEXT) - od poczatku */
        Adpcm_Rewind(s);
        n = Adpcm_Decode(s, dst, max);
    }
    return (n > 0) ? n : 0;         /* 0 = koniec strumienia */
}

static void Amiga_Muzyka_Stop(void)
{
    AmigaAudio_MusicStop();
    if(amiga_muzyka != NULL)
    {
        Adpcm_Close(amiga_muzyka);
        amiga_muzyka = NULL;
    }
    amiga_muzyka_fnv = 0;
}

/* ---- MIDI przez camd.library (music=2, 0.3.0) --------------------------
   XMIDI z LBX -> MIDI konwerterem ReMoM (fmt_mus_convert_xmid, wyciety przez
   build/xmi2mid-gen.py) -> plik w T: -> odtwarzacz z portu OpenTTD
   (native/amiga_camd.c, osobny proces na timer.device). Syntezator jest
   ZEWNETRZNY (moduł MIDI na porcie szeregowym / sterownik CAMD), wiec CPU
   Amigi nie syntetyzuje niczego. Brak camd i portu -> muzyka z plikow. */
#define AMIGA_MIDI_PLIK "T:remom-muzyka.mid"
extern bool fmt_mus_convert_xmid(const uint8_t *data_in, uint32_t len_in, uint8_t **data_out_ptr, uint32_t *len_out_ptr, bool *tune_loops);
static int amiga_midi_stan = 0;          /* 0 nie probowano, 1 dziala, -1 brak */
static unsigned long amiga_midi_fnv = 0;
static int amiga_midi_petla = 0;

static void Amiga_Midi_Serwis(void)
{
    if(amiga_midi_fnv != 0 && !AmigaMidi_IsPlaying())
    {
        if(amiga_midi_petla) { AmigaMidi_Play(AMIGA_MIDI_PLIK); }
        else { amiga_midi_fnv = 0; }
    }
}

/* 1 = obsluzone przez MIDI, 0 = MIDI niedostepne (gra bierze pliki) */
static int Amiga_Audio_Midi(const uint8_t * p, uint32_t rozmiar, unsigned long h)
{
    uint8_t * mid = NULL;
    uint32_t dl = 0;
    bool petla = false;
    FILE * f;
    if(amiga_midi_stan == 0)
    {
        amiga_midi_stan = AmigaMidi_Start() ? 1 : -1;
        printf("[amiga] muzyka MIDI (camd): %s\n", amiga_midi_stan > 0 ? "dziala" : AmigaMidi_LastError());
        fflush(stdout);
        if(amiga_midi_stan > 0) { atexit(AmigaMidi_Shutdown); }
    }
    if(amiga_midi_stan < 0) { return 0; }
    if(h == amiga_midi_fnv) { return 1; }
    if(!fmt_mus_convert_xmid(p, rozmiar, &mid, &dl, &petla)) { return 1; }
    AmigaMidi_Stop();
    f = fopen(AMIGA_MIDI_PLIK, "wb");
    if(f != NULL)
    {
        fwrite(mid, 1, dl, f);
        fclose(f);
        if(AmigaMidi_Play(AMIGA_MIDI_PLIK))
        {
            amiga_midi_fnv = h;
            amiga_midi_petla = petla ? 1 : 0;
            amiga_pompa_muzyki = Amiga_Audio_Service;
        }
    }
    free(mid);
    LOG_INFO(LOG_CAT_PFL, "[amiga] muzyka MIDI: %08lx, %lu B, petla %d", h, (unsigned long)dl, (int)petla);
    return 1;
}

/* raz na klatke (amiga_PFL.c) - dolewa bufory muzyki */
static unsigned long amiga_audio_serwis_licznik = 0;

void Amiga_Audio_Service(void)
{
    if(amiga_midi_stan > 0)
    {
        Amiga_Midi_Serwis();
    }
    if(amiga_muzyka != NULL)
    {
        amiga_audio_serwis_licznik++;
        AmigaAudio_MusicService();
        if(AmigaAudio_MusicFinished())
        {
            /* utwor bez petli dograny - kanaly 2 i 3 wracaja do efektow */
            Amiga_Muzyka_Stop();
        }
    }
}

static void Amiga_Audio_Zamknij(void)
{
    int ch;
    printf("[amiga] muzyka: serwis wolany %lu razy\n", amiga_audio_serwis_licznik);
    fflush(stdout);
    Amiga_Muzyka_Stop();
    AmigaAudio_Close();
    for(ch = 0; ch < AMIGA_AUDIO_CHANNELS; ch++)
    {
        AmigaAudio_FreeSample(amiga_audio_bufor[ch]);
        amiga_audio_bufor[ch] = NULL;
    }
    amiga_audio_stan = 0;
}

static int Amiga_Audio_Gotowe(void)
{
    if(amiga_audio_stan == 0)
    {
        amiga_audio_stan = AmigaAudio_Open() ? 1 : -1;
        if(amiga_audio_stan == 1)
        {
            atexit(Amiga_Audio_Zamknij);
        }
        printf("[amiga] audio.device: %s\n", (amiga_audio_stan == 1) ? "4 kanaly Pauli" : "niedostepne - gra bez dzwieku");
        fflush(stdout);
        LOG_INFO(LOG_CAT_PFL, "[amiga] audio.device %s", (amiga_audio_stan == 1) ? "open" : "unavailable");
    }
    return (amiga_audio_stan == 1);
}

/* Pierwszy wolny kanal, zaczynajac od kolejnego po ostatnio uzytym; -1 gdy
   wszystkie graja. */
static int Amiga_Audio_Wolny_Kanal(void)
{
    int i;
    /* przy muzyce kanaly 2 i 3 sa jej */
    int kanalow = AmigaAudio_MusicActive() ? 2 : AMIGA_AUDIO_CHANNELS;
    for(i = 0; i < kanalow; i++)
    {
        int ch = (amiga_audio_nastepny + i) % kanalow;
        if(AmigaAudio_ChannelIdle(ch))
        {
            amiga_audio_nastepny = (ch + 1) % kanalow;
            return ch;
        }
    }
    return -1;
}

static int16_t Amiga_Audio_VOC(const uint8_t * p, uint32_t rozmiar)
{
    const uint8_t * q;
    uint32_t zostalo;
    uint32_t probek = 0;
    unsigned int tc = 0;
    unsigned long dlugosc = 0;
    unsigned long czestosc;
    signed char * cel;
    int ch;
    int przebieg;

    if(rozmiar <= (uint32_t)(AMIGA_SND_HDR + AMIGA_VOC_HDR))
    {
        return 1;
    }
    if(!Amiga_Audio_Gotowe())
    {
        return -1;
    }

    /* przebieg 0: policz probki; przebieg 1: skopiuj */
    cel = NULL;
    ch = -1;
    for(przebieg = 0; przebieg < 2; przebieg++)
    {
        uint32_t skopiowane = 0;
        q = p + AMIGA_SND_HDR + AMIGA_VOC_HDR;
        zostalo = rozmiar - (AMIGA_SND_HDR + AMIGA_VOC_HDR);
        while(zostalo >= 1)
        {
            uint32_t blok;
            if(q[0] == 0)
            {
                break;              /* koniec pliku */
            }
            if(q[0] != 1 || zostalo < 6)
            {
                break;              /* inne bloki - jak sdl2: koniec */
            }
            blok = (uint32_t)q[1] | ((uint32_t)q[2] << 8) | ((uint32_t)q[3] << 16);
            if(blok < 2 || q[5] != 0)
            {
                break;              /* tylko 8 bit PCM */
            }
            tc = q[4];
            blok -= 2;
            q += 6;
            zostalo -= 6;
            if(blok > zostalo)
            {
                blok = zostalo;
            }
            if(przebieg == 0)
            {
                probek += blok;
            }
            else
            {
                uint32_t i;
                for(i = 0; i < blok && skopiowane < dlugosc; i++)
                {
                    cel[skopiowane++] = (signed char)(q[i] ^ 0x80);
                }
            }
            q += blok;
            zostalo -= blok;
        }
        if(przebieg == 0)
        {
            if(probek < 2 || tc >= 256)
            {
                return 1;
            }
            dlugosc = probek & ~1UL;
            if(dlugosc > AMIGA_PAULA_MAXLEN)
            {
                dlugosc = AMIGA_PAULA_MAXLEN;
            }
            ch = Amiga_Audio_Wolny_Kanal();
            if(ch < 0)
            {
                return -1;          /* wszystkie kanaly zajete - efekt przepada */
            }
            AmigaAudio_FreeSample(amiga_audio_bufor[ch]);
            amiga_audio_bufor[ch] = AmigaAudio_AllocSample(dlugosc);
            if(amiga_audio_bufor[ch] == NULL)
            {
                return -1;          /* brak Chip RAM */
            }
            cel = (signed char *)amiga_audio_bufor[ch];
        }
    }

    czestosc = 1000000UL / (256UL - tc);
    {
        int gra = AmigaAudio_Play(ch, cel, dlugosc, (int)(AMIGA_PAL_CLOCK / (long)czestosc), AMIGA_SFX_VOLUME);
        LOG_INFO(LOG_CAT_PFL, "[amiga] efekt: kanal %d, %lu probek, %lu Hz, start=%d", ch, dlugosc, czestosc, gra);
    }
    return -1;
}

static int16_t Amiga_Audio_Muzyka(const uint8_t * p, uint32_t rozmiar)
{
    unsigned long h = 2166136261UL;
    uint32_t i;
    char plik[40];
    AdpcmStream * s;
    int okres;
    /* Set REMOM_MUZYKA 0 - gra bez muzyki (pomiar, slabsza maszyna);
       zmienna LOKALNA - libnix nie widzi SetEnv */
    static int bez_muzyki = -1;

    if(bez_muzyki < 0)
    {
        const char * e = getenv("REMOM_MUZYKA");
        bez_muzyki = (e != NULL && e[0] == '0') ? 1 : 0;
        if(e == NULL && !amiga_opt_muzyka)
        {
            bez_muzyki = 1;   /* music=0 w Work:amiga.cfg (remom-prefs) */
        }
    }
    if(bez_muzyki)
    {
        return -1;
    }
    for(i = 0; i < rozmiar; i++)
    {
        h = ((h ^ p[i]) * 16777619UL) & 0xFFFFFFFFUL;
    }
    if(amiga_opt_muzyka == 2 && Amiga_Audio_Midi(p, rozmiar, h))
    {
        return -1;                  /* MIDI przez camd.library */
    }
    if(amiga_muzyka != NULL && h == amiga_muzyka_fnv)
    {
        return -1;                  /* ten utwor juz gra */
    }
    if(!Amiga_Audio_Gotowe())
    {
        return -1;
    }
    Amiga_Muzyka_Stop();
    /* <fnv>p.wav - w petli, <fnv>1.wav - raz (build/muzyka-render.py) */
    snprintf(plik, sizeof(plik), "muzyka/%08lxp.wav", h);
    s = Adpcm_Open(plik);
    amiga_muzyka_petla = 1;
    if(s == NULL)
    {
        snprintf(plik, sizeof(plik), "muzyka/%08lx1.wav", h);
        s = Adpcm_Open(plik);
        amiga_muzyka_petla = 0;
    }
    if(s == NULL)
    {
        if(!amiga_audio_muzyka_zgloszona)
        {
            amiga_audio_muzyka_zgloszona = 1;
            printf("[amiga] muzyka: brak %s (build/muzyka-host.sh) - cisza\n", plik);
            fflush(stdout);
        }
        LOG_INFO(LOG_CAT_PFL, "[amiga] muzyka: brak %s", plik);
        return -1;
    }
    okres = (int)(AMIGA_PAL_CLOCK / (long)Adpcm_Rate(s));
    AmigaAudio_MusicSetVolume(AMIGA_MUS_VOLUME);
    if(!AmigaAudio_MusicStart(okres, AMIGA_MUS_CHUNK, Amiga_Muzyka_Refill, s))
    {
        Adpcm_Close(s);
        LOG_INFO(LOG_CAT_PFL, "[amiga] muzyka: AmigaAudio_MusicStart nie ruszyl (%s)", plik);
        return -1;
    }
    amiga_muzyka = s;
    amiga_muzyka_fnv = h;
    /* dolewanie buforow takze w dlugich turach AI (random.c, latka
       latki_pompa_muzyki) */
    amiga_pompa_muzyki = Amiga_Audio_Service;
    LOG_INFO(LOG_CAT_PFL, "[amiga] muzyka: %s, %d Hz, okres %d", plik, Adpcm_Rate(s), okres);
    return -1;
}

int16_t Platform_Audio_Play_Sound(void *sound_buffer, uint32_t sound_buffer_size)
{
    const uint8_t * p = (const uint8_t *)sound_buffer;
    unsigned int typ;

    if(p == NULL || sound_buffer_size < 4 || p[0] != 0xAF || p[1] != 0xDE)
    {
        return 1;
    }
    typ = (unsigned int)p[2] | ((unsigned int)p[3] << 8);
    if(typ == 2)
    {
        return Amiga_Audio_VOC(p, sound_buffer_size);
    }
    if(typ == 1)
    {
        return Amiga_Audio_Muzyka(p, sound_buffer_size);
    }
    return -1;
}
