#!/bin/bash
# STAR WARS RACER - RECOMP : builder (Linux / SteamOS)
# Generates the recompiled game code from YOUR files and compiles it:
#   roms/podrace.zip (or swracer.zip)      -> libswr_game.so    (arcade)
#   dreamcast/<image>.chd / .gdi / .cdi    -> libswrdc_game.so  (Dreamcast)
# Genera el código recompilado a partir de TUS archivos y lo compila (ver arriba).
#
# Nothing has to be installed: if the system has no C compiler (SteamOS, for example) a portable
# one (Zig, ~45 MB) is downloaded once into builder/tools. No root/sudo needed.
# No hace falta instalar nada: si el sistema no tiene compilador de C (por ejemplo SteamOS) se
# descarga una vez uno portátil (Zig, ~45 MB) en builder/tools. No hace falta root/sudo.
#
#   ./build_game.sh                 generate + compile (both games, whichever files are present)
#   ./build_game.sh --compile-only  compile the sources already in builder/gen (testing)
# Env: CC=<compiler> forces a compiler; SWR_USE_ZIG=1 forces the portable compiler;
#      SWR_ONLY=arcade|dreamcast builds only one of the games.
set -e
cd "$(dirname "$(readlink -f "$0")")"
say() { printf '\n\033[36m== %s\033[0m\n   %s\n' "$1" "$2"; }
fail() { printf '\n\033[31mERROR: %s\n       %s\033[0m\n' "$1" "$2"; rm -rf builder/gen builder/obj builder/work; pause_exit 1; }
pause_exit() {
	# When started from a file manager or the launcher there is no shell left to read the message: wait for a key.
	if [ -t 0 ] && [ -z "$SWR_NO_PAUSE" ]; then read -r -p "Pulsa Enter / Press Enter " _ || true; fi
	exit "$1"
}

ZIG_VER=0.13.0
ZIG_NAME="zig-linux-x86_64-$ZIG_VER"
ZIG_URL="https://ziglang.org/download/$ZIG_VER/$ZIG_NAME.tar.xz"
ZIG_SHA256=d45312e61ebcc48032b77bc4cf7fd6915c11fa16e4aad116b66c9468211230ea
ZIG_DIR="builder/tools/$ZIG_NAME"

COMPILE_ONLY=0
[ "$1" = "--compile-only" ] && COMPILE_ONLY=1

say "STAR WARS RACER - RECOMP: construcción del código del juego" "STAR WARS RACER - RECOMP: building the game code"
[ "$(uname -m)" = "x86_64" ] || fail "Sólo x86_64." "x86_64 only."

# ---------------------------------------------------------------- compiler
download() {	# url file
	if command -v curl >/dev/null; then curl -fL --retry 3 -o "$2" "$1"
	elif command -v wget >/dev/null; then wget -O "$2" "$1"
	else return 1; fi
}
get_zig() {
	if [ ! -x "$ZIG_DIR/zig" ]; then
		say "No hay compilador de C: descargando Zig $ZIG_VER (una sola vez, ~45 MB)..." \
			"No C compiler found: downloading Zig $ZIG_VER (only once, ~45 MB)..."
		mkdir -p builder/tools
		local t="builder/tools/$ZIG_NAME.tar.xz"
		download "$ZIG_URL" "$t" || fail "No se pudo descargar $ZIG_URL (¿hay conexión a internet?)" \
			"Could not download $ZIG_URL (internet connection?)"
		local h; h=$(sha256sum "$t" | cut -d' ' -f1)
		if [ "$h" != "$ZIG_SHA256" ]; then rm -f "$t"; fail "El archivo descargado no es válido (sha256 $h)." \
			"The downloaded file is not valid (sha256 $h)."; fi
		tar -xJf "$t" -C builder/tools || fail "No se pudo descomprimir Zig." "Could not extract Zig."
		rm -f "$t"
	fi
	export ZIG_GLOBAL_CACHE_DIR="$PWD/builder/tools/zig-cache" ZIG_LOCAL_CACHE_DIR="$PWD/builder/tools/zig-cache"
	# glibc 2.28 baseline: the module then loads on any distribution from 2018 onwards
	CCCMD=("$PWD/$ZIG_DIR/zig" cc -target x86_64-linux-gnu.2.28)
	LINKFLAGS=(-shared -Wl,-Bsymbolic -Wl,-z,defs -s)
}
works() {	# test that the compiler really builds a shared library
	local d; d=$(mktemp -d)
	printf 'int f(void){return 1;}\n' > "$d/t.c"
	"$@" -fPIC -shared -o "$d/t.so" "$d/t.c" >/dev/null 2>&1; local r=$?
	rm -rf "$d"; return $r
}
LINKFLAGS=(-shared -Wl,-Bsymbolic -Wl,--no-undefined -s)
if [ -n "$SWR_USE_ZIG" ]; then get_zig
elif [ -n "$CC" ]; then CCCMD=($CC)
elif command -v gcc >/dev/null && works gcc; then CCCMD=(gcc)
elif command -v clang >/dev/null && works clang; then CCCMD=(clang)
elif command -v cc >/dev/null && works cc; then CCCMD=(cc)
else get_zig
fi
works "${CCCMD[@]}" || fail "El compilador no funciona: ${CCCMD[*]}" "The compiler does not work: ${CCCMD[*]}"
JOBS=$(nproc 2>/dev/null || echo 4)
CFLAGS=(-O2 -fPIC -fvisibility=hidden -fno-strict-aliasing -fexceptions -w -DNDEBUG -Ibuilder/sdk)

compile_module() {	# out [extra flags for the module glue]
	local out="$1"; shift
	say "Compilando $out con ${CCCMD[0]##*/} ($JOBS núcleos, unos minutos)..." \
		"Compiling $out with ${CCCMD[0]##*/} ($JOBS cores, a few minutes)..."
	rm -rf builder/obj && mkdir -p builder/obj
	export SWR_CC_CMD
	SWR_CC_CMD=$(printf '%q ' "${CCCMD[@]}" "${CFLAGS[@]}")
	printf '%s\0' builder/gen/blk_*.c | xargs -0 -n1 -P"$JOBS" bash -c '
		o="builder/obj/$(basename "${1%.c}").o"
		eval "$SWR_CC_CMD -c \"\$1\" -o \"\$o\"" || { echo "FAILED: $1" >&2; exit 255; }
		echo "  ok  $(basename "$1")"' _ \
		|| fail "La compilación ha fallado (mira los mensajes de arriba)." "Compilation failed (see the messages above)."
	"${CCCMD[@]}" "${CFLAGS[@]}" "$@" -c builder/sdk/blk_module_game.c -o builder/obj/blk_module_game.o \
		|| fail "La compilación ha fallado." "Compilation failed."
	"${CCCMD[@]}" "${LINKFLAGS[@]}" -o "$out" builder/obj/*.o -lm || fail "El enlazado ha fallado." "Linking failed."
}

if [ $COMPILE_ONLY = 1 ]; then
	[ -f builder/gen/blk_table.c ] || fail "No hay código generado en builder/gen." "No generated code in builder/gen."
	compile_module libswr_game.so
	rm -rf builder/gen builder/obj
	say "¡Listo!" "Done!"; pause_exit 0
fi

# ---------------------------------------------------------------- find the games
ROM=""; for f in roms/podrace.zip roms/swracer.zip podrace.zip swracer.zip; do [ -z "$ROM" ] && [ -f "$f" ] && ROM="$f"; done
DC=""; for f in dreamcast/*.chd dreamcast/*.gdi dreamcast/*.cdi ./*.chd ./*.gdi ./*.cdi; do [ -z "$DC" ] && [ -f "$f" ] && DC="$f"; done
[ "$SWR_ONLY" = dreamcast ] && ROM=""
[ "$SWR_ONLY" = arcade ] && DC=""
[ -n "$ROM" ] || [ -n "$DC" ] || fail "No hay nada que construir: copia podrace.zip en 'roms' y/o tu imagen de Dreamcast en 'dreamcast'." \
	"Nothing to build: copy podrace.zip to 'roms' and/or your Dreamcast image to 'dreamcast'."
chmod +x swracer swracer-dc 2>/dev/null || true

say "Preparando el generador..." "Preparing the generator..."
mkdir -p builder/tools
"${CCCMD[@]}" -O2 -w -o builder/tools/blockgen builder/sdk/blockgen.c || fail "No se pudo compilar el generador." "Could not compile the generator."

generate() {	# name seeds images...   (images: cpu:file:hexbase, file inside builder/work)
	local name="$1" seeds="$2"; shift 2
	local args=() s f
	for s in "$@"; do
		f="builder/work/$(echo "$s" | cut -d: -f2)"
		[ -s "$f" ] || fail "$name: no se pudo leer el programa del juego (mira el mensaje de arriba)." \
			"$name: could not read the game program (see the message above)."
		args+=(--image "$(echo "$s" | cut -d: -f1):$f:$(echo "$s" | cut -d: -f3)")
	done
	say "$name: generando el código C..." "$name: generating the C code..."
	rm -rf builder/gen && mkdir -p builder/gen
	builder/tools/blockgen -o builder/gen -n "$JOBS" "${args[@]}" --seeds "builder/$seeds" \
		|| fail "$name: la generación ha fallado." "$name: generation failed."
}

if [ -n "$ROM" ]; then
	[ -x ./swracer ] || fail "Falta el programa swracer." "The swracer program is missing."
	rm -rf builder/work && mkdir -p builder/work
	say "Recreativa: arrancando el juego para leer su programa (sin ventana, ~1 minuto)..." \
		"Arcade: booting the game to read its program (no window, ~1 minute)..."
	SWR_BUILDER_DUMP="$PWD/builder/work" SDL_AUDIODRIVER=dummy ./swracer "$ROM" || true
	generate "Recreativa / Arcade" seeds_hikaru.txt 0:ram_master.bin:0c000000 1:ram_slave.bin:0c000000
	compile_module libswr_game.so
fi
if [ -n "$DC" ]; then
	[ -x ./swracer-dc ] || fail "Falta el programa swracer-dc." "The swracer-dc program is missing."
	rm -rf builder/work && mkdir -p builder/work
	say "Dreamcast: arrancando el juego para leer su programa (se abre una ventana unos segundos)..." \
		"Dreamcast: booting the game to read its program (a window opens for a few seconds)..."
	SWRAOT_BUILDER_DUMP="$PWD/builder/work" SDL_AUDIODRIVER=dummy ./swracer-dc "$DC" >/dev/null 2>&1 || true
	generate "Dreamcast" seeds_dreamcast.txt 0:ram_dc.bin:0c000000
	compile_module libswrdc_game.so -DSWR_DREAMCAST
fi
rm -rf builder/gen builder/obj builder/work	# generated sources and RAM images contain code derived from the game
say "¡Listo! Ya puedes jugar con ./StarWarsRacer.sh y ./StarWarsRacer-DC.sh" \
	"Done! You can play with ./StarWarsRacer.sh and ./StarWarsRacer-DC.sh"
pause_exit 0
