#!/bin/bash
# STAR WARS RACER - RECOMP : builder (Linux)
# Generates the recompiled game code from YOUR ROM and compiles it into libswr_game.so (arcade) and, if a
# Dreamcast image is present, libswrdc_game.so. Needs / necesita: cc and make (sudo apt install build-essential)
# Nothing derived from the game is distributed. / No se distribuye nada derivado del juego.
set -e
cd "$(dirname "$(readlink -f "$0")")"
say() { printf '\n\033[36m== %s\033[0m\n   %s\n' "$1" "$2"; }
fail() { printf '\n\033[31mERROR: %s\n       %s\033[0m\n' "$1" "$2"; rm -rf builder/gen builder/obj builder/work; exit 1; }
say "STAR WARS RACER - RECOMP: construcción del código del juego" "STAR WARS RACER - RECOMP: building the game code"
command -v "${CC:-cc}" >/dev/null || fail "Falta el compilador: sudo apt install build-essential" "Compiler missing: sudo apt install build-essential"
command -v make >/dev/null || fail "Falta make: sudo apt install build-essential" "make missing: sudo apt install build-essential"
ROM=""; for f in roms/podrace.zip roms/swracer.zip podrace.zip swracer.zip; do [ -z "$ROM" ] && [ -f "$f" ] && ROM="$f"; done
DC=""; for f in dreamcast/*.chd dreamcast/*.gdi dreamcast/*.cdi ./*.chd ./*.gdi; do [ -z "$DC" ] && [ -f "$f" ] && DC="$f"; done
[ -n "$ROM" ] || [ -n "$DC" ] || fail "No hay nada que construir: copia podrace.zip en 'roms' y/o tu imagen de Dreamcast en 'dreamcast'." "Nothing to build: copy podrace.zip to 'roms' and/or your Dreamcast image to 'dreamcast'."
mkdir -p builder/tools && "${CC:-cc}" -O2 -o builder/tools/blockgen builder/sdk/blockgen.c
build() {  # name exe envvar machine out seeds image-specs...
    local name="$1" exe="$2" envn="$3" machine="$4" out="$5" seeds="$6"; shift 6
    rm -rf builder/gen builder/obj builder/work; mkdir -p builder/gen builder/obj builder/work
    say "$name: arrancando el juego para leer su programa..." "$name: booting the game to read its program..."
    env "$envn=builder/work" "./$exe" >/dev/null 2>&1 || true
    local args=(); for s in "$@"; do f="builder/work/${s#*:}"; f="${f%:*}"; [ -f "$f" ] || fail "$name: no se pudo leer el programa del juego." "$name: could not read the game program."; args+=(--image "${s%%:*}:$f:${s##*:}"); done
    say "$name: generando el código C..." "$name: generating the C code..."
    builder/tools/blockgen -o builder/gen -n 16 "${args[@]}" --seeds "builder/$seeds" || fail "$name: la generación ha fallado." "$name: generation failed."
    say "$name: compilando ($(nproc) núcleos)..." "$name: compiling ($(nproc) cores)..."
    make -C builder -f Makefile.linux -j"$(nproc)" --no-print-directory MACHINE="$machine" OUT="../$out" || fail "$name: error de compilación." "$name: compilation error."
    rm -rf builder/gen builder/obj builder/work   # generated sources and RAM images contain code derived from the game
}
[ -n "$ROM" ] && [ -x ./swracer ] && build "Recreativa / Arcade" swracer SWR_BUILDER_DUMP hikaru libswr_game.so seeds_hikaru.txt 0:ram_master.bin:0c000000 1:ram_slave.bin:0c000000
[ -n "$DC" ] && [ -x ./swracer-dc ] && build "Dreamcast" swracer-dc SWRAOT_BUILDER_DUMP dreamcast libswrdc_game.so seeds_dreamcast.txt 0:ram_dc.bin:0c000000
say "¡Listo! Ya puedes jugar." "Done! You can play now."
