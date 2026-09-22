/*
 * amiga_profil.c - profiler probkujacy NA AMIDZE (bez JIT, bez hosta).
 *
 * Serwer przerwania VBlank (50 Hz PAL) szuka na stosie nadzorcy ramki
 * wyjatku przerwanego kodu: 68020, format 0, poziom 3 -> slowo formatu
 * $006C, przed nim PC (4 B) i SR (2 B). Przerwany PC trafia do bufora
 * (liczony tylko, gdy przerwany byl tryb uzytkownika - bit S w SR zgaszony).
 * Przy wyjsciu programu probki ida do Work:profil.txt razem z adresem
 * Amiga_Profil_Start, zeby host (winuae/harness/profil.py) przeliczyl je na
 * symbole z niestripowanej binarki: przesuniecie = adres w pamieci - adres
 * z nm.
 *
 * Wlaczanie: Set REMOM_PROFIL 1 (zmienna LOKALNA - libnix nie widzi SetEnv).
 * Koszt wylaczonego: jedno getenv przy starcie.
 */
#include <exec/types.h>
#include <exec/interrupts.h>
#include <hardware/intbits.h>
#include <proto/exec.h>

#include <stdio.h>
#include <stdlib.h>

#define PROFIL_MAX 200000UL

static ULONG * profil_pc = NULL;
static ULONG profil_n = 0;
static ULONG profil_pominiete = 0;
static struct Interrupt profil_int;
static int profil_aktywny = 0;
/* surowy zrzut stosu nadzorcy z pierwszych wywolan - do recznego odczytu ramki */
#define PROFIL_ZRZUT 512
static UBYTE profil_zrzut[PROFIL_ZRZUT];
static ULONG profil_zrzut_sp = 0;
static int profil_zrzut_jest = 0;

/* Wolane z przerwania: d0/d1/a0/a1 wolno niszczyc; zwraca 0 (flaga Z). */
static ULONG Amiga_Profil_Serwer(void)
{
    UBYTE lokalna;
    /* zmienna bajtowa lezy pod NIEPARZYSTYM adresem - ramka jest na
       granicy slowa, wiec bez wyrownania skan co 2 nigdy w nia nie trafial
       (zrzut stosu 2026-09-17: sp=0x10002239, ramka na +0x2D) */
    UBYTE * sp = (UBYTE *)((ULONG)&lokalna & ~1UL);
    int i;

    if(profil_zrzut_jest == 0)
    {
        for(i = 0; i < PROFIL_ZRZUT; i++)
        {
            profil_zrzut[i] = sp[i];
        }
        profil_zrzut_sp = (ULONG)sp;
        profil_zrzut_jest = 1;
    }
    for(i = 0; i < 400; i += 2)
    {
        UBYTE * f = sp + i;
        /* SR(2) PC(4) format/wektor(2) */
        if(f[6] == 0x00 && f[7] == 0x6C)
        {
            UWORD sr = (UWORD)((f[0] << 8) | f[1]);
            /* tryb nadzorcy tez liczymy (czas w ROM/exec); poziom IPL >= 3
               to nie jest ramka przerwanego kodu */
            if((sr & 0x0700) < 0x0300)
            {
                ULONG pc = ((ULONG)f[2] << 24) | ((ULONG)f[3] << 16) | ((ULONG)f[4] << 8) | f[5];
                if(profil_n < PROFIL_MAX)
                {
                    profil_pc[profil_n++] = pc;
                }
                return 0;
            }
        }
    }
    profil_pominiete++;
    return 0;
}

void Amiga_Profil_Start(void);

static void Amiga_Profil_Stop(void)
{
    FILE * f;
    ULONG i;

    if(!profil_aktywny)
    {
        return;
    }
    RemIntServer(INTB_VERTB, &profil_int);
    profil_aktywny = 0;
    f = fopen("profil.txt", "w");
    if(f != NULL)
    {
        fprintf(f, "baza Amiga_Profil_Start 0x%08lx\n", (unsigned long)(ULONG)Amiga_Profil_Start);
        fprintf(f, "probek %lu pominietych %lu\n", (unsigned long)profil_n, (unsigned long)profil_pominiete);
        for(i = 0; i < profil_n; i++)
        {
            fprintf(f, "%08lx\n", (unsigned long)profil_pc[i]);
        }
        fclose(f);
    }
    f = fopen("profil-stos.txt", "w");
    if(f != NULL && profil_zrzut_jest)
    {
        fprintf(f, "sp 0x%08lx\n", (unsigned long)profil_zrzut_sp);
        for(i = 0; i < PROFIL_ZRZUT; i++)
        {
            fprintf(f, "%02x%s", (unsigned int)profil_zrzut[i], ((i & 15) == 15) ? "\n" : " ");
        }
    }
    if(f != NULL)
    {
        fclose(f);
    }
    printf("[amiga] profil: %lu probek, %lu bez ramki -> Work:profil.txt\n",
           (unsigned long)profil_n, (unsigned long)profil_pominiete);
    fflush(stdout);
}

void Amiga_Profil_Start(void)
{
    const char * e = getenv("REMOM_PROFIL");
    if(e == NULL || e[0] != '1' || profil_aktywny)
    {
        return;
    }
    profil_pc = (ULONG *)malloc(PROFIL_MAX * sizeof(ULONG));
    if(profil_pc == NULL)
    {
        return;
    }
    profil_int.is_Node.ln_Type = NT_INTERRUPT;
    profil_int.is_Node.ln_Pri = -60;
    profil_int.is_Node.ln_Name = (char *)"remom-profil";
    profil_int.is_Data = NULL;
    profil_int.is_Code = (VOID (*)())Amiga_Profil_Serwer;
    AddIntServer(INTB_VERTB, &profil_int);
    profil_aktywny = 1;
    atexit(Amiga_Profil_Stop);
    printf("[amiga] profil: probkowanie VBlank wlaczone\n");
    fflush(stdout);
}
