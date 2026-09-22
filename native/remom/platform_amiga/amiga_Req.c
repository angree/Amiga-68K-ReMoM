/*
 * amiga_Req.c - jedyny plik backendu, ktory dotyka naglowkow systemu Amigi.
 *
 * Systemowy requester (EasyRequest) dla Platform_Show_Error(): brak danych
 * gry wykrywany jest PRZED otwarciem ekranu, a gracz uruchamiajacy z ikony
 * nie widzi zadnego logu - bez requestera program po prostu by zniknal.
 *
 * KOLEJNOSC BAJTOW: plik nie potrzebuje niczego z silnika i najlepiej
 * kompilowac go flagami warstwy natywnej (bez -include amiga_le.h). Gdyby
 * jednak trafil pod flagi silnika, amiga_le.h zdazyl juz wlaczyc
 * "#pragma scalar_storage_order little-endian" - a struktury Intuition
 * (EasyStruct) MUSZA byc w kolejnosci natywnej, bo czyta je ROM. Stad
 * przelaczenie na default PRZED naglowkami systemu. Dziala w obu wariantach.
 */

#ifdef AMIGA_LE_H
#pragma scalar_storage_order default
#endif

#include <exec/types.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>

#include <stdio.h>

void Amiga_Req_Error(const char * title, const char * message);

extern struct IntuitionBase * IntuitionBase;

void Amiga_Req_Error(const char * title, const char * message)
{
    struct EasyStruct es;
    ULONG args[1];

    if(IntuitionBase == NULL || message == NULL)
    {
        return;
    }

    es.es_StructSize   = sizeof(es);
    es.es_Flags        = 0;
    es.es_Title        = (UBYTE *)((title != NULL) ? title : "ReMoM");
    es.es_TextFormat   = (UBYTE *)"%s";      /* tekst moze zawierac '%' */
    es.es_GadgetFormat = (UBYTE *)"OK";

    args[0] = (ULONG)message;
    EasyRequestArgs(NULL, &es, NULL, (APTR)args);
}
