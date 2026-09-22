/* adpcm-sprawdz.c - narzedzie HOSTOWE: dekoduje pliki Work:muzyka/*.wav
   tym samym kodem, ktorego uzywa Amiga (native/amiga_adpcm.c), i wypisuje
   dlugosc, szczyt i RMS. Plik niemy albo nieczytelny = blad renderu.
   gcc -O2 -I native -o adpcm-sprawdz build/adpcm-sprawdz.c native/amiga_adpcm.c -lm */
#include <math.h>
#include <stdio.h>
#include "amiga_adpcm.h"

int main(int argc, char ** argv)
{
    int i;
    int zle = 0;
    for(i = 1; i < argc; i++)
    {
        AdpcmStream * s = Adpcm_Open(argv[i]);
        signed char bufor[4096];
        long probek = 0;
        int szczyt = 0;
        double suma = 0.0;
        int n;
        if(s == NULL)
        {
            printf("%s: NIE OTWIERA SIE\n", argv[i]);
            zle++;
            continue;
        }
        while((n = Adpcm_Decode(s, bufor, sizeof(bufor))) > 0)
        {
            int k;
            for(k = 0; k < n; k++)
            {
                int v = bufor[k] < 0 ? -bufor[k] : bufor[k];
                if(v > szczyt) szczyt = v;
                suma += (double)bufor[k] * bufor[k];
            }
            probek += n;
        }
        printf("%s: %d Hz, %.1f s, szczyt %d, RMS %.1f\n", argv[i], Adpcm_Rate(s),
               (double)probek / Adpcm_Rate(s), szczyt, probek ? sqrt(suma / probek) : 0.0);
        if(probek == 0 || szczyt < 2)
        {
            zle++;
        }
        Adpcm_Close(s);
    }
    printf("plikow %d, zlych %d\n", argc - 1, zle);
    return zle ? 1 : 0;
}
