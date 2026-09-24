# Master of Magic for Amiga 68k (ReMoM port)

A native AmigaOS 68k port of **Master of Magic** (Simtex, 1994), built from
[jbalcomb/ReMoM](https://github.com/jbalcomb/ReMoM), a reconstruction of
the game's source code in C. It runs on AGA or on an 8-bit RTG screen, with
Workbench still running in the background.

![Main menu](docs/screenshots/menu.png) ![Overland map](docs/screenshots/mapa.png)
![Combat](docs/screenshots/walka-1.png) ![Spellbook](docs/screenshots/czary.png)
![Cities](docs/screenshots/miasta.png) ![Myrror](docs/screenshots/myrror.png)

**This repository and its releases contain no game data.** You need your own
copy of Master of Magic. The GOG version works (see below).

## Requirements

- A 68020 or better, no FPU needed. **A 68030 at 50 MHz is recommended.**
- AGA, or an RTG card with an 8-bit screen mode (CyberGraphX or Picasso96)
- Kickstart and Workbench 3.1 or newer
- 8 MB of Fast RAM, plus 2 MB of Chip RAM on AGA
- A hard disk with about 25 MB free for the game data, about 60 MB with
  11 kHz music or about 100 MB with 22 kHz music

## Installing with the GOG version

1. Install *Master of Magic Classic* from GOG on a PC.
2. Open the install folder and find the one that contains `MAGIC.EXE` and the
   `*.LBX` files. It may be the install folder itself or a subfolder of it.
3. Copy these files to one directory on the Amiga, for example `Games:MoM/`:
   - all `*.LBX` files (about 23 MB)
   - `CONFIG.MOM`
   - optionally your `SAVE1.GAM` to `SAVE9.GAM`. Saves are byte-compatible
     with the PC version in both directions.
4. Unpack the release archive into the same directory. It contains `remom`,
   `remom-prefs`, their icons, and `remom-music.exe` for the PC.
5. Start `remom` from its icon. To start it from a Shell, set the stack first
   with `Stack 1000000`.

File names need no conversion, because AmigaOS is case-insensitive. The game
runs without music. For music, see the next section.

### Music

The PC version synthesises its music from MIDI, which is too heavy for a
68020. The music is therefore converted once into IMA ADPCM files in a
`muzyka` directory next to `remom`, and the game streams them from disk.
Pick one of these ways:

1. **On the Amiga.** Run `remom-prefs`, choose *Music quality* (11 kHz or
   22 kHz) and press **Convert music**. The status line shows the progress.
   Converting all the music takes about 20 minutes at 11 kHz and about
   40 minutes at 22 kHz on a 68040-class machine, and longer on a 68030.
   You can stop it; pressing Convert music again continues where it stopped.
   From a Shell: `remom-prefs MUSICRATE=22kHz CONVERT`.
2. **On a PC, quick.** Run `remom-music.exe` (in the release) in the folder
   that contains `MUSIC.LBX`: `remom-music.exe 22` or `remom-music.exe 11`.
   It takes a few seconds. Copy the new `muzyka` folder next to `remom`.
3. **On a PC, best quality.** On Linux or WSL, the fluidsynth renderer uses a
   real General MIDI soundfont:

   ```sh
   sudo apt install gcc python3 libfluidsynth3 timgm6mb-soundfont
   sh build/build-host.sh
   RATE=22050 DANE=/path/to/your/LBX/files sh build/muzyka-host.sh
   ```

Options 1 and 2 use the same small built-in synthesiser, so the music
sounds like a simple tracker module rather than a soundfont. 22 kHz sounds
clearer, takes twice the disk space (about 75 MB instead of 37 MB) and
needs a little more CPU in the game.

## Options

`remom-prefs` is a small Workbench program for the settings: graphics (AGA or
RTG), video mode (Auto, PAL or NTSC), screen title bar, FPS on the bar, system
pointer, music and music quality. It also converts the music. The other
options are also in the game's main menu under **Amiga Options**. Settings are saved to `amiga.cfg`.

## Building from source

This port needs the bebbo amiga-gcc toolchain in `/opt/amiga`, on Linux or WSL.

```sh
# put the CyberGraphX developer headers in native/cgx-include/
# (clib/ cybergraphx/ inline/ libraries/ proto/ - they cannot be redistributed)
DEPLOY=/path/to/amiga/dir sh build/build.sh
```

The build downloads ReMoM at the pinned commit `a9cc082`. It applies
`build/remom-patch.py`, a set of mechanical patches for big-endian byte order,
AmigaOS and speed, and links the result with the Amiga platform layer in
`native/`. The platform layer comes from the author's OpenTTD and OpenXcom
Amiga ports.

Do not strip the binary. `m68k-amigaos-strip` produces an executable that
crashes the machine.

## License

- **ReMoM** belongs to its author, jbalcomb, and currently has no license.
  The author has allowed this port to use the code
  ([permission](docs/author-permission.png)). If you want to use ReMoM's
  code yourself, ask the author at <https://github.com/jbalcomb/ReMoM>.
- **The changes in this repository** (`native/`, `build/`) are licensed under
  the **GNU GPL v3**. ReMoM's author is considering the same license for
  ReMoM, and this choice keeps the port compatible with it.
- *Master of Magic* and all of its data are the property of their rights
  holders. None of the game's data is included here.

## Credits

- jbalcomb, for [ReMoM](https://github.com/jbalcomb/ReMoM)
- Simtex and MicroProse, for Master of Magic
