/*
 * Stack size for the libnix startup code.
 *
 * libnix reads this symbol when the program starts and allocates its stack
 * accordingly. Without it a program gets the shell's default - 4 KB - which a
 * C++ program with a state stack, recursive YAML parsing and 300-byte stack
 * frames blows through immediately. What that looks like from outside is not a
 * Guru: the exception handler itself faults on the dead stack, the emulated
 * 68040 double-faults, and WinUAE stops the CPU and shows HALT1. Black screen,
 * no log, nothing to read.
 *
 * The AmigaDOS "Stack" command in the startup script does NOT cover this - it
 * sets the shell's idea of the stack for the process it spawns, which libnix
 * has already overridden by the time main() runs. The openttd_amiga_68k port
 * carries the same 1 MB symbol for the same reason.
 */
/* 2026-09-17, ReMoM: EKSPERYMENT 8 MB. Na 1 MB HeMoM stawal w rysowaniu ekranu
   glownego, a zmiana optymalizacji jednego pliku (-O2 -> -O0) PRZESUWALA
   miejsce awarii - typowy objaw przepelnienia stosu (wieksze ramki na -O0).
   Kod ReMoM pochodzi z PC, gdzie stos ma 1-8 MB i strone ochronna; na Amidze
   przepelniony stos po cichu niszczy pamiec pod soba. */
unsigned long __stack = 8UL * 1024UL * 1024UL;
