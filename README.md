# MPUmid

MIDI player for DOS. It sends the music to a MIDI module (Roland SC-55 or any GM/GS module) using an MPU-401 interface in UART mode.

It runs on a 286 or better. The screen shows a Sound Canvas front panel in text mode, with a level meter for the 16 MIDI channels, the song length and the time played.

## Requirements

- 286 or better
- VGA for the graphic interface (command line mode works with any video card)
- MPU-401 interface in UART mode (Sound Blaster 16/AWE, Roland, etc.)
- GM or GS MIDI module (SC-55, SC-88, etc.)

## How to use

Without parameters it opens the file browser:

    MPUMID

With a file it only plays that file. It does not change the video mode and it only prints the name and copyright stored inside the MIDI file:

    MPUMID SONG.MID

Supported files: .MID, .RMI, .KAR

## Keys

| Key | Action |
|---|---|
| Up / Down / PgUp / PgDn / Home / End | move in the list |
| Enter | play file / enter directory |
| Space | play all files in the directory |
| Right / Left | next / previous song while playing a directory |
| Backspace | go to the upper directory |
| I | info |
| Esc | stop the music / exit |

You can keep moving in the browser while the music plays.

## MPUMID.INI

The program looks for MPUMID.INI in the same directory as the .EXE.

    PATH=C:\MIDI
    SC55COLOR=80,79,75
    LCDCOLOR=255,197,12
    LEDCOLOR=0,255,0
    EFFECT=STARS
    MPUPORT=330

- PATH: start directory for the browser. If it does not exist (for example a CD-ROM drive with no disc) the browser starts in the directory of the .EXE.
- SC55COLOR, LCDCOLOR, LEDCOLOR: colors of the panel, the LCD and the power LED. Use R,G,B (0 to 255) or a palette number (0 to 15).
- EFFECT: background effect. NONE, STARS or SNOW.
- MPUPORT: MPU-401 port in hex. If it is not set, the program uses the P value of the BLASTER variable, or 330.

## Long names

DOS only has 8.3 names, so MPUmid reads long names from two text files in each directory:

- DIR.LST: names for the directories inside
- FILES.LST: names for the MIDI files inside

Format, one line for each name:

    SONIC3=Sonic 3 and Knuckles
    TRK01.MID=Angel Island Zone

UTF-8 and Latin-1 names are converted to the DOS character set.

For loose MIDI files (in the root or in a directory that also has subdirectories) a name like "Game - Song" is split: the game goes to the top of the panel and the song to the bottom.


## Build

OpenWatcom 1.9, medium memory model. Run BUILD.BAT.

## Notes

- The interface texts are in Spanish.
- The MIDI file is read from disk while it plays, so big files start at once and no XMS/EMS is needed.
- Up to 1000 files per directory.

TheElf
