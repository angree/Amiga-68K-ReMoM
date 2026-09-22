/* AmiXcom music: software wavetable replayer for X-COM's GM.CAT tunes.
 * See amiga_music.h for the why. Plain C, no Amiga headers, no floating point.
 *
 * Chain:  GM.CAT entry -> event list (mirrors OpenXcom's GMCat.cpp exactly)
 *         -> 50 Hz sequencer -> N-voice mixer -> 8-bit signed mono @ 22050 Hz
 *
 * Everything is integer: sample positions are 16.16 fixed point, pitch comes
 * from a 193-entry semitone table, volume from a 65x256 table. The bank stores
 * the pitch correction folded into each sample's rate, so the replayer needs
 * no cents arithmetic.
 *
 * Host verification:
 *   gcc -O2 -DMUSIC_HOST_TEST amiga_music.c -o musictest
 *   musictest music.bnk GM.CAT <tune> out.wav
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "amiga_music.h"

void SDLmini_Log(const char *msg);

#ifdef MUSIC_HOST_TEST
/* MUSIC_HOST_TEST stub: the host build has no port log. */
void SDLmini_Log(const char *msg) { (void)msg; }
#endif

/* ------------------------------------------------------------------ bank */

typedef struct {
	unsigned char  flags;      /* bit0 looping, bit1 untuned */
	unsigned char  root;
	unsigned long  rate;       /* Hz at 'root', SF2 cents already folded in */
	unsigned long  length;     /* samples */
	unsigned long  ls, le;     /* loop start/end, samples */
	const signed char *data;
} MusZone;

static MusZone       *s_zones;
static unsigned short s_nzones;
static unsigned short s_nmaps;
static unsigned short *s_maps;        /* s_nmaps * 128 zone indices */
static unsigned short  s_patchmap[128];
static unsigned short  s_drummap[128];
static signed char    *s_blob;
static int             s_haveBank;

#define NOZONE 0xFFFFu

static unsigned short rd16(const unsigned char *p)
{
	return (unsigned short)(((unsigned short)p[0] << 8) | p[1]);
}

static unsigned long rd32(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16)
	     | ((unsigned long)p[2] << 8)  |  (unsigned long)p[3];
}

/* GM.CAT is a DOS file: its lengths are little-endian. */
static unsigned long rd32_le(const unsigned char *p)
{
	return  (unsigned long)p[0]         | ((unsigned long)p[1] << 8)
	     | ((unsigned long)p[2] << 16)  | ((unsigned long)p[3] << 24);
}

int AmigaMusic_HaveBank(void) { return s_haveBank; }

void AmigaMusic_FreeBank(void)
{
	AmigaMusic_Stop();
	if (s_zones) { free(s_zones); s_zones = NULL; }
	if (s_maps)  { free(s_maps);  s_maps  = NULL; }
	if (s_blob)  { free(s_blob);  s_blob  = NULL; }
	s_nzones = s_nmaps = 0;
	s_haveBank = 0;
}

int AmigaMusic_LoadBank(const char *path)
{
	FILE *f;
	unsigned char hdr[16];
	unsigned char *tbl = NULL;
	unsigned long sbytes, tblbytes;
	unsigned short nz, nm, flags;
	unsigned long i;
	const unsigned char *p;

	if (s_haveBank) return 1;
	if (path == NULL) return 0;

	f = fopen(path, "rb");
	if (f == NULL) return 0;

	if (fread(hdr, 1, 16, f) != 16) { fclose(f); return 0; }
	if (hdr[0] != 'A' || hdr[1] != 'X' || hdr[2] != 'M' || hdr[3] != '2') {
		fclose(f); return 0;
	}
	flags  = rd16(hdr + 6);
	nz     = rd16(hdr + 8);
	nm     = rd16(hdr + 10);
	sbytes = rd32(hdr + 12);
	if (flags & 4) { fclose(f); return 0; }     /* 16-bit banks not supported */
	if (nz == 0 || nz > 4000 || nm > 128) { fclose(f); return 0; }

	/* patch map + drum map + key maps + zone records, read in one go */
	tblbytes = 256UL + 256UL + (unsigned long)nm * 256UL + (unsigned long)nz * 24UL;
	tbl = (unsigned char *)malloc(tblbytes);
	if (tbl == NULL) { fclose(f); return 0; }
	if (fread(tbl, 1, tblbytes, f) != tblbytes) { free(tbl); fclose(f); return 0; }

	s_blob = (signed char *)malloc(sbytes ? sbytes : 1);
	if (s_blob == NULL) { free(tbl); fclose(f); return 0; }
	if (fread(s_blob, 1, sbytes, f) != sbytes) {
		free(tbl); free(s_blob); s_blob = NULL; fclose(f); return 0;
	}
	fclose(f);

	s_zones = (MusZone *)malloc(sizeof(MusZone) * nz);
	s_maps  = (unsigned short *)malloc(sizeof(unsigned short) * (nm ? nm : 1) * 128);
	if (s_zones == NULL || s_maps == NULL) {
		free(tbl); AmigaMusic_FreeBank(); return 0;
	}

	p = tbl;
	for (i = 0; i < 128; i++) s_patchmap[i] = rd16(p + i * 2);
	p += 256;
	for (i = 0; i < 128; i++) s_drummap[i] = rd16(p + i * 2);
	p += 256;
	for (i = 0; i < (unsigned long)nm * 128; i++) s_maps[i] = rd16(p + i * 2);
	p += (unsigned long)nm * 256;
	for (i = 0; i < nz; i++) {
		const unsigned char *z = p + i * 24;
		unsigned long off = rd32(z + 8);
		s_zones[i].flags  = z[0];
		s_zones[i].root   = z[1];
		s_zones[i].rate   = rd32(z + 4);
		s_zones[i].length = rd32(z + 12);
		s_zones[i].ls     = rd32(z + 16);
		s_zones[i].le     = rd32(z + 20);
		if (off > sbytes || s_zones[i].length > sbytes - off) {
			free(tbl); AmigaMusic_FreeBank(); return 0;
		}
		s_zones[i].data = s_blob + off;
	}
	free(tbl);

	s_nzones = nz;
	s_nmaps  = nm;
	s_haveBank = 1;
	return 1;
}

static const MusZone *zone_for(int patch, int key, int drum)
{
	unsigned short zi;
	if (!s_haveBank || key < 0 || key > 127) return NULL;
	if (drum) {
		zi = s_drummap[key];
	} else {
		unsigned short mi = s_patchmap[patch & 127];
		if (mi == NOZONE || mi >= s_nmaps) return NULL;
		zi = s_maps[(unsigned long)mi * 128 + key];
	}
	if (zi == NOZONE || zi >= s_nzones) return NULL;
	return &s_zones[zi];
}

/* -------------------------------------------------------------- GM.CAT */

/* GMCat.cpp's per-patch velocity table, verbatim: the tunes are mixed
 * assuming it, so dropping it changes the balance between instruments. */
static const unsigned char kVolTab[128] = {
	100,100,100,100,100, 90,100,100,100,100,100, 90,100,100,100,100,
	100,100, 85,100,100,100,100,100,100,100,100,100, 90, 90,110, 80,
	100,100,100, 90, 70,100,100,100,100,100,100,100,100,100,100,100,
	100,100, 90,100,100,100,100,100,100,120,100,100,100,120,100,127,
	100,100, 90,100,100,100,100,100,100, 95,100,100,100,100,100,100,
	100,100,100,100,100,100,100,115,100,100,100,100,100,100,100,100,
	100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,
	100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100 };

enum { EV_ON = 0, EV_OFF, EV_PATCH, EV_CTRL, EV_TEMPO };

typedef struct {
	unsigned long  tick;
	unsigned short seq;
	unsigned char  kind;
	unsigned char  ch;
	unsigned char  a;
	unsigned char  b;
} MusEvent;

#define MAX_SUBS   256
#define MAX_EVENTS 20000
/* hard ceiling for one rendered tune; the longest real one is ~3 min */
#define MUSIC_MAX_RENDER (8UL * 60UL * MUSIC_RATE)

typedef struct {
	const unsigned char *data;
	unsigned long size;
} MusSeq;

static MusEvent *s_ev;
static unsigned long s_nev;
static unsigned long s_evcap;
static int s_tempo0;

static MusSeq s_subs[MAX_SUBS];
static int    s_nsubs;

static void ev_push(unsigned long tick, int kind, int ch, int a, int b)
{
	MusEvent *e;
	if (s_nev >= s_evcap) return;
	e = &s_ev[s_nev];
	e->tick = tick;
	e->seq  = (unsigned short)(s_nev & 0xFFFF);
	e->kind = (unsigned char)kind;
	e->ch   = (unsigned char)ch;
	e->a    = (unsigned char)a;
	e->b    = (unsigned char)b;
	s_nev++;
}

/* Walk one sequence, mirroring gmext_write_sequence(). Returns the end tick. */
static unsigned long walk_seq(const MusSeq *seq, int ch, unsigned long t, int depth)
{
	const unsigned char *d = seq->data;
	unsigned long n = seq->size, i = 0;
	int cmd = -1;

	if (depth > 8) return t;
	while (i < n) {
		unsigned long delta = 0;
		int cnt = 0;
		for (;;) {
			unsigned char c;
			if (i >= n) return t;
			c = d[i++];
			delta += (unsigned long)(c & 0x7F);
			if (!(c & 0x80)) break;
			if (++cnt == 4 || i >= n) return t;
			delta <<= 7;
		}
		t += delta;
		if (i >= n) return t;

		if (d[i] & 0x80) {
			cmd = d[i++];
			if (cmd == 0xFF || cmd == 0xFD) return t;     /* end track / sub */
			if (cmd == 0xFE) {                             /* insert sub */
				int sub;
				if (i >= n) return t;
				sub = d[i++];
				if (sub >= s_nsubs) return t;
				t = walk_seq(&s_subs[sub], ch, t, depth + 1);
				cmd = -1;
				continue;
			}
			cmd &= 0xF0;
		} else if (cmd < 0) {
			return t;                                      /* invalid running mode */
		}

		if (i >= n) return t;
		{
			unsigned char d1 = d[i++];
			switch (cmd) {
			case 0x80:
			case 0x90: {
				unsigned char d2;
				if (i >= n) return t;
				d2 = d[i++];
				if (cmd == 0x90 && d2) ev_push(t, EV_ON, ch, d1, d2);
				else                   ev_push(t, EV_OFF, ch, d1, 0);
				break;
			}
			case 0xC0:
				if (d1 == 0x7E) return t;                  /* restart marker */
				ev_push(t, EV_PATCH, ch, d1, 0);
				break;
			case 0xB0: {
				unsigned char d2;
				if (i >= n) return t;
				d2 = d[i++];
				if (d1 == 0x7E) break;
				if (d1 == 0) {
					if (d2) ev_push(t, EV_TEMPO, ch, (2 * d2) & 0xFF, 0);
					break;
				}
				ev_push(t, EV_CTRL, ch, d1, d2);
				break;
			}
			case 0xE0:
				if (i >= n) return t;
				i++;                                       /* pitch bend: ignored */
				break;
			default:
				return t;
			}
		}
	}
	return t;
}

static int ev_cmp(const void *pa, const void *pb)
{
	const MusEvent *a = (const MusEvent *)pa;
	const MusEvent *b = (const MusEvent *)pb;
	if (a->tick < b->tick) return -1;
	if (a->tick > b->tick) return 1;
	if (a->seq  < b->seq)  return -1;
	if (a->seq  > b->seq)  return 1;
	return 0;
}

/* Parse one GM.CAT entry payload into the event list. 1 on success. */
static int parse_tune(const unsigned char *d, unsigned long n)
{
	unsigned long pos = 0;
	int ntracks, i;

	s_nev = 0;
	s_nsubs = 0;
	if (n < 2) return 0;

	s_tempo0 = d[pos++];
	if (s_tempo0 <= 0) s_tempo0 = 120;
	s_nsubs = d[pos++];
	if (s_nsubs > MAX_SUBS) return 0;
	for (i = 0; i < s_nsubs; i++) {
		unsigned long sz;
		if (pos + 4 > n) return 0;
		sz = rd32_le(d + pos);
		if (sz < 4 || pos + sz > n) return 0;
		s_subs[i].data = d + pos + 4;
		s_subs[i].size = sz - 4;
		pos += sz;
	}
	if (pos >= n) return 0;
	ntracks = d[pos++];

	for (i = 0; i < ntracks; i++) {
		MusSeq sq;
		unsigned long sz;
		int ch;
		if (pos >= n) return 0;
		ch = d[pos++];
		if (pos + 4 > n) return 0;
		sz = rd32_le(d + pos);
		if (sz < 4 || pos + sz > n) return 0;
		sq.data = d + pos + 4;
		sq.size = sz - 4;
		pos += sz;
		walk_seq(&sq, ch & 15, 0, 0);
	}
	if (s_nev == 0) return 0;
	qsort(s_ev, s_nev, sizeof(MusEvent), ev_cmp);
	return 1;
}

/* ------------------------------------------------------------- mixer */

typedef struct {
	const MusZone *z;
	unsigned long  pos;        /* 16.16 */
	unsigned long  step;       /* 16.16 */
	int vol, targ;             /* 0..64 */
	int ch, key;
	int on, fade;
	int amp;                   /* current peak contribution, for stealing */
	int age;
} MusVoice;

static MusVoice s_vox[MUSIC_MAXVOICES];
static signed char s_voltab[65][256];
static int  s_voltabReady;

/* 2^(n/12) in 16.16, index = semitones + 96 */
static unsigned long s_ratio[193];
static int  s_ratioReady;

static int s_interp = 1;   /* linear interpolation: better sound, ~2x mix cost */
static FILE *s_file;        /* pre-rendered stream, when playing one */
static int s_fileLoop;
static int s_playing;
static int s_loop;
static int s_gain = 48;        /* per-tune, 0..64 */
static int s_master = 64;      /* user volume, 0..64 */
static unsigned long s_evAt;   /* next event index */
static unsigned long s_lastTick;
static unsigned long s_sptFx;  /* samples per MIDI tick, 16.16 */
static unsigned long s_dueFx;  /* samples until the next event, 16.16 */
static int  s_tickLeft;        /* samples until the next 50 Hz control tick */
static int  s_patch[16];
static int  s_chvol[16];

static void build_tables(void)
{
	int v, s, i;
	if (!s_voltabReady) {
		for (v = 0; v <= 64; v++)
			for (s = 0; s < 256; s++) {
				int x = (s < 128) ? s : s - 256;
				s_voltab[v][s] = (signed char)((x * v) >> 6);
			}
		s_voltabReady = 1;
	}
	if (!s_ratioReady) {
		/* 2^(n/12) built by repeated multiplication from an exact half-tone
		 * constant would drift; use a small table of the 12 ratios and
		 * octave shifts instead, so every entry is exact to 1/65536. */
		static const unsigned long semi[12] = {
			65536UL, 69433UL, 73562UL, 77936UL, 82570UL, 87480UL,
			92682UL, 98193UL, 104032UL, 110218UL, 116772UL, 123715UL };
		for (i = 0; i < 193; i++) {
			int n = i - 96;              /* -96..+96 semitones */
			int oct = n / 12, rem = n % 12;
			unsigned long r;
			if (rem < 0) { rem += 12; oct--; }
			r = semi[rem];
			if (oct >= 0) r <<= oct;
			else          r >>= (-oct);
			s_ratio[i] = r ? r : 1;
		}
		s_ratioReady = 1;
	}
}

static unsigned long step_for(const MusZone *z, int key)
{
	int semis, idx;
	unsigned long r;
	if (z->flags & 2) semis = 0;                 /* untuned percussion */
	else              semis = key - (int)z->root;
	idx = semis + 96;
	if (idx < 0) idx = 0;
	if (idx > 192) idx = 192;
	r = s_ratio[idx];
	/* step = rate * 2^(semis/12) / MUSIC_RATE, in 16.16 */
	return (unsigned long)(((unsigned long long)z->rate * r) / MUSIC_RATE);
}

static void voices_clear(void)
{
	int i;
	for (i = 0; i < MUSIC_MAXVOICES; i++) {
		s_vox[i].z = NULL;
		s_vox[i].vol = s_vox[i].targ = 0;
		s_vox[i].on = s_vox[i].fade = 0;
		s_vox[i].amp = 0;
		s_vox[i].age = 0;
	}
}

static void note_on(int ch, int key, int velo)
{
	int drum = (ch == 9);
	int patch = s_patch[ch];
	int vs, vol, i, newamp;
	const MusZone *z;
	MusVoice *cand = NULL;

	vs = (velo * (drum ? 80 : kVolTab[patch & 127])) >> 7;
	if (vs <= 0) return;
	z = zone_for(patch, key, drum);
	if (z == NULL || z->length < 2) return;

	vol = (vs * s_chvol[ch] * 64) / (127 * 127);
	if (vol <= 0) return;
	if (vol > 64) vol = 64;

	for (i = 0; i < MUSIC_MAXVOICES; i++)
		if (s_vox[i].z == NULL) { cand = &s_vox[i]; break; }

	newamp = (vol * 127) >> 6;
	if (cand == NULL) {
		int best = 0, bestamp = 0x7FFFFFFF;
		for (i = 0; i < MUSIC_MAXVOICES; i++) {
			int a = s_vox[i].on ? s_vox[i].amp : (s_vox[i].amp >> 1);
			if (a < bestamp || (a == bestamp && s_vox[i].age > s_vox[best].age)) {
				bestamp = a; best = i;
			}
		}
		cand = &s_vox[best];
		if (cand->on && cand->amp > (newamp * 3) / 2)
			return;                       /* never steal a much louder note */
	}

	cand->z    = z;
	cand->pos  = 0;
	cand->step = step_for(z, key);
	cand->vol  = (z->flags & 1) ? 0 : vol;   /* looped samples fade in */
	cand->targ = vol;
	cand->ch   = ch;
	cand->key  = key;
	cand->on   = 1;
	cand->fade = 0;
	cand->age  = 0;
	cand->amp  = newamp;
}

static void note_off(int ch, int key)
{
	int i;
	for (i = 0; i < MUSIC_MAXVOICES; i++) {
		MusVoice *v = &s_vox[i];
		if (v->z != NULL && v->on && v->ch == ch && v->key == key) {
			v->on = 0;
			if (v->z->flags & 1) v->fade = 1;
		}
	}
}

static void set_tempo(int bpm)
{
	if (bpm <= 0) bpm = 120;
	/* samples per MIDI tick (24 per beat) in 16.16:
	 *   MUSIC_RATE * 60 * 65536 / (bpm * 24) = MUSIC_RATE * 163840 / bpm */
	s_sptFx = (unsigned long)(((unsigned long long)MUSIC_RATE * 163840UL) / (unsigned long)bpm);
	if (s_sptFx == 0) s_sptFx = 1;
}

static void apply_event(const MusEvent *e)
{
	switch (e->kind) {
	case EV_ON:    note_on(e->ch, e->a, e->b); break;
	case EV_OFF:   note_off(e->ch, e->a); break;
	case EV_PATCH: s_patch[e->ch] = e->a; break;
	case EV_CTRL:  if (e->a == 7) s_chvol[e->ch] = e->b; break;
	case EV_TEMPO: set_tempo(e->a); break;
	default: break;
	}
}

static void restart_tune(void)
{
	int i;
	voices_clear();
	for (i = 0; i < 16; i++) { s_patch[i] = 0; s_chvol[i] = 127; }
	s_evAt = 0;
	s_lastTick = 0;
	set_tempo(s_tempo0);
	s_dueFx = 0;
	s_tickLeft = MUSIC_RATE / MUSIC_TICKHZ;
}

int AmigaMusic_Play(const unsigned char *data, unsigned long len, int gain, int loop)
{
	AmigaMusic_Stop();
	if (!s_haveBank || data == NULL || len < 4) return 0;
	build_tables();
	if (s_ev == NULL) {
		s_ev = (MusEvent *)malloc(sizeof(MusEvent) * MAX_EVENTS);
		if (s_ev == NULL) return 0;
		s_evcap = MAX_EVENTS;
	}
	if (!parse_tune(data, len)) return 0;
	s_gain = (gain < 0) ? 0 : (gain > 64 ? 64 : gain);
	s_loop = loop;
	restart_tune();
	s_playing = 1;
	return 1;
}

void AmigaMusic_Stop(void)
{
	s_playing = 0;
	if (s_file != NULL) { fclose(s_file); s_file = NULL; }
	voices_clear();
}

int AmigaMusic_Playing(void) { return s_playing; }

void AmigaMusic_SetInterp(int on)
{
	s_interp = on ? 1 : 0;
}

void AmigaMusic_SetVolume(int vol)
{
	s_master = (vol < 0) ? 0 : (vol > 64 ? 64 : vol);
}

/* Advance envelopes; called once per 50 Hz control tick. */
static void control_tick(void)
{
	int i;
	for (i = 0; i < MUSIC_MAXVOICES; i++) {
		MusVoice *v = &s_vox[i];
		int dv;
		if (v->z == NULL) continue;
		if (v->fade) v->targ = 0;
		dv = v->targ - v->vol;
		if (dv > 16) dv = 16;
		if (dv < -16) dv = -16;
		v->vol += dv;
		if (v->fade && v->vol <= 0) { v->z = NULL; continue; }
		v->age++;
	}
}

/* Mix exactly n samples (n <= tick remainder, no events inside). */
static void mix_run(signed char *dst, int n)
{
	static short acc[1024];
	int i, vi;
	int g = (s_gain * s_master) >> 6;

	if (n <= 0) return;
	if (n > (int)(sizeof(acc) / sizeof(acc[0]))) n = (int)(sizeof(acc) / sizeof(acc[0]));
	memset(acc, 0, (size_t)n * sizeof(short));

	for (vi = 0; vi < MUSIC_MAXVOICES; vi++) {
		MusVoice *v = &s_vox[vi];
		const MusZone *z = v->z;
		const signed char *sd;
		const signed char *vt;
		unsigned long pos, step, lend, llen;
		int done = 0;

		if (z == NULL || v->vol <= 0) continue;
		sd   = z->data;
		vt   = s_voltab[v->vol];
		pos  = v->pos;
		step = v->step ? v->step : 1;
		if (z->flags & 1) {
			lend = z->le << 16;
			llen = (z->le - z->ls) << 16;
		} else {
			lend = (z->length - 1) << 16;
			llen = 0;
		}

		/* The wrap test is hoisted out of the sample loop: work out how many
		 * samples fit before the loop point and run that many unchecked. The
		 * bank stores one guard sample past every zone, so the interpolation
		 * partner sd[ip+1] never needs a bounds test either. Together those
		 * were two compares and a branch on every sample of every voice. */
		while (done < n) {
			unsigned long avail;
			int run, k;

			if (pos >= lend) {
				if (llen) { do { pos -= llen; } while (pos >= lend); }
				else { v->z = NULL; break; }
			}
			avail = ((lend - pos) + step - 1) / step;
			run = (avail > (unsigned long)(n - done)) ? (n - done) : (int)avail;
			if (run <= 0) run = 1;

			if (s_interp) {
				short *ap = acc + done;
				for (k = 0; k < run; k++) {
					const signed char *sp = sd + (pos >> 16);
					int s0 = sp[0];
					ap[k] += (short)vt[(unsigned char)
						(s0 + (((sp[1] - s0) * (int)(pos & 0xFFFF)) >> 16))];
					pos += step;
				}
			} else {
				short *ap = acc + done;
				for (k = 0; k < run; k++) {
					ap[k] += (short)vt[(unsigned char)sd[pos >> 16]];
					pos += step;
				}
			}
			done += run;
		}

		v->pos = pos;
		/* Amplitude for the voice-stealing heuristic: the instantaneous level
		 * is enough, so it is sampled once per run instead of per sample. */
		if (v->z != NULL) {
			int cur = vt[(unsigned char)sd[pos >> 16]];
			v->amp = (cur < 0) ? -cur : cur;
		} else {
			v->amp = 0;
		}
	}

	for (i = 0; i < n; i++) {
		int x = ((int)acc[i] * g) >> 6;
		if (x > 127) x = 127;
		else if (x < -128) x = -128;
		dst[i] = (signed char)x;
	}
}

int AmigaMusic_Refill(void *ud, signed char *dst, int max)
{
	int done = 0;

	/* Pre-rendered stream: the mixing was done once, at install time. */
	if (s_file != NULL) {
		size_t got;
		if (!s_playing || dst == NULL || max <= 0) return 0;
		got = fread(dst, 1, (size_t)max, s_file);
		if (got < (size_t)max) {
			if (s_fileLoop && fseek(s_file, 0, SEEK_SET) == 0) {
				size_t more = fread(dst + got, 1, (size_t)max - got, s_file);
				got += more;
			}
			if (got < (size_t)max) {
				memset(dst + got, 0, (size_t)max - got);
				if (!s_fileLoop) { s_playing = 0; }
			}
		}
		return got ? max : 0;
	}
	(void)ud;

	if (!s_playing || dst == NULL || max <= 0) return 0;

	while (done < max) {
		int chunk = max - done;

		/* events that are due now */
		while (s_evAt < s_nev && s_dueFx < 65536UL) {
			const MusEvent *e = &s_ev[s_evAt];
			apply_event(e);
			s_evAt++;
			if (s_evAt < s_nev) {
				unsigned long dt = s_ev[s_evAt].tick - e->tick;
				s_dueFx += (unsigned long)(((unsigned long long)dt * s_sptFx) >> 0);
			} else {
				s_dueFx = 0xFFFFFFFFUL;      /* nothing left to schedule */
			}
		}
		if (s_evAt >= s_nev && s_dueFx == 0xFFFFFFFFUL) {
			/* Let the tails ring out, then loop or stop. Notes still held when
			 * the events run out have to be released here: a looped sample with
			 * no note-off would sound for ever. */
			int silent = 1, i;
			for (i = 0; i < MUSIC_MAXVOICES; i++) {
				if (s_vox[i].z == NULL) continue;
				silent = 0;
				s_vox[i].on = 0;
				s_vox[i].fade = 1;
			}
			if (silent) {
				if (s_loop) { restart_tune(); continue; }
				s_playing = 0;
				break;
			}
		}

		if (s_dueFx != 0xFFFFFFFFUL) {
			int untilEvent = (int)(s_dueFx >> 16);
			if (untilEvent < chunk) chunk = untilEvent;
		}
		if (s_tickLeft < chunk) chunk = s_tickLeft;

		if (chunk <= 0) {
			if (s_tickLeft <= 0) {
				control_tick();
				s_tickLeft = MUSIC_RATE / MUSIC_TICKHZ;
				continue;
			}
			/* an event is due within this sample: consume the fraction */
			if (s_dueFx != 0xFFFFFFFFUL) { s_dueFx = 0; continue; }
			break;
		}

		mix_run(dst + done, chunk);
		done += chunk;
		s_tickLeft -= chunk;
		if (s_dueFx != 0xFFFFFFFFUL) {
			unsigned long adv = (unsigned long)chunk << 16;
			s_dueFx = (s_dueFx > adv) ? s_dueFx - adv : 0;
		}
		if (s_tickLeft <= 0) {
			control_tick();
			s_tickLeft = MUSIC_RATE / MUSIC_TICKHZ;
		}
	}

	if (done < max) memset(dst + done, 0, (size_t)(max - done));

	return done ? max : 0;
}

/* Per-tune replay gain (0..64), calibrated offline so nothing clips.
 * Index is the GM.CAT position, i.e. music.rul's catPos. */
int AmigaMusic_TuneGain(int catpos)
{
	static const unsigned char kGain[22] = {
		46, 46, 50, 49, 44, 46, 44, 46, 46, 54, 46, 46,
		33, 46, 46, 44, 44, 44, 38, 64, 40, 44 };
	if (catpos < 0 || catpos >= 22) return 46;
	return kGain[catpos];
}

/* --------------------------------------------------- pre-rendered music */

/* A rendered stream is just headerless 8-bit signed mono at MUSIC_RATE; a
 * file that exists and holds at least a second of audio counts as done. */
int AmigaMusic_HaveRendered(const char *path)
{
	FILE *f;
	long n;
	if (path == NULL) return 0;
	f = fopen(path, "rb");
	if (f == NULL) return 0;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fclose(f);
	return (n >= MUSIC_RATE) ? 1 : 0;
}

/* Mix one tune to disk. Interpolation is forced on: this runs once, so the
 * cheap loop would be a false economy. Writes to a .tmp and renames at the
 * end, so an interrupted render never leaves a half file that looks done.
 * prog(done, total) is called every few percent; it may be NULL. */
int AmigaMusic_RenderToFile(const unsigned char *data, unsigned long len,
                            int gain, const char *path,
                            void (*prog)(unsigned long done, unsigned long total))
{
	char tmp[512];
	FILE *f;
	signed char buf[2048];
	int wasInterp = s_interp;
	unsigned long written = 0;
	unsigned long lastReport = 0;
	int ok = 1;

	if (path == NULL || data == NULL) return 0;
	snprintf(tmp, sizeof tmp, "%s.tmp", path);

	s_interp = 1;
	if (!AmigaMusic_Play(data, len, gain, 0)) { s_interp = wasInterp; return 0; }

	f = fopen(tmp, "wb");
	if (f == NULL) { AmigaMusic_Stop(); s_interp = wasInterp; return 0; }

	while (s_playing && written < MUSIC_MAX_RENDER) {
		int n = AmigaMusic_Refill(NULL, buf, (int)sizeof buf);
		if (n <= 0) break;
		if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) { ok = 0; break; }
		written += (unsigned long)n;
		if (prog != NULL && s_nev > 0 && s_evAt - lastReport >= s_nev / 32 + 1) {
			lastReport = s_evAt;
			prog(s_evAt, s_nev);
		}
	}
	fclose(f);
	AmigaMusic_Stop();
	s_interp = wasInterp;

	if (!ok) { remove(tmp); return 0; }
	remove(path);                 /* rename() will not overwrite on AmigaDOS */
	if (rename(tmp, path) != 0) { remove(tmp); return 0; }
	if (prog != NULL) prog(1, 1);
	return 1;
}

/* Play a stream produced by AmigaMusic_RenderToFile. */
int AmigaMusic_PlayFile(const char *path, int loop)
{
	AmigaMusic_Stop();
	if (path == NULL) return 0;
	s_file = fopen(path, "rb");
	if (s_file == NULL) return 0;
	s_fileLoop = loop ? 1 : 0;
	s_playing = 1;
	return 1;
}
/* ------------------------------------------------------------- host test */

#ifdef MUSIC_HOST_TEST
static void put32(FILE *o, unsigned long v)
{
	fputc((int)(v & 255), o); fputc((int)((v >> 8) & 255), o);
	fputc((int)((v >> 16) & 255), o); fputc((int)((v >> 24) & 255), o);
}
static void put16(FILE *o, unsigned int v)
{
	fputc((int)(v & 255), o); fputc((int)((v >> 8) & 255), o);
}

int main(int argc, char **argv)
{
	FILE *cat;
	unsigned char *catbuf;
	long catlen;
	unsigned long off, size, first;
	int tune, namesize;
	const unsigned char *payload;
	FILE *wav;
	signed char buf[4410];
	unsigned long total = 0;
	int seconds;

	if (argc < 5) {
		fprintf(stderr, "usage: %s music.bnk GM.CAT tune out.wav [seconds]\n", argv[0]);
		return 2;
	}
	tune = atoi(argv[3]);
	seconds = (argc > 5) ? atoi(argv[5]) : 180;

	if (!AmigaMusic_LoadBank(argv[1])) { fprintf(stderr, "bank load failed\n"); return 1; }
	fprintf(stderr, "bank ok: %u zones, %u maps\n", (unsigned)s_nzones, (unsigned)s_nmaps);

	cat = fopen(argv[2], "rb");
	if (!cat) { fprintf(stderr, "no GM.CAT\n"); return 1; }
	fseek(cat, 0, SEEK_END); catlen = ftell(cat); fseek(cat, 0, SEEK_SET);
	catbuf = (unsigned char *)malloc((size_t)catlen);
	if (fread(catbuf, 1, (size_t)catlen, cat) != (size_t)catlen) { return 1; }
	fclose(cat);

	first = rd32_le(catbuf);
	if ((unsigned long)tune >= first / 8) { fprintf(stderr, "tune out of range\n"); return 1; }
	off  = rd32_le(catbuf + tune * 8);
	size = rd32_le(catbuf + tune * 8 + 4);
	payload = catbuf + off;
	namesize = payload[0];
	if (namesize <= 56) {
		fprintf(stderr, "tune %d: \"%s\"\n", tune, (const char *)payload + 1);
		payload += 1 + namesize;
	}

	if (!AmigaMusic_Play(payload, size, AmigaMusic_TuneGain(tune), 0)) {
		fprintf(stderr, "tune parse failed\n"); return 1;
	}
	fprintf(stderr, "events: %lu, tempo %d\n", s_nev, s_tempo0);

	wav = fopen(argv[4], "wb");
	fseek(wav, 44, SEEK_SET);
	while (AmigaMusic_Playing() && total < (unsigned long)seconds * MUSIC_RATE) {
		int n = AmigaMusic_Refill(NULL, buf, (int)sizeof(buf));
		int i;
		if (n <= 0) break;
		for (i = 0; i < n; i++) put16(wav, (unsigned int)((buf[i] << 8) & 0xFFFF));
		total += (unsigned long)n;
	}
	fseek(wav, 0, SEEK_SET);
	fwrite("RIFF", 1, 4, wav); put32(wav, 36 + total * 2);
	fwrite("WAVE", 1, 4, wav); fwrite("fmt ", 1, 4, wav); put32(wav, 16);
	put16(wav, 1); put16(wav, 1); put32(wav, MUSIC_RATE);
	put32(wav, MUSIC_RATE * 2); put16(wav, 2); put16(wav, 16);
	fwrite("data", 1, 4, wav); put32(wav, total * 2);
	fclose(wav);
	fprintf(stderr, "wrote %s: %.1f s\n", argv[4], (double)total / MUSIC_RATE);
	AmigaMusic_FreeBank();
	free(catbuf);
	return 0;
}
#endif
