/* AmiXcom music: software wavetable replayer for X-COM's GM.CAT tunes.
 *
 * Why a software mixer and not Paula's four hardware voices: the tunes are
 * 16-channel orchestral MIDI and need 10-16 simultaneous notes; with four
 * hardware voices two thirds of the music disappears (measured: 0.5 s of
 * geo1 needs 16 voices, 908 notes over the track). The mixer folds all
 * voices into one 8-bit stream and Paula plays that stream by DMA, so the
 * hardware channel count stops mattering.
 *
 * The instrument bank (data/common/music.bnk) ships with the port; the tunes
 * themselves are read from the user's own GM.CAT and never redistributed.
 *
 * All Amiga OS calls stay out of this file - it is plain C, buildable on the
 * host for verification (see MUSIC_HOST_TEST in amiga_music.c).
 */
#ifndef AMIGA_MUSIC_H
#define AMIGA_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

#define MUSIC_RATE      22050   /* mixing rate, mono */
#define MUSIC_TICKHZ    50      /* replayer control rate */
#define MUSIC_MAXVOICES 16

/* Load the instrument bank. Returns 1 on success, 0 on failure (no bank ->
 * the caller stays silent). Safe to call twice; the second call is a no-op. */
int AmigaMusic_LoadBank(const char *path);

/* Free the bank and stop any tune. */
void AmigaMusic_FreeBank(void);

/* 1 once a bank is loaded. */
int AmigaMusic_HaveBank(void);

/* Start a tune from one GM.CAT entry: 'data' points at the entry payload
 * (after the embedded name, if any), 'len' is its length in bytes.
 * 'gain' is the per-tune master volume, 0..64 (see AmigaMusic_TuneGain).
 * 'loop' repeats the tune when it ends. Returns 1 if the tune parsed. */
int AmigaMusic_Play(const unsigned char *data, unsigned long len,
                    int gain, int loop);

/* Stop playing; the bank stays loaded. */
void AmigaMusic_Stop(void);

/* 1 while a tune is playing. */
int AmigaMusic_Playing(void);

/* Linear interpolation between sample points: clearly cleaner in the treble,
 * but roughly doubles the cost of the mixing loop. On by default. */
void AmigaMusic_SetInterp(int on);

/* Master volume applied on top of the per-tune gain, 0..64. */
void AmigaMusic_SetVolume(int vol);

/* Render the next 'max' 8-bit signed mono samples into dst; returns the
 * count produced (0 only when nothing is playing). This matches the
 * refill callback signature of AmigaAudio_MusicStart(). */
int AmigaMusic_Refill(void *ud, signed char *dst, int max);


/* ---- pre-rendered music -------------------------------------------------
 * Mixing every tune once to disk and streaming the result afterwards: the
 * mixer keeps up on average even on a slow machine, but loses whenever the
 * game stops drawing for a while, and a file stream cannot lose. */

/* 1 if a usable rendered stream is already at 'path'. */
int AmigaMusic_HaveRendered(const char *path);

/* Mix one tune to 'path' (8-bit signed mono at MUSIC_RATE), interpolated.
 * prog(done, total) reports progress and may be NULL. 1 on success. */
int AmigaMusic_RenderToFile(const unsigned char *data, unsigned long len,
                            int gain, const char *path,
                            void (*prog)(unsigned long done, unsigned long total));

/* Stream a file produced by AmigaMusic_RenderToFile. */
int AmigaMusic_PlayFile(const char *path, int loop);

/* Per-tune replay gain, 0..64, indexed by GM.CAT position (0..21).
 * Calibrated offline so no tune clips and quiet tunes are not buried. */
int AmigaMusic_TuneGain(int catpos);

#ifdef __cplusplus
}
#endif
#endif
