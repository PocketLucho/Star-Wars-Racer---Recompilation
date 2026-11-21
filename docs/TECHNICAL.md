# Technical notes

## Layout

| Folder | What |
|---|---|
| `emu/` | The Hikaru machine and the SH-4 core (C). Frontends: `main_win32.c` (Windows), `main_sdl.c` (Linux / Steam Deck), `main_cli.c` (headless tests), `launcher_win32.c`. |
| `tools/` | `blockgen.c` (block generator; `blockgen.py` is the reference it was ported from and produces identical output), generators of the opcode tables, analysis scripts. |
| `builder/` | What the user runs: `build_game.ps1`, makefiles, `sdk/` (the headers and generator the module is compiled with) and `seeds_*.txt` (entry addresses only). |
| `package/` | What sits at the root of a released package: `windows/` (bat, LEEME, launcher.ini, emu.cfg), `linux/` (`build_game.sh`, `StarWarsRacer.sh`, `StarWarsRacer-DC.sh`, LEEME/README). |
| `tools/package_*.sh` | Build the release packages (also used by CI, `.github/workflows`). |
| `dreamcast/` | Flycast patch (`flycast-swraot.patch`, pinned commit in `setup.sh`), the executor (`swraot/`), `chd2gd.c`. |
| `icons/` | Original icons and the script that draws them. |

## Block recompilation

* `blockgen` decodes basic blocks from a RAM image starting at the seed addresses and follows static
  branches. Each block becomes `B_<pc>_<fnv1a>(Cpu*)`, built from the same `SEM_OP_*` macros the interpreter
  uses (`sh4sem.h`), so both always agree.
* At run time `blockrt.c` looks the PC up in the table and compares the FNV-1a hash of the code in RAM with
  the one the block was generated from. A mismatch (self-modified or reloaded code) or a missing block falls
  back to the interpreter for that block. Timing is counted in cycles identically on both paths.
* Idle loops (vblank waits, the slave mailbox) are skipped only at the loop head while the condition holds.

## Game module

The engines link `blk_stub.c` (an empty table). At start they load `swr_game.dll` / `libswr_game.so`
(`swrdc_game.*` for Dreamcast) from their own folder (`blk_module.c`): the module exports
`swr_module_init(const SwrHost*)` and returns `SwrModule{abi, cpu_size, machine, table, count}`. The blocks
reach memory through the function pointers in `SwrHost` (`swrmod.h`). ABI number, `sizeof(Cpu)` and machine
name are checked; anything else means "no module" and the interpreter is used.

The builder runs the engine with `SWR_BUILDER_DUMP=<dir>` (arcade: 3000 frames headless on the interpreter,
then `ram_master.bin` / `ram_slave.bin`) or `SWRAOT_BUILDER_DUMP=<dir>` (Dreamcast: `ram_dc.bin` when the
game program first runs at 8C010000), feeds the images and the seeds to `blockgen`, compiles the result and
deletes every intermediate file.

To extend coverage: play with `SWR_LOG`/`-misses` (arcade) or look at `swraot_misses.txt` (Dreamcast) and add
the addresses (not the bytes) to the seed files.

## Hikaru

* Two SH-4 (master/slave) with their on-chip peripherals; the comm (link) board is absent.
* MIE: Z80 running the board's own program from the ROM set, JVS, two 93C46 EEPROMs. `swracer.nv` holds the
  SRAM and both EEPROMs. Region is byte 5 of the MIE EEPROM.
* Sound: two AICA, each with an ARM7 running Sega's driver. AICA 1 effects, AICA 2 music.
* Command processor (`hikaru_cp.c`): viewports, model matrices, materials, texheads, lightsets, meshes.
  `hikaru_gfx.c` turns its output into clip-space vertices with per-vertex lighting and fog.
* Vulkan (`vkrender.c`, through volk): the TEXRAM is bound as it is (R16_UINT) and decoded, wrapped,
  mip-mapped and filtered in the fragment shader; reversed depth; the 2D layers of the Eurasia chip are
  composed over or under the 3D by coverage.
* Framebuffer blender (command 181: bit 24 enable, bits 16-23 rate): the 3D picture of the frame before is
  mixed over the new one before the 2D layers, `new = kept*k + drawn*(1-k)`. The game uses it while boosting.

## Dreamcast

`swraot_executor.cpp` implements Flycast's `Sh4Executor` with our core: registers are mirrored once per time
slice, exceptions and interrupts stay Flycast's. Widescreen: a hook at the function that builds the camera
(8C0872B4, PAL) widens the field of view and the aspect for its duration.

## Portable Linux / SteamOS package

* `swracer` links SDL2 statically (built from `third_party/SDL2`); SDL loads X11/Wayland, PipeWire/PulseAudio/ALSA
  at run time. `swracer-dc` is Flycast built with bundled SDL and libzip, no libcurl (`SWR_NO_CURL`), no libao or
  OpenMP, static libstdc++/libgcc. `tools/package_linux.sh` refuses binaries that need any library outside a
  short allow-list (libc, libm, libz, libzstd, libpng16, GL/EGL, udev, pulse, asound...). Built on Ubuntu 22.04
  (glibc 2.35 baseline).
* With an `emu.cfg` next to `swracer-dc`, Flycast keeps its configuration, saves and data in the program's folder
  (portable mode, as on Windows) and finds the disc in `dreamcast/`.
* `build_game.sh` needs no `make`: it compiles with `xargs -P`. Without a working C compiler it downloads Zig 0.13.0
  (pinned SHA-256) into `builder/tools` and builds the module for glibc 2.28.
* `StarWarsRacer.sh` / `StarWarsRacer-DC.sh` are the entries for Steam (non-Steam game): they run the builder in a
  terminal if the module is missing, log to `swracer.log` / `swracer-dc.log` and show errors in a window
  (kdialog / zenity). In SteamOS (`SteamDeck` / `SteamGamepadUI` set) the arcade game starts fullscreen in 16:9.
* CI (`steamos` job): an Arch Linux container with only runtime libraries checks that both engines load and that
  the builder bootstraps Zig and builds a module (`tools/make_test_gen.sh`, synthetic blocks).
