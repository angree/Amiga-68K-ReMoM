/*
 * amiga_hemom_stuby.c - zaslepki dla HeMoM (silnik bez ekranu).
 *
 * Latka menu glownego (remom-patch.py, latki_menu_amigi) wola funkcje
 * backendu ekranu (amiga_Opcje.c), ktorego HeMoM nie linkuje. Menu w HeMoM
 * nigdy sie nie wyswietla - zaslepki tylko domykaja linkowanie.
 */
void Amiga_Opcje_Ekran(void)
{
}

void Amiga_Rysuj_Napis_Menu(const char * tekst, int x, int y, int podswietlony)
{
    (void)tekst;
    (void)x;
    (void)y;
    (void)podswietlony;
}

/* rules (0.8.3, amiga_le.h): HeMoM gra zawsze z poprawkami portu, jak
   domyslnie remom - wynik zgodny z wyrocznia na PC nie zalezy od amiga.cfg */
int amiga_opt_oryginal = 0;
