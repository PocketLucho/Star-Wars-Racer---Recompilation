STAR WARS RACER - RECOMP for Linux x86_64 and SteamOS  (Made by Pocket_lucho with Claude)
=========================================================================================

If you want to support me: https://buymeacoffee.com/pocket_lucho

Two games in one folder:
  - Star Wars Racer Arcade (Sega Hikaru arcade)        -> StarWarsRacer.sh
  - Star Wars Episode I Racer (Dreamcast, PAL)          -> StarWarsRacer-DC.sh

REQUIREMENTS: nothing to install. Works on SteamOS (Steam Deck, Legion Go S...), Ubuntu 22.04 or
later, Fedora, Arch, etc. A Vulkan driver is needed (SteamOS has one). If the system has no C
compiler, build_game.sh downloads a portable one (Zig, ~45 MB) the first time, inside this
folder. No sudo needed. An internet connection is only needed that first time.

INSTALLATION
1. Copy your MAME romset podrace.zip (or swracer.zip, with the BIOS epr-23400a.ic94 inside) to the
   "roms" folder and/or your PAL Dreamcast image (.chd or .gdi) to the "dreamcast" folder.
2. Open a terminal in this folder and run ONCE:   ./build_game.sh
   It reads the program from YOUR files, translates it to C and compiles it into libswr_game.so
   (arcade) and libswrdc_game.so (Dreamcast). It takes a few minutes. For the Dreamcast game a
   window opens for a few seconds: that is normal.
3. To play:   ./StarWarsRacer.sh   or   ./StarWarsRacer-DC.sh
   (if something fails, the reason is shown in a window and kept in swracer.log / swracer-dc.log)
   Without step 2 the games still run, but slower (on the interpreter).

STEAMOS (Steam Deck / Legion Go S)
1. Switch to Desktop Mode (Steam button > Power > Switch to Desktop).
2. Extract the .tar.gz (for example to ~/Games/StarWarsRacer-Recomp) and copy the ROM and/or the
   Dreamcast image.
3. In the file manager (Dolphin), inside the folder: right click > "Open Terminal Here" and type:
       chmod +x *.sh swracer swracer-dc
       ./build_game.sh
4. Try the games with ./StarWarsRacer.sh and ./StarWarsRacer-DC.sh
5. To play in Game Mode: open Steam on the desktop > "Add a Game" > "Add a Non-Steam Game" >
   Browse > pick StarWarsRacer.sh (set the filter to "All Files"). Repeat with
   StarWarsRacer-DC.sh. Back in Game Mode they are in your library. arcade.png and
   dreamcast.png in this folder can be used as artwork.

CONTROLS
  Arcade: the gamepad works directly. "Sticks" mode (default): each stick up moves one lever;
    or "Triggers" mode (LT/RT levers, left stick steers). A/B boost and confirm, Start,
    Select = coin, LB/RB = select buttons. L3+R3 (or F10) opens the options: language,
    resolution, MSAA, widescreen, region, cheats (Ben Quadinaros and Gasgano), gamepad and
    keys. On SteamOS it starts fullscreen and in widescreen.
    Keyboard: 5 coin, 1 start, Space boost, arrows levers/steer, F2 test, F1 service, P pause,
    F11 fullscreen, F12 screenshot, Esc quit.
  Dreamcast: gamepad works directly. The Flycast menu (controls, video, cheats, saves) opens
    with Tab or with the button assigned in its control options. Widescreen is on.
    Settings and saves stay in this folder (emu.cfg and "data").

This package contains nothing from the games. libswr_game.so and libswrdc_game.so are made from
your files: do not share them. GPL v2 licence (the Dreamcast engine is based on Flycast).
Source code: https://github.com/PocketLucho/Star-Wars-Racer---Recompilation
