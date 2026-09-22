/*
 * straznik.c - reset Amigi na zadanie hosta, BEZ restartu emulatora.
 *
 * Kazdy start WinUAE zabiera developerowi mysz, wiec emulator dziala caly
 * czas, a host tylko podmienia binarki i Work:run. Ten program startuje
 * z Work:run (Run >NIL:), co pol sekundy sprawdza Work:reset.flag i gdy
 * plik sie pojawi, kasuje go (inaczej reset zapetlilby sie po starcie)
 * i robi ColdReboot(). Maszyna startuje od nowa i wykonuje nowy Work:run.
 *
 * Drugi egzemplarz nie startuje: nazwa zadania "straznik-remom".
 * Ctrl-C konczy program.
 */
#include <exec/types.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#define NAZWA_ZADANIA "straznik-remom"
#define FLAGA "Work:reset.flag"

int main(void)
{
    struct Task * ja;
    BPTR zamek;

    Forbid();
    if(FindTask((STRPTR)NAZWA_ZADANIA) != NULL)
    {
        Permit();
        return 0;               /* juz pilnuje */
    }
    ja = FindTask(NULL);
    ja->tc_Node.ln_Name = (char *)NAZWA_ZADANIA;
    Permit();

    for(;;)
    {
        if(SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
        {
            break;
        }
        zamek = Lock((STRPTR)FLAGA, ACCESS_READ);
        if(zamek != 0)
        {
            UnLock(zamek);
            DeleteFile((STRPTR)FLAGA);
            Delay(25);          /* pol sekundy na zapis katalogu hosta */
            ColdReboot();
        }
        Delay(25);
    }
    return 0;
}
