#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
remom-patch.py - mechaniczne latki na swiezo rozpakowane zrodla ReMoM.

Model ten sam co w porcie OpenXcom: repozytorium NIE przechowuje zmienionej
kopii upstreamu. Build rozpakowuje upstream/ReMoM-<rewizja>.tar.gz od zera
i przepuszcza go przez ten skrypt. Kazda latka to dokladna zamiana tekstu;
jesli upstream sie zmieni i tekstu nie ma, skrypt PRZERYWA build zamiast
cicho pominac latke - pominieta latka wyglada potem dokladnie jak blad gry.

UZYCIE:  python3 remom-patch.py <katalog z rozpakowanym ReMoM>

Glowny temat latek: kolejnosc bajtow. native/remom/amiga_le.h trzyma
wszystkie struktury silnika w little-endian (#pragma scalar_storage_order),
co ma dwa ograniczenia opisane w tamtym naglowku. Latki ponizej je obsluguja:

  A. struktury, ktore NIE sa danymi z plikow (tablice statyczne ze
     wskaznikami, stan walki, sciezki, szablony nowej gry) wracaja do
     kolejnosci natywnej - sa wtedy szybsze i nie blokuja kompilacji;
  B. miejsca, ktore biora adres pola struktury z danych gry, dostaja adres
     liczony przez offsetof (LE_FIELD_PTR), a tam, gdzie przez taki wskaznik
     czyta sie int16, odczyt idzie przez LE16_GET / LE16_SET.
"""

import io
import os
import sys

ROOT = None
PRAGMA_NATIVE = "#pragma scalar_storage_order default  /* AMIGA: nie sa to dane z plikow */"
PRAGMA_LE = "#pragma scalar_storage_order little-endian  /* AMIGA: powrot do kolejnosci danych gry */"


def sciezka(rel):
    return os.path.join(ROOT, rel)


def czytaj(rel):
    with io.open(sciezka(rel), "r", encoding="latin-1", newline="") as f:
        return f.read()


def zapisz(rel, tekst):
    with io.open(sciezka(rel), "w", encoding="latin-1", newline="") as f:
        f.write(tekst)


def zamien(rel, stare, nowe, ile=None):
    """Dokladna zamiana. ile=None: wszystkie wystapienia (co najmniej jedno)."""
    t = czytaj(rel)
    n = t.count(stare)
    if n == 0 and "\n" in stare:
        # upstream ma czesc plikow z koncami CRLF
        stare = stare.replace("\n", "\r\n")
        nowe = nowe.replace("\n", "\r\n")
        n = t.count(stare)
    if n == 0:
        sys.exit("LATKA NIE PASUJE: %s\n  nie znaleziono: %r" % (rel, stare))
    if ile is not None and n != ile:
        sys.exit("LATKA NIE PASUJE: %s\n  oczekiwano %d wystapien, jest %d: %r" % (rel, ile, n, stare))
    zapisz(rel, t.replace(stare, nowe))


def natywna_struktura(rel, naglowek):
    """Otacza definicje struktury (od linii zaczynajacej sie od `naglowek`
    do jej konczacego sie srednikiem zamkniecia) pragmami kolejnosci
    natywnej."""
    t = czytaj(rel)
    linie = t.split("\n")
    start = None
    for i, l in enumerate(linie):
        if l.rstrip("\r").rstrip() == naglowek:
            start = i
            break
    if start is None:
        sys.exit("LATKA NIE PASUJE: %s\n  brak definicji: %r" % (rel, naglowek))
    glebokosc = 0
    otwarte = False
    koniec = None
    for j in range(start, len(linie)):
        kod = linie[j].split("//")[0]
        for c in kod:
            if c == "{":
                glebokosc += 1
                otwarte = True
            elif c == "}":
                glebokosc -= 1
        if otwarte and glebokosc == 0:
            # zamkniecie struktury; srednik moze byc w tej lub nastepnej linii
            k = j
            while ";" not in linie[k].split("//")[0]:
                k += 1
            koniec = k
            break
    if koniec is None:
        sys.exit("LATKA NIE PASUJE: %s\n  nie domknieto: %r" % (rel, naglowek))
    eol = "\r\n" if linie[start].endswith("\r") else ""
    linie.insert(koniec + 1, PRAGMA_LE + eol)
    linie.insert(start, PRAGMA_NATIVE + eol)
    zapisz(rel, "\n".join(linie))


def natywny_typedef(rel, nazwa):
    """Jak natywna_struktura, ale dla anonimowego `typedef struct { ... } nazwa;`
    - szuka zamkniecia po nazwie i cofa sie do jego `typedef struct`."""
    t = czytaj(rel)
    linie = t.split("\n")
    koniec = None
    for i, l in enumerate(linie):
        if l.strip().rstrip("\r").strip() == "} %s;" % nazwa:
            koniec = i
            break
    if koniec is None:
        sys.exit("LATKA NIE PASUJE: %s\n  brak zamkniecia typedef: %r" % (rel, nazwa))
    start = None
    for j in range(koniec, -1, -1):
        if linie[j].strip().startswith("typedef struct"):
            start = j
            break
    if start is None:
        sys.exit("LATKA NIE PASUJE: %s\n  brak poczatku typedef: %r" % (rel, nazwa))
    eol = "\r" if linie[start].endswith("\r") else ""
    linie.insert(koniec + 1, PRAGMA_LE + eol)
    linie.insert(start, PRAGMA_NATIVE + eol)
    zapisz(rel, "\n".join(linie))


def natywne_od(rel, kotwica):
    """Wszystko od linii zaczynajacej sie od `kotwica` do konca pliku .c
    deklaruje struktury natywnie (lokalne tablice programu)."""
    t = czytaj(rel)
    linie = t.split("\n")
    for i, l in enumerate(linie):
        if l.lstrip().startswith(kotwica):
            eol = "\r" if l.endswith("\r") else ""
            linie.insert(i, PRAGMA_NATIVE + eol)
            zapisz(rel, "\n".join(linie))
            return
    sys.exit("LATKA NIE PASUJE: %s\n  brak kotwicy: %r" % (rel, kotwica))


# --------------------------------------------------------------------------
#  A. Struktury w kolejnosci natywnej
# --------------------------------------------------------------------------

def latki_natywne():
    # Tablice statyczne ze wskaznikami (GCC nie wyemituje ich w odwroconej
    # kolejnosci) - naglowki.
    natywna_struktura("MoM/src/RACETYPE.h", "struct s_RACE_TYPE")
    # s_UNIT_TYPE: tablica statyczna w UNITTYPE.c. Kopiowana memcpy do
    # s_BATTLE_UNIT (COMBINIT.c, UnitView.c), wiec OBIE musza miec te sama
    # kolejnosc bajtow.
    natywna_struktura("MoM/src/UNITTYPE.h", "struct s_UNIT_TYPE")
    natywna_struktura("MoX/src/MOM_DAT.h", "struct s_BATTLE_UNIT")
    # Ten memcpy zaklada ten sam uklad od pola Melee: w s_BATTLE_UNIT w miejscu
    # pict_seg stoi unia z int64_t (8 B), w s_UNIT_TYPE goly wskaznik - 8 B na
    # PC, 4 B na 68k. Na Amidze od hits wszystko bylo przesuniete o 4 B:
    # zle figure_max, Abilities..., wiec walka strategiczna na wodzie uznawala
    # wszystkich za "niezaangazowanych" (2026-09-17: 4 wywolania RNG zamiast 27).
    zamien("MoM/src/UNITTYPE.h",
           "    /* 10 */  SAMB_ptr pict_seg;",
           "    /* 10 */  union { int64_t amiga_pict_seg_8b; SAMB_ptr pict_seg; };  /* AMIGA: 8 B jak na PC */", ile=1)
    natywna_struktura("MoM/src/UnitView.h", "struct USW_HeroAbl")
    natywna_struktura("STU/src/STU_INIT.h", "typedef struct s_LBX_SUBFILE_INFO")
    natywna_struktura("STU/src/STU_INIT.h", "typedef struct s_LBX_ARCHIVE_INFO")
    natywny_typedef("STU/src/STU_GRAF.h", "STU_LBX_Manifest_Entry")

    # Stan wylacznie w pamieci - nie trafia do SAVE.GAM, a kod bierze adresy
    # jego pol 16-bitowych i czyta je natywnie.
    natywna_struktura("MoM/src/Combat.h", "struct s_BATTLEFIELD")
    natywna_struktura("MoM/src/MovePath.h", "struct s_MOVE_PATH")
    natywna_struktura("MoM/src/NewGame.h", "struct s_WIZARD_PRESET")
    natywna_struktura("MoM/src/NewGame.h", "struct s_Init_Base_Spells")
    natywna_struktura("MoM/src/NewGame.h", "struct s_Init_Base_Realms")
    natywna_struktura("MoM/src/NewGame.h", "struct s_DEFAULT_SPELLS")

    # Pliki .c z lokalnymi tablicami: od pierwszej lokalnej definicji w dol.
    natywne_od("MoM/src/INITGAME.c", "typedef struct { uint16_t off; uint8_t kind;")
    natywne_od("MoM/src/UnitView.c", "struct USW_Ability")
    natywne_od("STU/src/STU_WRLD.c", "static const struct { int type; const char * name; } terrain_type_names[]")
    natywne_od("src/Artificial_Human_Player.c", "struct s_HeMoM_Action")


# --------------------------------------------------------------------------
#  B. Adresy pol struktur z danymi gry
# --------------------------------------------------------------------------

def latki_adresy():
    # --- pola jednobajtowe i bitmapy: adres przez offsetof wystarcza ---
    for rel in ("MoM/src/INITGAME.c", "MoM/src/NewGame.c", "MoM/src/Lair.c",
                "src/HeMoM.c", "MoM/src/WIZVIEW.c"):
        t = czytaj(rel)
        for wzor, nowe in (
                ("&_players[itr2].alchemy", "LE_FIELD_PTR(&_players[itr2], alchemy)"),
                ("&_players[0].alchemy", "LE_FIELD_PTR(&_players[0], alchemy)"),
                ("&_players[player_idx].alchemy", "LE_FIELD_PTR(&_players[player_idx], alchemy)"),
                ("&_players[mirror_screen_player_idx].alchemy",
                 "LE_FIELD_PTR(&_players[mirror_screen_player_idx], alchemy)")):
            t = t.replace(wzor, nowe)
        zapisz(rel, t)

    for rel in ("MoM/src/AISPELL.c", "MoM/src/Outpost.c"):
        t = czytaj(rel)
        for idx in ("itr_cities", "city_idx", "itr"):
            t = t.replace("&_CITIES[%s].contacts" % idx, "LE_FIELD_PTR(&_CITIES[%s], contacts)" % idx)
        zapisz(rel, t)

    for rel in ("MoM/src/Lair.c", "MoM/src/Surveyor.c"):
        t = czytaj(rel)
        for idx in ("lair_idx", "itr_lairs"):
            t = t.replace("&_LAIRS[%s].Misc_Flags" % idx, "LE_FIELD_PTR(&_LAIRS[%s], Misc_Flags)" % idx)
        zapisz(rel, t)

    # Defeated_Wizards to uint16 uzywany jako bitmapa BAJT PO BAJCIE - na
    # pamieci little-endian daje dokladnie to, co na PC.
    for rel in ("MoM/src/CONQUEST.c", "MoM/src/SCORE.c"):
        zamien(rel, "(char *)&_players[_current_player_idx].Defeated_Wizards",
               "(char *)LE_FIELD_PTR(&_players[_current_player_idx], Defeated_Wizards)")

    # research_spells przesuwane przez Clear_Structure bajt po bajcie.
    zamien("MoM/src/NEXTTURN.c", "(uint8_t *)&_players[player_idx].research_spells[0]",
           "(uint8_t *)LE_FIELD_PTR(&_players[player_idx], research_spells[0])", ile=2)

    # Aura_Xs / Aura_Ys / type to int8 - Set_Node_Type pisze bajty.
    zamien("MoM/src/MAPGEN.c",
           "Set_Node_Type(_NODES[itr].power, &_NODES[itr].Aura_Xs[0], &_NODES[itr].Aura_Ys[0], _NODES[itr].wp, &_NODES[itr].type);",
           "Set_Node_Type(_NODES[itr].power, LE_FIELD_PTR(&_NODES[itr], Aura_Xs[0]), LE_FIELD_PTR(&_NODES[itr], Aura_Ys[0]), _NODES[itr].wp, LE_FIELD_PTR(&_NODES[itr], type));",
           ile=2)

    # enchantments sprawdzane na zero jako dwa int - porownanie z zerem nie
    # zalezy od kolejnosci bajtow.
    zamien("MoM/src/AISPELL.c", "((unsigned int *)&_UNITS[itr].enchantments)",
           "((unsigned int *)LE_FIELD_PTR(&_UNITS[itr], enchantments))", ile=2)

    # spellranks w AISPELL jest przypisywany i dalej nieuzywany.
    zamien("MoM/src/AISPELL.c", "spellranks = &_players[player_idx].spellranks[0];",
           "spellranks = LE_FIELD_PTR(&_players[player_idx], spellranks[0]);", ile=1)

    # --- int16 czytane przez wskaznik: potrzebna natywna kopia albo LE16 ---

    # Make_Item tylko CZYTA spellranks - dostaje natywna kopie.
    for rel, n in (("MoM/src/Lair.c", 2), ("MoM/src/SBookScr.c", 4)):
        zamien(rel, "&_players[player_idx].spellranks[0]", "Amiga_Spellranks(player_idx)", ile=n)
    # Ta sama tablica przekazana przez ROZKLAD do wskaznika - GCC tego nie
    # zglasza (znalezione skanerem build/skanuj-tablice-le.py, 2026-09-17).
    for rel in ("MoM/src/Lair.c", "MoM/src/EVENTS.c"):
        zamien(rel, "_players[player_idx].spellranks, 0)", "Amiga_Spellranks(player_idx), 0)", ile=1)
    for rel in ("MoM/src/Lair.c", "MoM/src/SBookScr.c", "MoM/src/EVENTS.c"):
        t = czytaj(rel)
        # funkcja pomocnicza wstawiona tuz przed pierwsza definicja uzywajaca
        # _players - czyli po ostatnim #include
        linie = t.split("\n")
        ostatni = max(i for i, l in enumerate(linie) if l.startswith("#include"))
        eol = "\r" if linie[ostatni].endswith("\r") else ""
        linie.insert(ostatni + 1, (
            "/* AMIGA: s_WIZARD.spellranks lezy w pamieci little-endian; Make_Item\n"
            "   czyta ja natywnie, wiec dostaje kopie. */%s\n"
            "static int16_t * Amiga_Spellranks(int16_t p) { static int16_t kopia[5]; int k; "
            "for(k = 0; k < 5; k++) { kopia[k] = _players[p].spellranks[k]; } return kopia; }%s") % (eol, eol))
        zapisz(rel, "\n".join(linie))

    # --- rozklad tablic int16 do wskaznika (skaner skanuj-tablice-le.py) ---

    # AI badan czarow: research_spells to uint16_t[] z rekordu gracza. Natywny
    # odczyt dawal odwrocone numery czarow -> indeks spoza spell_data_table ->
    # dzielenie przez zero (CPU TRAP 5 w AI_Spell_Research_Select, 2026-09-17,
    # po 2228 wywolaniach RNG zgodnych z PC).
    import re
    rel = "MoM/src/AISPELL.c"
    t = czytaj(rel)
    t, ile = re.subn(r"\bresearch_spells\[(\w+)\]", r"LE16_GET(research_spells, \1)", t)
    if ile < 5:
        sys.exit("LATKA NIE PASUJE: %s - research_spells[...] tylko %d razy" % (rel, ile))
    zapisz(rel, t)

    # Dyplomacja: int zapisywany rzutowaniem w tablicy int16 rekordu gracza.
    # Na PC to 4 bajty little-endian pod field_A8 + 4*offset - odtwarzamy to
    # bajt po bajcie.
    zamien("MoM/src/DIPLOMAC.c",
           "((int *)_players[0].Dipl.field_A8)[offset_A8] = m_exchange_spell_list[itr_players];",
           "{ unsigned char *amiga_p = (unsigned char *)LE_FIELD_PTR(&_players[0].Dipl, field_A8[0]) + (offset_A8) * 4;"
           " uint32_t amiga_v = (uint32_t)(int)(m_exchange_spell_list[itr_players]);"
           " amiga_p[0] = (unsigned char)amiga_v; amiga_p[1] = (unsigned char)(amiga_v >> 8);"
           " amiga_p[2] = (unsigned char)(amiga_v >> 16); amiga_p[3] = (unsigned char)(amiga_v >> 24); }", ile=1)

    # Sloty przedmiotow bohatera: Hero_Slot_Types tylko ZAPISUJE tablice.
    zamien("MoM/src/Spells132.c",
           "    Hero_Slot_Types(unit_type_idx, _players[player_idx].Heroes[hero_slot_idx].Item_Slots);",
           "    { int16_t amiga_sloty[3]; int amiga_k; Hero_Slot_Types(unit_type_idx, amiga_sloty);"
           " for(amiga_k = 0; amiga_k < 3; amiga_k++) { _players[player_idx].Heroes[hero_slot_idx].Item_Slots[amiga_k] = amiga_sloty[amiga_k]; } }",
           ile=1)

    # Przedmioty bohatera czytane przez int16_t * (tylko odczyt).
    zamien("MoM/src/Combat.c",
           "hero_items = &(_players[battle_units[battle_unit_idx].controller_idx].Heroes[_UNITS[battle_units[battle_unit_idx].unit_idx].Hero_Slot].Items[0]);",
           "hero_items = LE_FIELD_PTR(&_players[battle_units[battle_unit_idx].controller_idx].Heroes[_UNITS[battle_units[battle_unit_idx].unit_idx].Hero_Slot], Items[0]);",
           ile=1)
    zamien("MoM/src/NEXTTURN.c",
           "hero_items = &(_players[_UNITS[unit_idx].owner_idx].Heroes[_UNITS[unit_idx].Hero_Slot].Items[0]);",
           "hero_items = LE_FIELD_PTR(&_players[_UNITS[unit_idx].owner_idx].Heroes[_UNITS[unit_idx].Hero_Slot], Items[0]);",
           ile=1)
    for rel, idx in (("MoM/src/Combat.c", "itr"), ("MoM/src/NEXTTURN.c", "itr_hero_items")):
        zamien(rel, "hero_items[%s]" % idx, "LE16_GET(hero_items, %s)" % idx)

    # Param0 w s_SPELL_DATA: kod czyta 16 bitow spod adresu jednobajtowego
    # pola (to jest unit_type / ce_idx z tej samej unii).
    zamien("MoM/src/Combat.c",
           "*(int16_t /* */ *)&spell_data_table[spell_idx].Param0",
           "LE16_GET(LE_FIELD_PTR(&spell_data_table[spell_idx], Param0), 0)", ile=1)

    # Ekran przedmiotow: sloty skarbca i bohatera, odczyt i zapis int16.
    rel = "MoM/src/ItemScrn.c"
    zamien(rel, "item_slots_ptr = &_players[_current_player_idx].Vault_Items[0];",
           "item_slots_ptr = LE_FIELD_PTR(&_players[_current_player_idx], Vault_Items[0]);", ile=1)
    zamien(rel, "item_slots_ptr = &_players[_current_player_idx].Heroes[hero_slot_idx].Items[0];",
           "item_slots_ptr = LE_FIELD_PTR(&_players[_current_player_idx].Heroes[hero_slot_idx], Items[0]);", ile=1)
    zamien(rel, "item_slots_ptr[item_slot_idx] = m_cursor_item_idx;",
           "LE16_SET(item_slots_ptr, item_slot_idx, m_cursor_item_idx);", ile=1)
    zamien(rel, "item_slots_ptr[item_slot_idx] = ST_UNDEFINED;",
           "LE16_SET(item_slots_ptr, item_slot_idx, ST_UNDEFINED);", ile=1)
    zamien(rel, "Swap_Short(&item_slots_ptr[item_slot_idx], &m_cursor_item_idx);",
           "{ int16_t amiga_tmp = LE16_GET(item_slots_ptr, item_slot_idx); "
           "LE16_SET(item_slots_ptr, item_slot_idx, m_cursor_item_idx); m_cursor_item_idx = amiga_tmp; }", ile=1)
    zamien(rel, "item_slots_ptr[item_slot_idx]", "LE16_GET(item_slots_ptr, item_slot_idx)")


def latki_pamiec():
    # Dwa statyczne bufory po 16 MB daly .bss 34 MB - LoadSeg na maszynie
    # z 32 MB nie mogl tego zaalokowac i powloka zwracala rc=10, zanim
    # program wypisal cokolwiek (2026-09-17).
    #
    # Pula alokatora: upstream sam zmierzyl szczyt 4,58 MiB i ustawil
    # POOL_MIN_ARENA_BYTES na 5 MiB (build pilnuje, zeby pojemnosc nie spadla
    # ponizej). 6 MiB zostawia 1 MiB zapasu.
    zamien("MoX/src/Allocate_Pool.h",
           "#define POOL_ARENA_CAPACITY  (16 * 1024 * 1024)",
           "#define POOL_ARENA_CAPACITY  (6 * 1024 * 1024)   /* AMIGA: bylo 16 MiB */", ile=1)
    # Pierscien logu: przy przepelnieniu gubi komunikaty ze znacznikiem, nie
    # wywraca programu.
    zamien("STU/src/STU_LOG.c",
           "#define LOG_RING_SIZE        ((size_t)(16 * 1024 * 1024))",
           "#define LOG_RING_SIZE        ((size_t)(512 * 1024))  /* AMIGA: bylo 16 MB */", ile=1)


def latki_log():
    # Log na Amidze pisany NATYCHMIAST. Upstream trzyma komunikaty w pierscieniu
    # i oproznia go "pompa" w petli gry - program, ktory utknie przed pierwsza
    # pompa, zostawia pusty plik i nie wiadomo, gdzie stanal (tak bylo przy
    # pierwszym uruchomieniu HeMoM, 2026-09-17: CPU 100%, log 0 bajtow).
    # Jedyne wywolanie Ring_Write_Bytes to STU_Log_Write_At - po nim oprozniamy.
    zamien("STU/src/STU_LOG.c",
           "    STU_Log_Ring_Write_Bytes(stack_buf, total_len);",
           "    STU_Log_Ring_Write_Bytes(stack_buf, total_len);\n"
           "#ifdef AMIGA_LOG_SYNC\n"
           "    STU_Log_Flush_All();  /* AMIGA: zapis natychmiastowy */\n"
           "#endif", ile=1)
    # Pierscien jest tu 512 KB (latki_pamiec), a w turze AI nikt go nie
    # oproznia - upstream wtedy GUBI linie ("359 messages dropped", slad RNG
    # z dziura 1810..2168, pomiar 2026-09-17). Pelny pierscien -> zapis do
    # pliku i dopiero potem nowa linia.
    zamien("STU/src/STU_LOG.c",
           "    if(total_len > STU_Log_Ring_Free())\n    {\n        ++log_dropped_since_last_pump;",
           "    if(total_len > STU_Log_Ring_Free() && log_file != NULL)\n    {\n"
           "        STU_Log_Flush_All();  /* AMIGA: zamiast gubic linie */\n    }\n"
           "    if(total_len > STU_Log_Ring_Free())\n    {\n        ++log_dropped_since_last_pump;", ile=1)
    # Slad RNG (przy --seed1) szedl TAKZE na stderr - na Amidze to okno
    # konsoli, nieprzekierowane: ~0,25 s na linie, 521 s z 562 s tury 1 na
    # maszynie pomiarowej (2026-09-17). Kopia w logu wystarcza
    # (porownaj-rng.py czyta log).
    zamien("MoX/src/random.c",
           "        fprintf(stderr,\n            \"[RNG-CALL] seg=",
           "        if(0) fprintf(stderr,  /* AMIGA: stderr = okno konsoli, bardzo wolne */\n            \"[RNG-CALL] seg=", ile=1)
    # (2026-09-17) Po doprowadzeniu scenariusza do konca zapis natychmiastowy
    # jest wylaczony (kosztowal wiekszosc czasu przebiegu); przy diagnozie
    # zawieszenia wlaczyc -DAMIGA_LOG_SYNC w REMOM_DEFS.


def latki_surowe_dane():
    import re
    # --- liczniki i mapa swiata z SAVE.GAM: natywnie w pamieci, zamiana na
    #     granicy pliku (native/remom/amiga_endian.c) ---
    rel = "MoX/src/LOADSAVE.c"
    t = czytaj(rel)
    wzor_r = re.compile(r"stu_fread\(&(\w+)\s*,\s*1\s*,\s*2\s*,\s*file_pointer\);")
    wzor_w = re.compile(r"stu_fwrite\(&(\w+)\s*,\s*1\s*,\s*2\s*,\s*file_pointer\);")
    n_r = len(wzor_r.findall(t))
    n_w = len(wzor_w.findall(t))
    if n_r < 9 or n_w < 9:
        sys.exit("LATKA NIE PASUJE: %s - licznikow int16: odczyt %d, zapis %d (oczekiwano >= 9)" % (rel, n_r, n_w))
    t = wzor_r.sub(lambda m: "%s Amiga_Swap16_Array(&%s, 1);" % (m.group(0), m.group(1)), t)
    t = wzor_w.sub(lambda m: "Amiga_Swap16_Array(&%s, 1); %s Amiga_Swap16_Array(&%s, 1);"
                   % (m.group(1), m.group(0), m.group(1)), t)
    stare_r = "stu_fread(_world_maps, NUM_PLANES, 4800, file_pointer);"
    stare_w = "stu_fwrite(_world_maps, NUM_PLANES, 4800, file_pointer);"
    if t.count(stare_r) != 1 or t.count(stare_w) != 1:
        sys.exit("LATKA NIE PASUJE: %s - wczytanie/zapis _world_maps" % rel)
    t = t.replace(stare_r, stare_r + " Amiga_Swap16_Array(_world_maps, NUM_PLANES * 2400);")
    t = t.replace(stare_w, "Amiga_Swap16_Array(_world_maps, NUM_PLANES * 2400); " + stare_w +
                  " Amiga_Swap16_Array(_world_maps, NUM_PLANES * 2400);")
    # Tablice int16 AI podpiete pod _players[5].spells_list (ALLOC.c):
    # gra czyta je natywnie przez int16_t*, wiec w pamieci sa natywne, a do
    # pliku ida jako czesc rekordu gracza. Objaw (2026-09-17): SAVE9.GAM po
    # 5 turach rozni sie od PC w 32 bajtach _players[5]+0x266..0x34D, mimo
    # zgodnego sladu RNG (3085/3085).
    ai = ("Amiga_Swap16_Array(_ai_landmass_war_targets[0], 6); "
          "Amiga_Swap16_Array(_ai_landmass_war_targets[1], 6); "
          "Amiga_Swap16_Array(_ai_reevaluate_continents_countdown, 8); "
          "Amiga_Swap16_Array(_ai_reevaluate_summoning_circle_countdown, 8);")
    stare_r = "stu_fread(_players, NUM_PLAYERS, 1224, file_pointer);"
    stare_w = "stu_fwrite(_players, NUM_PLAYERS, 1224, file_pointer);"
    if t.count(stare_r) != 1 or t.count(stare_w) != 1:
        sys.exit("LATKA NIE PASUJE: %s - wczytanie/zapis _players" % rel)
    t = t.replace(stare_r, stare_r + " /* AMIGA */ " + ai)
    t = t.replace(stare_w, "/* AMIGA */ " + ai + " " + stare_w + " " + ai)
    zapisz(rel, t)

    # Makra little-endian na mapie swiata -> dostep natywny.
    rel = "MoX/src/MOX_DEF.h"
    t = czytaj(rel)
    k = t.rfind("#endif")
    if k < 0:
        sys.exit("LATKA NIE PASUJE: %s - brak koncowego #endif" % rel)
    blok = (
        "/* AMIGA: mapa swiata lezy w pamieci natywnie (amiga_endian.c), a te makra\n"
        "   czytaly ja jako little-endian. Ten sam adres, dostep natywny. */\n"
        "#undef GET_TERRAIN_TYPE\n#undef TERRAIN_TYPE_INDEX\n#undef TERRAIN_TYPE\n#undef SET_TERRAIN_TYPE\n"
        "#define AMIGA_WM_IDX(_wx_, _wy_, _wp_) ((((_wp_) * WORLD_SIZE) + ((_wy_) * WORLD_WIDTH) + (_wx_)))\n"
        "#define GET_TERRAIN_TYPE(_wx_, _wy_, _wp_)   ( (int16_t)((int16_t *)_world_maps)[AMIGA_WM_IDX(_wx_, _wy_, _wp_)] )\n"
        "#define TERRAIN_TYPE_INDEX(_wx_, _wy_, _wp_) ( (int16_t)((int16_t *)_world_maps)[AMIGA_WM_IDX(_wx_, _wy_, _wp_)] )\n"
        "#define TERRAIN_TYPE(_wx_, _wy_, _wp_)       ( (int16_t)(uint16_t)((int16_t *)_world_maps)[AMIGA_WM_IDX(_wx_, _wy_, _wp_)] % NUM_TERRAIN_TYPES )\n"
        "#define SET_TERRAIN_TYPE(_wx_, _wy_, _wp_, _terrain_type_) ( ((int16_t *)_world_maps)[AMIGA_WM_IDX(_wx_, _wy_, _wp_)] = (int16_t)(_terrain_type_) )\n\n")
    zapisz(rel, t[:k] + blok + t[k:])

    # Naglowek tablicy rekordow LBX ([liczba u16][rozmiar u16]) czytany
    # fread-em do zmiennych int16. Objaw (2026-09-17): "HLPENTRY.LBX
    # [entry 1] has an incorrect record size" zaraz po wejsciu do
    # Screen_Control().
    rel = "MoX/src/LBX_Load.c"
    t = czytaj(rel)
    wzor = re.compile(r"fread\(&(max_records|rec_size), 2, 1, lbxload_fptr\);")
    if len(wzor.findall(t)) != 4:
        sys.exit("LATKA NIE PASUJE: %s - oczekiwano 4 odczytow naglowka rekordow" % rel)
    t = wzor.sub(lambda m: "%s Amiga_Swap16_Array(&%s, 1);" % (m.group(0), m.group(1)), t)
    zapisz(rel, t)

    # TERRTYPE.LBX rekord 0: tablica int16 (5 x 512 B) czytana natywnie przez
    # int16_t* w autokafelkowaniu. Objaw (2026-09-17): nowa gra staje zaraz
    # po Shuffle_Terrains, na otwarciu TERRTYPE.LBX, bez wyjatku CPU.
    zamien("MoM/src/MAPGEN.c",
           "LBX_Load_Data_Static(terrtype_lbx_file__MGC_ovr051, 0, (SAMB_ptr)terrtype, 0, 5, 512);",
           "LBX_Load_Data_Static(terrtype_lbx_file__MGC_ovr051, 0, (SAMB_ptr)terrtype, 0, 5, 512);"
           " Amiga_Swap16_Array(terrtype, (5 * 512) / 2);  /* AMIGA */", ile=3)

    zamien("MoM/src/MainScr_Maps.c", "GET_2B_OFS(_world_maps, ", "AMIGA_WM_GET(", ile=2)
    zamien("MoM/src/MainScr_Maps.c", "GET_2B_OFS(_world_maps,world_maps_offset)",
           "AMIGA_WM_GET(world_maps_offset)", ile=1)


def znaczniki_w_funkcji(rel, sygnatura, prefiks, minimum=3, pamiec=None):
    """Diagnostyka: znacznik AMIGA_KROK po kazdym wywolaniu-instrukcji
    (linia postaci `nazwa(...);`) w ciele funkcji zaczynajacej sie linia
    `sygnatura`. Konczy na pierwszej linii '}' w kolumnie 0."""
    import re
    t = czytaj(rel)
    a = t.find(sygnatura)
    if a < 0:
        sys.exit("LATKA NIE PASUJE: %s - brak funkcji %r" % (rel, sygnatura))
    m_koniec = re.compile(r"^\}", re.M).search(t, a)
    if not m_koniec:
        sys.exit("LATKA NIE PASUJE: %s - brak konca funkcji %r" % (rel, sygnatura))
    b = m_koniec.start()
    wzor = re.compile(r"^([ \t]+)([A-Za-z_][A-Za-z_0-9]*)(\(.*\);)[ \t]*(\r?)$", re.M)
    pomin = ("if", "while", "for", "switch", "return", "sizeof", "assert")
    def wstaw(m):
        if m.group(2) in pomin:
            return m.group(0)
        if pamiec:
            return '%s%s%s AMIGA_KROK_B("%s: %s", %s);%s' % (m.group(1), m.group(2), m.group(3), prefiks, m.group(2), pamiec, m.group(4))
        return '%s%s%s AMIGA_KROK("%s: %s");%s' % (m.group(1), m.group(2), m.group(3), prefiks, m.group(2), m.group(4))
    srodek, ile = wzor.subn(wstaw, t[a:b])
    if ile < minimum:
        sys.exit("LATKA NIE PASUJE: %s - w %r tylko %d znacznikow" % (rel, sygnatura, ile))
    zapisz(rel, t[:a] + srodek + t[b:])


def latki_diagnoza():
    # Znaczniki na drodze do Main_Screen() (2026-09-17: HeMoM staje po
    # "SCR ENTER screen=Main", a przed pierwszym znacznikiem Main_Screen).
    rel = "MoM/src/MOM_SCR.c"
    for stare, nazwa in (
            ("Load_Palette(0, -1, 0);", "Main: Load_Palette"),
            ("Set_Button_Down_Offsets(1, 1);", "Main: Set_Button_Down_Offsets"),
            ("Fill(SCREEN_XMIN, SCREEN_YMIN, SCREEN_XMAX, SCREEN_YMAX, 7);", "Main: Cycle+Apply+Fill7"),
            ("Fill(SCREEN_XMIN, SCREEN_YMIN, SCREEN_XMAX, SCREEN_YMAX, 5);", "Main: Fill5")):
        zamien(rel, stare, '%s AMIGA_KROK("%s");' % (stare, nazwa), ile=1)
    zamien(rel, "                Main_Screen();",
           '                AMIGA_KROK("Main: -> Main_Screen");\n'
           '                { extern int Remom_Walka_Z_Env(void); Remom_Walka_Z_Env(); }  /* AMIGA: test walki (REMOM_WALKA), build/walka-gen.py */\n'
           '                Main_Screen();', ile=1)
    # (2026-09-17) Instrumentacja funkcji rysujacych (znaczniki_w_funkcji w
    # Main_Screen, Main_Screen_Draw_Do_Draw, oknach, Draw_Unit_StatFig,
    # Draw_Picture_To_Bitmap) zdjeta po znalezieniu defektu nr 7 - w petli
    # ekranu dawala ~60 tys. linii i dlawila gre. Funkcja zostaje w skrypcie.

    # (2026-09-17) Znaczniki w Combat() / Strategic_Combat() zdjete po
    # znalezieniu przyczyny (uklad s_UNIT_TYPE, latki_natywne).

    # MOUSE_LOG pisze przez Mouse_Dbg_Log(), funkcje STATIC w naglowku - kazdy
    # plik .c otwiera wiec mouse_debug.log od nowa, a na AmigaOS drugie
    # otwarcie pliku do zapisu sie nie udaje i znaczniki ida na stderr
    # (konsola, niewidoczna przy przekierowaniu). Kierujemy je na stdout,
    # razem ze znacznikami AMIGA_KROK, w jednej kolejnosci.
    zamien("platform/include/Platform.h",
           'mouse_dbg_log_file = fopen("mouse_debug.log", "w");',
           'mouse_dbg_log_file = stdout;  /* AMIGA: jeden strumien dla wszystkich plikow */', ile=1)


def latki_czas():
    # get_datetime() przy KAZDEJ linii logu robilo putenv("TZ=EST5EDT"),
    # tzset() i localtime(). Na AmigaOS kosztowalo to ~11 s na linie -
    # zmierzone 2026-09-17 ze znacznikow czasu w logu (co 11 s rowno). HeMoM
    # wygladal na zawieszony: CPU 100%, 15 minut, nic po banerze. Wynik
    # funkcji i tak uzywa tylko czasu UTC (gmtime), wiec strefa jest zbedna.
    zamien("STU/src/STU_UTIL.c",
           "    stu_putenv(tzstr);\n    stu_tzset();\n    edtt = time(NULL);\n    edt = localtime(&edtt);",
           "    /* AMIGA: bez putenv/tzset/localtime - ~11 s na wywolanie */\n"
           "    (void)tzstr;\n    edtt = time(NULL);\n    edt = NULL;\n    (void)edt;", ile=1)


def latki_start():
    # Znacznik buildu jako pierwsza linia wyjscia (native/remom/amiga_banner.c).
    zamien("src/HeMoM.c", '    int argi;\n\n    STU_Log_Startup("ReMoM.ini");',
           '    int argi;\n\n    Amiga_Banner("HeMoM");\n    AMIGA_TRAP_ARM();\n'
           '    { extern void Amiga_Profil_Start(void); Amiga_Profil_Start(); }  /* Set REMOM_PROFIL 1 */\n'
           '    STU_Log_Startup("ReMoM.ini");', ile=1)
    # Znaczniki postepu startu na stdout (Work:mom.log): gdy program stanie,
    # ostatni znacznik mowi, w ktorym etapie.
    for stare, nowe in (
            ("    STU_BRAK_Install();",
             '    AMIGA_KROK("STU_Log_Startup");\n    STU_BRAK_Install();\n    AMIGA_KROK("STU_BRAK_Install");'),
            ("    STU_GRAF_Init(STU_GRAF_HEADLESS);",
             '    AMIGA_KROK("Perf_Init");\n    STU_GRAF_Init(STU_GRAF_HEADLESS);\n    AMIGA_KROK("STU_GRAF_Init");'),
            ("    Startup_Platform();",
             '    AMIGA_KROK("argumenty");\n    Startup_Platform();\n    AMIGA_KROK("Startup_Platform");'),
            ("    ReMoM_Init_Engine();",
             '    ReMoM_Init_Engine();\n    AMIGA_KROK("ReMoM_Init_Engine");'),
            ("        Load_SAVE_GAM(save_slot);\n        Loaded_Game_Update();",
             '        AMIGA_KROK("Load_WZD_Resources");\n        Load_SAVE_GAM(save_slot);\n'
             '        AMIGA_KROK("Load_SAVE_GAM");\n        Loaded_Game_Update();\n        AMIGA_KROK("Loaded_Game_Update");')):
        zamien("src/HeMoM.c", stare, nowe, ile=1)


def latki_dzwiek():
    # Play_Sound() wola platforme tylko bez NO_SOUND_LIBRARY, a ta definicja
    # zostaje (wylacza SDL_mixer w upstreamie). Na Amidze platforma gra sama
    # (native/remom/platform_amiga/amiga_Audio.c); HeMoM ma zaslepke headless.
    zamien("MoX/src/SOUND.c",
           "int16_t Play_Sound(void* sound_buffer, uint32_t sound_buffer_size)\n{\n#ifndef NO_SOUND_LIBRARY",
           "int16_t Play_Sound(void* sound_buffer, uint32_t sound_buffer_size)\n{\n#if 1  /* AMIGA: dzwiek z platformy */", ile=1)


def latki_pompa_muzyki():
    # W dlugiej turze AI gra nie wraca do petli zdarzen, wiec nikt nie dolewa
    # buforow muzyki (amiga_Audio.c) i muzyka milknie na kilka sekund. AI
    # przez caly czas losuje - co 64. wywolanie Random_at() wola pompe, jesli
    # platforma ja ustawila (HeMoM nie ustawia; generator losowy bez zmian).
    rel = "MoX/src/random.c"
    zamien(rel, "int16_t Random_at(int16_t n, const char *file, int line, const char *func)\n{",
           "void (*amiga_pompa_muzyki)(void) = 0;  /* AMIGA: amiga_Audio.c */\n"
           "int16_t Random_at(int16_t n, const char *file, int line, const char *func)\n{", ile=1)
    zamien(rel, "    seed_before = random_seed;\n\n    ret = Random(n);",
           "    seed_before = random_seed;\n"
           "    if(amiga_pompa_muzyki != 0 && (g_random_call_count & 63) == 0) { amiga_pompa_muzyki(); }  /* AMIGA */\n\n"
           "    ret = Random(n);", ile=1)


def latki_remom():
    # ReMoM (z ekranem): znacznik buildu i przechwytywanie wyjatkow jak w HeMoM.
    zamien("src/ReMoM.c", "int main(int argc, char * argv[])\n{",
           "int main(int argc, char * argv[])\n{\n"
           "    { extern void Amiga_WB_Start(int); Amiga_WB_Start(argc); }  /* start z ikony: katalog gry, stdout do pliku */\n"
           "    Amiga_Banner(\"ReMoM\");\n    AMIGA_TRAP_ARM();\n"
           "    { extern void Amiga_Profil_Start(void); Amiga_Profil_Start(); }  /* Set REMOM_PROFIL 1 */", ile=1)
    # Profil PLAYER szuka danych i pisze zapisy w katalogach XDG pod $HOME;
    # na Amidze wszystko ma byc w Work: (katalog biezacy) - jak w HeMoM.
    zamien("src/ReMoM.c", "    STU_GRAF_Init(STU_GRAF_PLAYER);",
           "    STU_GRAF_Init(STU_GRAF_HEADLESS);  /* AMIGA: dane i zapisy w Work: */", ile=1)
    zamien("src/ReMoM.c", "    Startup_Platform();",
           '    AMIGA_KROK("ReMoM: -> Startup_Platform");\n    Startup_Platform();\n    AMIGA_KROK("ReMoM: Startup_Platform");', ile=1)


def latki_wydajnosc():
    # Wyniki profilera na Amidze (native/remom/amiga_profil.c, 2026-09-17,
    # mom-graj.uae bez JIT -80%) - kazda latka z pomiarem w PROGRESS.md.
    #
    # 1. ReMoM_Check_Data_Compat liczy SHA-256 KAZDEGO pliku LBX przy starcie:
    #    63% probek przebiegu 5 tur (~90 s). Tylko ostrzezenie o wersji
    #    danych - gra i generator losowy bez zmian.
    zamien("src/ReMoM.c", "    ReMoM_Check_Data_Compat();\n",
           "    /* AMIGA: bez SHA-256 wszystkich LBX (~90 s na 040) */\n", ile=1)
    # 2. Release_Time czeka do mark + 55 ms, spiac po 1 ms. Na Amidze sen
    #    trwal caly VBlank (20 ms) i klatka wychodzila 60 ms; z precyzyjnym
    #    snem (native/remom/amiga_czas.c) 1 ms zrobilby z tego aktywne
    #    czekanie z pompa zdarzen co 1 ms. Spimy reszte ticka, najwyzej
    #    10 ms naraz (mysz dalej pompowana co <= 10 ms). Warunek wyjscia
    #    i liczenie klatek bez zmian.
    zamien("MoX/src/Timer.c",
           "        /* CLAUDE */  Platform_Pump_Events();  /* pumps events AND refreshes cursor (polls OS position, redraws only if moved) */\n"
           "        Platform_Sleep_Millies(1);\n",
           "        /* CLAUDE */  Platform_Pump_Events();  /* pumps events AND refreshes cursor (polls OS position, redraws only if moved) */\n"
           "        { uint64_t teraz = Platform_Get_Millies();  /* AMIGA: sen do konca ticka, max 10 ms */\n"
           "          if(teraz < tick_end) { uint64_t zostalo = tick_end - teraz; Platform_Sleep_Millies(zostalo > 10 ? 10 : zostalo); } }\n", ile=1)
    # 3. Kopie calych stron 320x200 (Copy_Back_To_Off i siostry) ida slowo po
    #    slowie - 32 000 iteracji; Copy_Back_To_Off to 10% probek mapy.
    #    memcpy z libnix kopiuje dlugimi slowami. Bufory rozlaczne - wynik
    #    identyczny bajt w bajt.
    rel = "MoX/src/Video.c"
    zamien(rel,
           "    itr = 0;\n    // while(itr++ < 16000)\n    while(itr++ < (screen_pixel_size / 2))\n"
           "    {\n        *dst++ = *src++;\n    }\n",
           "    itr = 0;  /* AMIGA: memcpy zamiast 32 000 kopii slow */\n"
           "    memcpy(dst, src, (size_t)screen_pixel_size);\n    (void)itr;\n", ile=4)
    zamien(rel,
           "    itr = 0;\n    // while(itr++ < 16000)\n    while(itr++ < screen_pixel_size)\n"
           "    {\n        *dst++ = *src++;\n    }\n",
           "    itr = 0;  /* AMIGA: memcpy zamiast 64 000 kopii bajtow */\n"
           "    memcpy(dst, src, (size_t)screen_pixel_size);\n    (void)itr;\n", ile=1)
    zamien(rel,
           "        itr_rep_movsb = 0;\n        while(itr_rep_movsb++ < (screen_pixel_size / 4))\n"
           "        {\n            *dst++ = *src++;\n        }\n",
           "        itr_rep_movsb = 0;  /* AMIGA: memcpy cwiartki strony */\n"
           "        memcpy(dst, src, (size_t)(screen_pixel_size / 4));\n"
           "        dst += screen_pixel_size / 4;\n        src += screen_pixel_size / 4;\n"
           "        (void)itr_rep_movsb;\n", ile=1)
    # 4. Get_Bitmap_Actual_Size (walka: 13% probek - panel aktywnej jednostki
    #    w kazdej klatce) pisze min/max przez wskazniki przy KAZDYM pikselu.
    #    Nowa wersja: zmienne w rejestrach, w kolumnie szuka pierwszego
    #    i ostatniego niezerowego piksela. Wynik identyczny; oryginal zostaje
    #    jako _Oryginal i obsluguje bitmapy, przy ktorych jego int16 offset
    #    by sie przepelnil (tam zachowanie zostaje takie jak bylo).
    rel_fd = "MoX/src/FLIC_Draw.c"
    zamien(rel_fd,
           "void Get_Bitmap_Actual_Size(SAMB_ptr bitmap_addr, int16_t * x1, int16_t * y1, int16_t * width, int16_t * height)\n{\n",
           "void Get_Bitmap_Actual_Size_Oryginal(SAMB_ptr bitmap_addr, int16_t * x1, int16_t * y1, int16_t * width, int16_t * height);\n"
           "void Get_Bitmap_Actual_Size(SAMB_ptr bitmap_addr, int16_t * x1, int16_t * y1, int16_t * width, int16_t * height)\n{\n"
           "    /* AMIGA: szybka wersja, wynik jak Get_Bitmap_Actual_Size_Oryginal */\n"
           "    const uint8_t * kol;\n    int fw;\n    int fh;\n    int x;\n    int y;\n    int gora;\n"
           "    int lx1 = 1000;\n    int ly1 = 1000;\n    int lx2 = 0;\n    int ly2 = 0;\n"
           "    fw = FLIC_GET_WIDTH(bitmap_addr);\n    fh = FLIC_GET_HEIGHT(bitmap_addr);\n"
           "    if(fw <= 0 || fh <= 0 || ((long)fw * (long)fh + SZ_FLIC_HDR) > 32767L)\n"
           "    {\n        Get_Bitmap_Actual_Size_Oryginal(bitmap_addr, x1, y1, width, height);\n        return;\n    }\n"
           "    kol = (const uint8_t *)bitmap_addr + SZ_FLIC_HDR;\n"
           "    for(x = 0; x < fw; x++, kol += fh)\n    {\n"
           "        gora = -1;\n"
           "        for(y = 0; y < fh; y++) { if(kol[y] != ST_TRANSPARENT) { gora = y; break; } }\n"
           "        if(gora < 0) { continue; }\n"
           "        y = fh - 1;\n        while(kol[y] == ST_TRANSPARENT) { y--; }\n"
           "        if(lx1 > x) { lx1 = x; }\n        if(ly1 > gora) { ly1 = gora; }\n"
           "        lx2 = x;\n        if(ly2 < y) { ly2 = y; }\n    }\n"
           "    *x1 = (int16_t)lx1;\n    *y1 = (int16_t)ly1;\n"
           "    *height = (int16_t)(ly2 - ly1 + 1);\n    *width  = (int16_t)(lx2 - lx1 + 1);\n}\n\n"
           "void Get_Bitmap_Actual_Size_Oryginal(SAMB_ptr bitmap_addr, int16_t * x1, int16_t * y1, int16_t * width, int16_t * height)\n{\n",
           ile=1)
    # 5. Find_Closest_Color: 256 kolorow na wywolanie, Create_Remap_Palette_
    #    wola ja 256 razy na blok (wejscie do walki: 11% probek). Ciasna
    #    petla na wskazniku, kwadraty z tablicy (roznice < 21). Te same
    #    progi, ta sama kolejnosc sumy, remis wygrywa NIZSZY indeks - jak
    #    w oryginale (zostaje jako _Oryginal).
    rel_fo = "MoX/src/Fonts.c"
    zamien(rel_fo,
           "uint8_t Find_Closest_Color(uint8_t red, uint8_t green, uint8_t blue)\n{\n",
           "static const uint16_t amiga_kwadrat[REMAP_THRESHOLD] = {\n"
           "    0, 1, 4, 9, 16, 25, 36, 49, 64, 81, 100, 121, 144, 169, 196, 225, 256, 289, 324, 361, 400 };\n"
           "uint8_t Find_Closest_Color(uint8_t red, uint8_t green, uint8_t blue)\n{\n"
           "    /* AMIGA: szybka wersja, wynik jak Find_Closest_Color_Oryginal */\n"
           "    const uint8_t * pal = (const uint8_t *)(current_palette);\n"
           "    int itr;\n    int dr;\n    int dg;\n    int db;\n    int dif;\n    int najlepszy = 10000;\n"
           "    uint8_t closest = 0;\n"
           "    for(itr = 0; itr < 256; itr++, pal += 3)\n    {\n"
           "        dr = (int)pal[0] - (int)red;   if(dr < 0) { dr = -dr; } if(dr >= REMAP_THRESHOLD) { continue; }\n"
           "        dg = (int)pal[1] - (int)green; if(dg < 0) { dg = -dg; } if(dg >= REMAP_THRESHOLD) { continue; }\n"
           "        db = (int)pal[2] - (int)blue;  if(db < 0) { db = -db; } if(db >= REMAP_THRESHOLD) { continue; }\n"
           "        dif = amiga_kwadrat[db] + amiga_kwadrat[dr] + amiga_kwadrat[dg];\n"
           "        if(dif < najlepszy) { najlepszy = dif; closest = (uint8_t)itr; }\n    }\n"
           "    return closest;\n}\n\n"
           "uint8_t Find_Closest_Color_Oryginal(uint8_t red, uint8_t green, uint8_t blue);\n"
           "uint8_t Find_Closest_Color_Oryginal(uint8_t red, uint8_t green, uint8_t blue)\n{\n",
           ile=1)
    # 6. Fill (prostokat koloru; nieodkryta mapa, przewijanie: 30-40% probek)
    #    liczy adres kazdego piksela mnozeniem. memset wierszami - te same
    #    bajty; przesuniecie poczatku zostaje 16-bitowe jak w oryginale.
    zamien("MoX/src/Graphics.c",
           "    itr_height = 0;\n    while(itr_height < height)\n    {\n        itr_width = 0;\n"
           "        while(itr_width < width)\n        {\n"
           "            *(screen_page + (itr_height * SCREEN_WIDTH) + itr_width) = color;\n"
           "            itr_width++;\n        }\n        itr_height++;\n    }\n",
           "    itr_height = 0;  /* AMIGA: memset wiersza zamiast piksel po pikselu */\n"
           "    (void)itr_width;\n"
           "    if(width > 0)\n    {\n"
           "        while(itr_height < height)\n        {\n"
           "            memset(screen_page + (itr_height * SCREEN_WIDTH), color, (size_t)width);\n"
           "            itr_height++;\n        }\n    }\n", ile=1)
    if "#include <string.h>" not in czytaj("MoX/src/Graphics.c"):
        t = czytaj("MoX/src/Graphics.c")
        i = t.index("#include")
        zapisz("MoX/src/Graphics.c", t[:i] + "#include <string.h>  /* AMIGA: memset */\n" + t[i:])
    # 7. Zrzuty danych gry gd_dump_* (tura AI: ~10% probek + formatowanie)
    #    formatuja KAZDE pole ~1000 jednostek przez snprintf i oddaja to do
    #    LOG_TRACE. build.sh kompiluje z STU_LOG_MIN_SEVERITY=LOG_SEV_INFO
    #    (TRACE/DEBUG to wtedy puste makra), wiec zrzut i tak nic nie
    #    wypisze - wychodzimy od razu. Gra i RNG bez zmian.
    import re as _re
    rel_ig = "MoM/src/INITGAME.c"
    t = czytaj(rel_ig)
    t, n = _re.subn(r"(\nvoid gd_dump_\w+\(const char\* point\) \{\r?\n)",
                    r"\1    if(STU_LOG_MIN_SEVERITY > LOG_SEV_TRACE) { (void)point; return; }  /* AMIGA: TRACE wyciety */\n",
                    t)
    if n != 9:
        sys.exit("LATKA NIE PASUJE: %s - gd_dump_* (%d zamiast 9)" % (rel_ig, n))
    zapisz(rel_ig, t)
    # 8. Create_Remap_Palette_ (24 bloki x 256 x Find_Closest_Color po 256)
    #    przy kazdym wejsciu do walki i wyjsciu z niej: ~1,5 s na raz.
    #    Wynik zalezy WYLACZNIE od palety (current_palette: 768 B kolorow +
    #    256 B flag) i czterech parametrow - pamietamy ostatnie 72 wyniki.
    #    Ta sama paleta i parametry -> te same 256 bajtow, wiec wynik
    #    identyczny; kolejne walki i ekrany nie licza od nowa.
    zamien(rel_fo,
           "void Create_Remap_Palette_(int16_t block, uint8_t red, uint8_t green, uint8_t blue, uint8_t percent)\n{\n",
           "static void Create_Remap_Palette_Licz(int16_t block, uint8_t red, uint8_t green, uint8_t blue, uint8_t percent);\n"
           "#define AMIGA_REMAP_WPISY 72\n"
           "#define AMIGA_REMAP_WPIS (4 + 1024 + 256)\n"
           "static uint8_t * amiga_remap_pamiec = NULL;\n"
           "static int amiga_remap_ile = 0;\n"
           "static int amiga_remap_nastepny = 0;\n"
           "void Create_Remap_Palette_(int16_t block, uint8_t red, uint8_t green, uint8_t blue, uint8_t percent)\n{\n"
           "    /* AMIGA: pamiec wynikow (wynik = f(paleta 1024 B, parametry)) */\n"
           "    uint8_t * cel = (uint8_t *)(remap_color_palettes + (block * 256));\n"
           "    uint8_t * w;\n    int i;\n"
           "    if(amiga_remap_pamiec == NULL)\n    {\n"
           "        amiga_remap_pamiec = (uint8_t *)malloc((size_t)AMIGA_REMAP_WPISY * AMIGA_REMAP_WPIS);\n    }\n"
           "    if(amiga_remap_pamiec != NULL)\n    {\n"
           "        for(i = 0; i < amiga_remap_ile; i++)\n        {\n"
           "            w = amiga_remap_pamiec + (long)i * AMIGA_REMAP_WPIS;\n"
           "            if(w[0] == red && w[1] == green && w[2] == blue && w[3] == percent\n"
           "               && memcmp(w + 4, current_palette, 1024) == 0)\n            {\n"
           "                memcpy(cel, w + 4 + 1024, 256);\n                return;\n            }\n        }\n    }\n"
           "    Create_Remap_Palette_Licz(block, red, green, blue, percent);\n"
           "    if(amiga_remap_pamiec != NULL)\n    {\n"
           "        w = amiga_remap_pamiec + (long)amiga_remap_nastepny * AMIGA_REMAP_WPIS;\n"
           "        w[0] = red;\n        w[1] = green;\n        w[2] = blue;\n        w[3] = percent;\n"
           "        memcpy(w + 4, current_palette, 1024);\n"
           "        memcpy(w + 4 + 1024, cel, 256);\n"
           "        amiga_remap_nastepny = (amiga_remap_nastepny + 1) % AMIGA_REMAP_WPISY;\n"
           "        if(amiga_remap_ile < AMIGA_REMAP_WPISY)\n        {\n            amiga_remap_ile++;\n        }\n    }\n}\n\n"
           "static void Create_Remap_Palette_Licz(int16_t block, uint8_t red, uint8_t green, uint8_t blue, uint8_t percent)\n{\n",
           ile=1)
    # 9. Pierwsze liczenie tez szybciej: w Create_Remap_Palette_Licz paleta
    #    jest stala, wiec raz sortujemy ja po czerwonym i dla kazdego
    #    zapytania idziemy od najblizszego czerwonego w obie strony, konczac,
    #    gdy dr^2 przekroczy najlepszy wynik (dif >= dr^2). Progi (< 21 na
    #    kanal), suma kwadratow i remis (nizszy indeks) - jak w
    #    Find_Closest_Color_Oryginal; brak kandydata -> 0.
    zamien(rel_fo,
           "static void Create_Remap_Palette_Licz(int16_t block, uint8_t red, uint8_t green, uint8_t blue, uint8_t percent)\n{\n",
           "static uint8_t amiga_ss_r[256];\nstatic uint8_t amiga_ss_g[256];\nstatic uint8_t amiga_ss_b[256];\n"
           "static uint8_t amiga_ss_i[256];\nstatic uint16_t amiga_ss_od[257];\n"
           "static const uint16_t amiga_ss_kw[REMAP_THRESHOLD] = {\n"
           "    0, 1, 4, 9, 16, 25, 36, 49, 64, 81, 100, 121, 144, 169, 196, 225, 256, 289, 324, 361, 400 };\n"
           "static void Amiga_Szukaj_Buduj(const uint8_t * pal)\n{\n"
           "    uint16_t ile[256];\n    int i;\n    int v;\n    int poz;\n"
           "    memset(ile, 0, sizeof(ile));\n"
           "    for(i = 0; i < 256; i++) { ile[pal[i * 3]]++; }\n"
           "    poz = 0;\n"
           "    for(v = 0; v < 256; v++) { amiga_ss_od[v] = (uint16_t)poz; poz += ile[v]; }\n"
           "    amiga_ss_od[256] = 256;\n"
           "    memcpy(ile, amiga_ss_od, sizeof(ile));\n"
           "    for(i = 0; i < 256; i++)\n    {\n"
           "        poz = ile[pal[i * 3]]++;\n"
           "        amiga_ss_r[poz] = pal[i * 3];\n        amiga_ss_g[poz] = pal[i * 3 + 1];\n"
           "        amiga_ss_b[poz] = pal[i * 3 + 2];\n        amiga_ss_i[poz] = (uint8_t)i;\n    }\n}\n"
           "static uint8_t Amiga_Szukaj_Koloru(int r, int g, int b)\n{\n"
           "    int gora = amiga_ss_od[r];\n    int dol = gora - 1;\n"
           "    int najlepszy = 10000;\n    int najl_idx = 256;\n"
           "    int dr;\n    int dg;\n    int db;\n    int dif;\n    int p;\n"
           "    for(;;)\n    {\n"
           "        int dr_g = (gora < 256) ? (amiga_ss_r[gora] - r) : 1000;\n"
           "        int dr_d = (dol >= 0) ? (r - amiga_ss_r[dol]) : 1000;\n"
           "        if(dr_g <= dr_d) { p = gora++; dr = dr_g; } else { p = dol--; dr = dr_d; }\n"
           "        if(dr >= REMAP_THRESHOLD || dr * dr > najlepszy) { break; }\n"
           "        dg = (int)amiga_ss_g[p] - g; if(dg < 0) { dg = -dg; } if(dg >= REMAP_THRESHOLD) { continue; }\n"
           "        db = (int)amiga_ss_b[p] - b; if(db < 0) { db = -db; } if(db >= REMAP_THRESHOLD) { continue; }\n"
           "        dif = amiga_ss_kw[db] + amiga_ss_kw[dr] + amiga_ss_kw[dg];\n"
           "        if(dif < najlepszy || (dif == najlepszy && amiga_ss_i[p] < najl_idx))\n"
           "        {\n            najlepszy = dif;\n            najl_idx = amiga_ss_i[p];\n        }\n    }\n"
           "    return (najl_idx < 256) ? (uint8_t)najl_idx : 0;\n}\n\n"
           "static void Create_Remap_Palette_Licz(int16_t block, uint8_t red, uint8_t green, uint8_t blue, uint8_t percent)\n{\n",
           ile=1)
    zamien(rel_fo,
           "    remap_palette   = (uint8_t *)(remap_color_palettes + (block * 256));\n"
           "    tmpcurrpal = (uint8_t *)(current_palette);\n",
           "    remap_palette   = (uint8_t *)(remap_color_palettes + (block * 256));\n"
           "    tmpcurrpal = (uint8_t *)(current_palette);\n"
           "    Amiga_Szukaj_Buduj(tmpcurrpal);  /* AMIGA */\n",
           ile=1)
    zamien(rel_fo,
           "            closest = Find_Closest_Color(color3_red, color3_grn, color3_blu);\n        }\n\n"
           "        *(remap_palette + itr) = closest;",
           "            closest = Amiga_Szukaj_Koloru(color3_red, color3_grn, color3_blu);  /* AMIGA: = Find_Closest_Color */\n        }\n\n"
           "        *(remap_palette + itr) = closest;",
           ile=1)
    for naglowek in ("#include <string.h>", "#include <stdlib.h>"):
        if naglowek not in czytaj(rel_fo):
            t = czytaj(rel_fo)
            i = t.index("#include")
            zapisz(rel_fo, t[:i] + naglowek + "  /* AMIGA: pamiec wynikow remap */\n" + t[i:])
    if "#include <string.h>" not in czytaj(rel):
        zamien(rel, "#include \"../../platform/include/Platform.h\"",
               "#include <string.h>  /* AMIGA: memcpy */\n#include \"../../platform/include/Platform.h\"", ile=1)


def latki_menu_amigi():
    # Menu glowne (developer, 2026-09-17):
    #  - "Quit To DOS" -> "Quit To AmigaOS",
    #  - nowa pozycja "Amiga Options" miedzy New Game a Hall Of Fame (skrot A),
    #    cale menu 12 px wyzej (Hall Of Fame i Quit zostaja na miejscu),
    #  - napisy tworcow koncza sie 12 px wyzej, zeby nie wchodzily na Continue.
    # Napisy nowych pozycji rysuje amiga_Opcje.c (font 4 w kolorach bitmap
    # menu z VORTEX.LBX gracza).
    rel = "MoM/src/MainMenu.c"
    zamien(rel, "int16_t current_menu_screen;\n",
           "int16_t current_menu_screen;\n"
           "static int16_t _amiga_opcje_button = 0;  /* AMIGA: \"Amiga Options\" */\n"
           "static int16_t _amiga_opcje_hotkey = 0;\n"
           "void Amiga_Opcje_Ekran(void);\n"
           "void Amiga_Rysuj_Napis_Menu(const char * tekst, int x, int y, int podswietlony);\n", ile=1)
    zamien(rel,
           "        _load_button = Add_Hidden_Field(108, 150, 211, 161, 0, ST_UNDEFINED);\n"
           "        _help_entries[0].help_idx = HLP_LOAD;\n"
           "        _help_entries[0].x1 = 108;\n"
           "        _help_entries[0].y1 = 148;\n"
           "        _help_entries[0].x2 = 211;\n"
           "        _help_entries[0].y2 = 162;\n",
           "        _load_button = Add_Hidden_Field(108, 138, 211, 149, 0, ST_UNDEFINED);  /* AMIGA: 12 px wyzej */\n"
           "        _help_entries[0].help_idx = HLP_LOAD;\n"
           "        _help_entries[0].x1 = 108;\n"
           "        _help_entries[0].y1 = 136;\n"
           "        _help_entries[0].x2 = 211;\n"
           "        _help_entries[0].y2 = 150;\n", ile=1)
    zamien(rel, "(138 + (12 * menu_shift))", "(126 + (12 * menu_shift))", ile=2)
    zamien(rel, "(149 + (12 * menu_shift))", "(137 + (12 * menu_shift))", ile=2)
    zamien(rel,
           "    _new_button = Add_Hidden_Field(108, 162, 211, 173, 0, ST_UNDEFINED);\n"
           "    _help_entries[2].help_idx = HLP_NEW_GAME;\n"
           "    _help_entries[2].x1 = 108;\n"
           "    _help_entries[2].y1 = 162;\n"
           "    _help_entries[2].x2 = 211;\n"
           "    _help_entries[2].y2 = 173;\n",
           "    _new_button = Add_Hidden_Field(108, 150, 211, 161, 0, ST_UNDEFINED);  /* AMIGA: 12 px wyzej */\n"
           "    _help_entries[2].help_idx = HLP_NEW_GAME;\n"
           "    _help_entries[2].x1 = 108;\n"
           "    _help_entries[2].y1 = 150;\n"
           "    _help_entries[2].x2 = 211;\n"
           "    _help_entries[2].y2 = 161;\n"
           "    _amiga_opcje_button = Add_Hidden_Field(108, 162, 211, 173, 0, ST_UNDEFINED);  /* AMIGA */\n", ile=1)
    zamien(rel,
           "    _quit_hotkey = Add_Hot_Key('Q');\n    _esc_hotkey = Add_Hot_Key(27);",
           "    _quit_hotkey = Add_Hot_Key('Q');\n"
           "    _amiga_opcje_hotkey = Add_Hot_Key('A');  /* AMIGA */\n"
           "    _esc_hotkey = Add_Hot_Key(27);", ile=1)
    zamien(rel,
           "            if((input_field_idx == _hof_hotkey) || (input_field_idx == _hof_button))\n",
           "            if((input_field_idx == _amiga_opcje_hotkey) || (input_field_idx == _amiga_opcje_button))\n"
           "            {\n"
           "                /* AMIGA: ekran opcji, potem menu od nowa */\n"
           "                Amiga_Opcje_Ekran();\n"
           "                leave_screen_flag = ST_TRUE;\n"
           "                current_menu_screen = 5;\n"
           "                current_screen = scr_Main_Menu_Screen;\n"
           "            }\n"
           "            if((input_field_idx == _hof_hotkey) || (input_field_idx == _hof_button))\n", ile=1)
    zamien(rel, "    menu_y_start = 141;\n\n    scanned_field = Scan_Input();",
           "    menu_y_start = 129;  /* AMIGA: 141 - miejsce na \"Amiga Options\" */\n\n    scanned_field = Scan_Input();", ile=2)  # takze wersja debug
    zamien(rel,
           "\n    FLIC_Draw(menu_x_start, (menu_y_start + 36), mainmenu_h);",
           "\n    Amiga_Rysuj_Napis_Menu(\"Amiga Options\", menu_x_start, (menu_y_start + 36),\n"
           "                           (scanned_field == _amiga_opcje_button) ? 1 : 0);  /* AMIGA */\n"
           "    FLIC_Draw(menu_x_start, (menu_y_start + 48), mainmenu_h);", ile=1)
    zamien(rel,
           "\n    FLIC_Draw(menu_x_start, (menu_y_start + 48), mainmenu_q);",
           "\n    /* AMIGA: \"Quit To AmigaOS\" zamiast bitmapy \"Quit To DOS\" */\n"
           "    Amiga_Rysuj_Napis_Menu(\"Quit To AmigaOS\", menu_x_start, (menu_y_start + 60),\n"
           "                           (scanned_field == _quit_button) ? 1 : 0);", ile=1)

    rel = "MoM/src/CREDITS.c"
    zamien(rel, "    Set_Window(0, 40, 319, 137);", "    Set_Window(0, 40, 319, 125);  /* AMIGA: 137 - menu 12 px wyzej */", ile=1)
    zamien(rel, "            (line_top >= 99)", "            (line_top >= 87)  /* AMIGA: 99 */", ile=1)
    zamien(rel, "        if(line_top > 88)", "        if(line_top > 76)  /* AMIGA: 88 */", ile=1)
    zamien(rel, "(line_top - 88)", "(line_top - 76)", ile=1)
    zamien(rel, "_credits_y = 95;", "_credits_y = 83;  /* AMIGA: 95 */", ile=2)


def latki_bez_float():
    # Silnik gry nie ma arytmetyki zmiennoprzecinkowej, ale trzy dopiski
    # upstreamu wolaly soft-float (nm -u obiektow, 2026-09-17). Na 68k bez
    # FPU kazde takie dzialanie to wywolanie biblioteki - zamiana na liczby
    # calkowite / 16.16.
    #
    # 1. User_Mouse_Handler: skala okna jako float przy KAZDYM zdarzeniu
    #    myszy. 16.16: dla okna 320 (Amiga) skala = 1.0 dokladnie, wynik
    #    identyczny; dzielenie calkowite obcina do zera jak rzutowanie floata.
    zamien("MoX/src/Mouse.c",
           "    float screen_scale = (float)Platform_Get_Window_Width() / (float)SCREEN_WIDTH;\n"
           "    int16_t gx = (int16_t)(l_mx / screen_scale);\n"
           "    int16_t gy = (int16_t)(l_my / screen_scale);\n",
           "    /* AMIGA: skala 16.16 zamiast float (soft-float przy kazdym ruchu myszy) */\n"
           "    long screen_scale_16 = ((long)Platform_Get_Window_Width() << 16) / (long)SCREEN_WIDTH;\n"
           "    int16_t gx = (int16_t)((screen_scale_16 > 0) ? (((long)l_mx << 16) / screen_scale_16) : l_mx);\n"
           "    int16_t gy = (int16_t)((screen_scale_16 > 0) ? (((long)l_my << 16) / screen_scale_16) : l_my);\n",
           ile=1)
    zamien("MoX/src/Mouse.c",
           " scale=%.2f enabled=%d interrupt=%d\\n\", (unsigned long long)Platform_Get_Millies(), buttons, l_mx, l_my, gx, gy, screen_scale,",
           " scale16=%ld enabled=%d interrupt=%d\\n\", (unsigned long long)Platform_Get_Millies(), buttons, l_mx, l_my, gx, gy, screen_scale_16,",
           ile=1)

    # 2. PFL_Perf.c (pomiar fps PERF-LIVE, raz na sekunde + podsumowanie
    #    przy wyjsciu): percentyle w promilach, fps i milisekundy
    #    w dziesiatych/tysiecznych jako liczby calkowite.
    rel = "platform/metrics/PFL_Perf.c"
    zamien(rel,
           "static uint64_t perf_percentile(const uint64_t * sorted, uint32_t n, double p)\n{\n"
           "    uint32_t rank;\n\n    if(n == 0) { return 0; }\n\n"
           "    rank = (uint32_t)(p * (double)n);\n"
           "    if((double)rank < (p * (double)n)) { rank++; }   /* ceil */\n",
           "static uint64_t perf_percentile(const uint64_t * sorted, uint32_t n, uint32_t p_promile)\n{\n"
           "    uint32_t rank;\n\n    if(n == 0) { return 0; }\n\n"
           "    rank = (uint32_t)(((uint64_t)p_promile * n + 999u) / 1000u);   /* AMIGA: ceil w promilach, bez double */\n",
           ile=1)
    for stare, nowe in (("0.50)", "500)"), ("0.95)", "950)"), ("0.99)", "990)")):
        zamien(rel, "perf_percentile(sorted, n, " + stare, "perf_percentile(sorted, n, " + nowe, ile=1)
        zamien(rel, "perf_percentile(durs, n, " + stare, "perf_percentile(durs, n, " + nowe, ile=1)
    zamien(rel,
           "    if(fps != NULL)         { *fps = (sum > 0) ? (1000.0 * (double)n / (double)sum) : 0.0; }\n",
           "    if(fps != NULL)         { *fps = 0; (void)sum; }  /* AMIGA: bez double (jedyny wolajacy podaje NULL) */\n",
           ile=1)
    zamien(rel,
           "        double   fps = (elapsed > 0) ? (1000.0 * (double)m_live_window_frames / (double)elapsed) : 0.0;\n",
           "        unsigned fps10 = (elapsed > 0) ? (unsigned)((10000u * (uint64_t)m_live_window_frames + elapsed / 2) / elapsed) : 0u;  /* AMIGA: fps x10 */\n",
           ile=1)
    zamien(rel,
           "        snprintf(title, sizeof(title), \"%s - %.1f fps (worst %u ms)\",\n"
           "                 m_live_base_title, fps, (unsigned)m_live_window_worst_ms);\n",
           "        snprintf(title, sizeof(title), \"%s - %u.%u fps (worst %u ms)\",\n"
           "                 m_live_base_title, fps10 / 10u, fps10 % 10u, (unsigned)m_live_window_worst_ms);\n",
           ile=1)
    zamien(rel,
           "\"[PERF-LIVE] %.1f fps  frame_ms p50=%u p95=%u p99=%u max=%u (last %u)  interval_over=%u/%u  work_over=%u/%u  ticks=%u idle=%u  (budget %d ms)\",\n"
           "                         fps, p50,",
           "\"[PERF-LIVE] %u.%u fps  frame_ms p50=%u p95=%u p99=%u max=%u (last %u)  interval_over=%u/%u  work_over=%u/%u  ticks=%u idle=%u  (budget %d ms)\",\n"
           "                         fps10 / 10u, fps10 % 10u, p50,",
           ile=1)
    zamien(rel,
           "snprintf(m_live_line1, sizeof(m_live_line1), \"FPS %.1f\", fps);",
           "snprintf(m_live_line1, sizeof(m_live_line1), \"FPS %u.%u\", fps10 / 10u, fps10 % 10u);",
           ile=1)
    # podsumowanie stref: "(double)s.X_us / 1000.0" -> dwie liczby calkowite
    t = czytaj(rel)
    t2 = t
    for pole in ("total_us", "self_total_us", "p50_us", "p95_us", "p99_us", "max_us"):
        t2 = t2.replace("(double)s.%s / 1000.0" % pole,
                        "(unsigned)(s.%s / 1000u), (unsigned)(s.%s %% 1000u)" % (pole, pole))
    t2 = t2.replace("#zone %6u %11u %12.3f %13.3f %10.3f %10.3f %10.3f %10.3f  %-40s",
                    "#zone %6u %11u %8u.%03u %9u.%03u %6u.%03u %6u.%03u %6u.%03u %6u.%03u  %-40s")
    t2 = t2.replace("total=%.1fms self=%.1fms p50=%.3f p95=%.3f p99=%.3f max=%.3f ms",
                    "total=%u.%03ums self=%u.%03ums p50=%u.%03u p95=%u.%03u p99=%u.%03u max=%u.%03u ms")
    if t2.count("(unsigned)(s.") != 24 or "%.3f" in t2 or "%12.3f" in t2:
        sys.exit("LATKA NIE PASUJE: %s - podsumowanie stref bez double" % rel)
    zapisz(rel, t2)

    # 3. STU_WRLD.c (symulacja generatora swiata, ~40 miejsc z double) nie
    #    jest kompilowany wcale - build.sh wyrzuca go z listy zrodel; jedyny
    #    jego symbol globalny (Simulate_World_Map_Generation) nie jest
    #    uzywany przez zaden inny obiekt (nm -u, 2026-09-17).


def main():
    global ROOT
    if len(sys.argv) != 2:
        sys.exit("uzycie: remom-patch.py <katalog ReMoM>")
    ROOT = sys.argv[1]
    latki_natywne()
    latki_adresy()
    latki_pamiec()
    latki_log()
    latki_czas()
    latki_surowe_dane()
    latki_diagnoza()
    latki_start()
    latki_dzwiek()
    latki_pompa_muzyki()
    latki_remom()
    latki_wydajnosc()
    latki_bez_float()
    latki_menu_amigi()
    print("remom-patch: latki nalozone")


main()
