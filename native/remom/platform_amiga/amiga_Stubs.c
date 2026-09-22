/*
 * amiga_Stubs.c - czesci interfejsu, ktore na Amidze sa na razie puste.
 *
 *  - EMS_Startup(): bufor "EMS" z malloc, jak w headless_EMM.c.
 *  - dzwiek: zaslepka jak headless_Audio.c (NO_SOUND_LIBRARY; Paula - pozniej).
 *  - przechwytywanie A/V (Platform_Capture.h): ReMoM.c wola Start/Stop przy
 *    --capture. Upstreamowy capture/PFL_Capture.c liczy na double (zakaz FPU
 *    w tym porcie) i pisze setki plikow klatek - na Amidze bez sensu, wiec
 *    zaslepki: Start zglasza porazke, reszta nic nie robi.
 *  - Pump_Events / Pump_Paints: wejscia Win32 wolane spod #ifdef _STU_WIN;
 *    headless definiuje je bezwarunkowo - robimy tak samo.
 */

#include "Platform.h"
#include "Platform_Capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* WZD s13p01 */
void EMS_Startup(void)
{
    _VGAFILEH_seg = (uint8_t *)malloc((16384 * 5));

    memset(_VGAFILEH_seg, 0, (16384 * 5));
}

/* Platform_Audio_Play_Sound - amiga_Audio.c (Paula) */

int Platform_Capture_Start(const char * out_dir, int fps)
{
    (void)fps;
    fprintf(stdout, "[amiga] --capture \"%s\": capture is not available on AmigaOS\n",
            (out_dir != NULL) ? out_dir : "(null)");
    fflush(stdout);
    return 1;
}

int Platform_Capture_Active(void)
{
    return 0;
}

void Platform_Capture_Video_Frame(const uint8_t * indexed, int w, int h, const PFL_Color * palette)
{
    (void)indexed;
    (void)w;
    (void)h;
    (void)palette;
}

void Platform_Capture_Audio(const uint8_t * stream, int len)
{
    (void)stream;
    (void)len;
}

void Platform_Capture_Set_Audio_Format(int frequency, int channels, int bits_per_sample, int is_float)
{
    (void)frequency;
    (void)channels;
    (void)bits_per_sample;
    (void)is_float;
}

void Platform_Capture_Stop(void)
{
}

void Pump_Events(void) { /* no-op */ }
void Pump_Paints(void) { /* no-op */ }
