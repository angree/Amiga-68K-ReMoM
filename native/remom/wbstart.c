/*
 * wbstart.c - uruchomienie programu TAK JAK ROBI TO WORKBENCH (narzedzie
 * testowe, bez myszy): LoadSeg, nowy proces bez CLI z katalogiem
 * biezacym innym niz program (SYS:), komunikat WBStartup z blokada katalogu
 * programu, czekanie na odpowiedz (koniec programu).
 *
 * Najpierw czyta ikone programu przez icon.library (GetDiskObject) - typ
 * i stos z ikony ida do logu i sa uzyte dla nowego procesu, jak na WB.
 *
 * UZYCIE (Work:run):  wbstart remom-rtg >Work:wbstart.log
 *                    wbstart IKONA remom-prefs   (tylko odczyt ikony)
 * Kod wyjscia: 0 - program wystartowal i sie zakonczyl, 10 - blad.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>

#include <stdio.h>
#include <string.h>

struct Library * IconBase = NULL;

int main(int argc, char ** argv)
{
    const char * nazwa;
    struct DiskObject * ikona;
    ULONG stos = 4096;
    BPTR seg;
    BPTR katalog;
    struct Process * proc;
    struct MsgPort * odpowiedz;
    struct WBStartup * msg;
    struct WBArg * arg;

    int tylko_ikona = 0;

    if(argc >= 3 && strcmp(argv[1], "IKONA") == 0)
    {
        tylko_ikona = 1;
        argv++;
        argc--;
    }
    if(argc < 2)
    {
        printf("uzycie: wbstart [IKONA] <program>\n");
        return 10;
    }
    nazwa = argv[1];

    IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 36);
    if(IconBase == NULL)
    {
        printf("wbstart: brak icon.library\n");
        return 10;
    }
    ikona = GetDiskObject((STRPTR)nazwa);
    if(ikona == NULL)
    {
        printf("wbstart: ikona %s.info NIE wczytana (blad %ld)\n", nazwa, (long)IoErr());
        CloseLibrary(IconBase);
        return 10;
    }
    printf("wbstart: ikona %s.info: typ %d (3 = narzedzie), stos %ld, %dx%d\n", nazwa,
           (int)ikona->do_Type, (long)ikona->do_StackSize,
           (int)ikona->do_Gadget.Width, (int)ikona->do_Gadget.Height);
    if(ikona->do_StackSize > 4096)
    {
        stos = (ULONG)ikona->do_StackSize;
    }
    FreeDiskObject(ikona);
    CloseLibrary(IconBase);
    fflush(stdout);
    if(tylko_ikona)
    {
        return 0;
    }

    seg = LoadSeg((STRPTR)nazwa);
    katalog = Lock((STRPTR)"", ACCESS_READ);   /* katalog programu = biezacy (Work:) */
    odpowiedz = CreateMsgPort();
    msg = (struct WBStartup *)AllocVec(sizeof(struct WBStartup), MEMF_CLEAR | MEMF_PUBLIC);
    arg = (struct WBArg *)AllocVec(sizeof(struct WBArg), MEMF_CLEAR | MEMF_PUBLIC);
    if(seg == 0 || katalog == 0 || odpowiedz == NULL || msg == NULL || arg == NULL)
    {
        printf("wbstart: nie da sie przygotowac startu (seg %ld)\n", (long)seg);
        return 10;
    }

    /* Jak Workbench: brak CLI, wejscie/wyjscie NIL:, katalog biezacy
       celowo INNY niz program (SYS:) - program musi sam przejsc do wa_Lock. */
    proc = CreateNewProcTags(NP_Seglist, (ULONG)seg,
                             NP_FreeSeglist, FALSE,
                             NP_Name, (ULONG)nazwa,
                             NP_StackSize, stos,
                             NP_Cli, FALSE,
                             NP_CurrentDir, (ULONG)Lock((STRPTR)"SYS:", ACCESS_READ),
                             TAG_DONE);
    if(proc == NULL)
    {
        printf("wbstart: CreateNewProc nieudany\n");
        UnLoadSeg(seg);
        return 10;
    }

    arg->wa_Lock = katalog;
    arg->wa_Name = (BYTE *)nazwa;
    msg->sm_Message.mn_Node.ln_Type = NT_MESSAGE;
    msg->sm_Message.mn_ReplyPort = odpowiedz;
    msg->sm_Message.mn_Length = sizeof(struct WBStartup);
    msg->sm_Process = &proc->pr_MsgPort;
    msg->sm_Segment = seg;
    msg->sm_NumArgs = 1;
    msg->sm_ToolWindow = NULL;
    msg->sm_ArgList = arg;
    PutMsg(&proc->pr_MsgPort, (struct Message *)msg);
    printf("wbstart: %s uruchomiony ze stosem %lu, czekam na koniec\n", nazwa, (unsigned long)stos);
    fflush(stdout);

    WaitPort(odpowiedz);
    GetMsg(odpowiedz);
    printf("wbstart: %s zakonczony\n", nazwa);

    UnLoadSeg(seg);
    UnLock(katalog);
    FreeVec(arg);
    FreeVec(msg);
    DeleteMsgPort(odpowiedz);
    return 0;
}
