STAR WARS RACER - RECOMP (Windows x64 + Vulkan)
===============================================
Made by Pocket_lucho with Claude.  https://buymeacoffee.com/pocket_lucho
Source code and licence (GPL v2): https://github.com/PocketLucho/Star-Wars-Racer---Recompilation

This package contains NO code, graphics, sound or BIOS of the games.

Getting started
---------------
1. Copy your MAME romset (podrace.zip or swracer.zip, with the BIOS epr-23400a.ic94 inside) to the
   "roms" folder and/or your PAL Dreamcast image (.chd or .gdi) to the "dreamcast" folder.
2. Run BUILD_GAME.bat once. It downloads the llvm-mingw compiler (180 MB, first time only), reads
   the game program from your ROM, translates it to C and compiles it into swr_game.dll (arcade)
   and swrdc_game.dll (Dreamcast). It takes a few minutes and needs internet the first time.
3. Run SWRacer-Launcher.exe and pick a game.
Without step 2 the games run on the interpreter (slower; the window title says so).
The generated .dll files come from YOUR copy of the game: personal use only, do not distribute them.

Arcade: default keys (change them with F10)
-------------------------------------------
  5 coin   1 start   Space boost / confirm
  Up/Down  both levers     A/Z left lever     K/M right lever
  Left/Right  steer; in the game menus they change the option
  F2 test (enters the service menu and confirms)   F1 service (moves the cursor)
  P pause   F10 OPTIONS   F11 fullscreen   F12 screenshot   Esc quit
XInput gamepad: "Sticks" mode (one lever per stick) or "Triggers" (LT/RT levers, stick steers).
  A/B boost, Start start, Back coin, LB/RB select.

Options (F10): language (automatic / espanol / English), internal resolution 1x-8x, MSAA,
16:9 widescreen, fullscreen, region (Japan, USA, Export/Europe, Australia, Korea), cheats (unlock
Ben Quadinaros and Gasgano: on SELECT PODRACER press "previous" from Anakin), lever speed, gamepad
mode and keys.
Attract sound: F2 > SYSTEM ASSIGNMENTS > ADVERTISE SOUND = ON.

Dreamcast
---------
Tab opens the Flycast menu (controls, video, cheats, language, saves...).
16:9 widescreen is on by default (Settings > Video > "Widescreen Game Cheats").
