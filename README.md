# STAR WARS RACER - RECOMP

Static recompilation of **Star Wars Racer Arcade** (Sega Hikaru, 2000) and **Star Wars Episode I Racer**
(Dreamcast, PAL) for Windows and Linux / Steam Deck.
Made by **Pocket_lucho** with Claude. [Leer en español](README.es.md)

The SH-4 machine code of each game is translated ahead of time to C (one function per basic block, checked
by hash at run time) and compiled to native code; whatever was not translated runs on a cycle-exact
interpreter, so the whole game always works.

* **Arcade**: everything else is an original implementation of the Hikaru board written for this project:
  both SH-4s, the MIE I/O board (Z80 + JVS), two AICA sound boards (ARM7), the command processor and GPU on
  Vulkan, the 2D layers and the EEPROMs.
* **Dreamcast**: the same SH-4 core plugged into [Flycast](https://github.com/flyinghead/flycast) as its CPU
  executor; Flycast provides the Dreamcast hardware (PowerVR2, GD-ROM, AICA, controllers, HLE BIOS).

If you want to support me: https://buymeacoffee.com/pocket_lucho

## What you need

* Your own MAME romset of the arcade game: `podrace.zip` or `swracer.zip` (with the Hikaru BIOS
  `epr-23400a.ic94` inside), and/or
* your own image of the Dreamcast game, PAL version T-23001D-50 (`.chd` or `.gdi`).
* Windows 10/11 64-bit, or Linux x86_64 (glibc 2.35+: SteamOS 3, Ubuntu 22.04 or later, Fedora, Arch...), with a
  Vulkan 1.1 driver.

**Neither this repository nor the released packages contain any code, graphics, sound or BIOS of the games.**
The recompiled code is generated on your PC from your own files.

## Getting started (Windows)

1. Unzip the package.
2. Copy `podrace.zip` (or `swracer.zip`) to `roms\` and/or the Dreamcast image to `dreamcast\`.
3. Run **`BUILD_GAME.bat`** once. It
   * downloads the free [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) compiler (180 MB, first time
     only, checked with SHA-256),
   * boots the game without window on the interpreter to read its program from memory and translates it to C
     (about 27,000 blocks for the arcade game, 38,000 for the Dreamcast one),
   * compiles it into `swr_game.dll` / `swrdc_game.dll` (a few minutes),
   * deletes the generated sources.
4. Run **`SWRacer-Launcher.exe`** and pick a game (or start `StarWarsRacer-Recomp.exe` /
   `SWRacer-DC-Recomp.exe` directly). F10 opens the options of the arcade game, Tab those of the Dreamcast one.

Without step 3 the games still run, on the interpreter (slower; the window title says so).

## Getting started (Linux x86_64 / SteamOS)

Download `StarWarsRacer-Recomp-linux-x64.tar.gz`, extract it, copy the ROM and/or the disc image as above, run
`./build_game.sh` once and play with `./StarWarsRacer.sh` (arcade) or `./StarWarsRacer-DC.sh` (Dreamcast).
Nothing has to be installed: SDL is built into the programs, and if there is no C compiler (SteamOS) the builder
downloads a portable one (Zig) into its own folder. On SteamOS do this in Desktop Mode and then add both `.sh`
files to Steam as non-Steam games. More details in `README-linux.txt` inside the package.

## Features

* Native speed (recompiled code, no JIT) with an exact interpreter fallback.
* Arcade: internal resolution 1x-8x, MSAA, **16:9 widescreen** with the real wider view, fullscreen, pause,
  screenshots, keyboard and XInput/SDL gamepad with analog levers, service/test menu with persistent settings.
* **Region selector** (Japan, USA, Export/Europe, Australia, Korea).
* **Cheats**: unlock the hidden pilots Ben Quadinaros and Gasgano (the cabinet unlocks them after 50 Expert
  finishes / 700 plays). Dreamcast: Flycast's cheat menu.
* Menus in **Spanish and English** (automatic, or chosen in the options).
* Boost motion blur (the Hikaru framebuffer blender), lens flares, dust, fog, per-vertex lighting.
* Dreamcast: widescreen through a camera hook (wider field of view, not a stretch), everything Flycast offers.

## Building from source

    tools/package_linux.sh                               # Linux / SteamOS package (both engines + builder)
    LLVM_MINGW=/path/to/llvm-mingw tools/package_windows.sh   # Windows package (cross-compiled)

or step by step:

    third_party/fetch_deps.sh                 # Vulkan-Headers and SDL2 sources (pinned)
    cd emu
    make swracer                              # Linux frontend (system libSDL2; SDL_STATIC=<prefix> for static)
    make hikaru_run                           # headless test program
    TC=/path/to/llvm-mingw ./build_win.sh     # Windows engine and launcher (cross-compiled)
    ../dreamcast/setup.sh                     # Dreamcast engine: Flycast at a pinned commit + our core

The packages are built by GitHub Actions on every push (`.github/workflows/build.yml`), including a check in an
Arch Linux container without compiler or development packages, like SteamOS; tags `v*` publish a release.
The engines contain no game code. The builder (`builder/`, plus `package/` scripts) makes the game module
from the user's ROM. See [docs/TECHNICAL.md](docs/TECHNICAL.md); development history in
[docs/STATUS.es.md](docs/STATUS.es.md) (Spanish).

## Legal

* Licensed under the **GNU GPL v2** (see `LICENSE`); the Dreamcast engine is a patch for Flycast (GPL v2).
  `third_party/miniz` (MIT) and `third_party/volk` (MIT) keep their own licences.
* **No material copyrighted by Sega or Lucasfilm is included**: no ROM, BIOS, disc image, graphics, sound,
  music or translated game code. Graphics, sound and music are always read at run time from the user's ROM;
  the recompiled code (`swr_game.dll`, `swrdc_game.dll`, `libswr_game.so`, `libswrdc_game.so`) is generated on
  the user's PC and **must not be redistributed**.
* `builder/seeds_*.txt` only contain addresses of code entry points.
* The icons are original drawings; no official logos or characters are used.
* "Star Wars", "Star Wars Racer Arcade", "Episode I Racer", "SEGA", "Hikaru" and "Dreamcast" are trademarks
  of their owners and are only used to say what this software is compatible with. The project is not
  affiliated with or endorsed by them.
* This is not legal advice.
