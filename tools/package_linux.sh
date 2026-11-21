#!/bin/bash
# Builds the distributable Linux x86_64 / SteamOS package: both engines (no game code) + the builder that
# turns the user's own ROM / disc into libswr_game.so / libswrdc_game.so. Nothing derived from the games.
#   tools/package_linux.sh [outdir]
# Build dependencies (Ubuntu 22.04, as in CI):
#   build-essential cmake ninja-build git libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev
#   libxss-dev libxinerama-dev libxfixes-dev libxkbcommon-dev libwayland-dev libegl-dev libdecor-0-dev
#   libpulse-dev libasound2-dev libpipewire-0.3-dev libudev-dev libgl-dev zlib1g-dev
# (only headers are used from the audio/video packages: SDL loads those libraries at run time)
# SWR_SKIP_DC=1 skips the Dreamcast engine; SWR_ALLOW_SILENT=1 accepts an SDL without audio backends (tests).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/dist}"
B="$ROOT/build-linux"
JOBS=$(nproc)
mkdir -p "$B" "$OUT"

"$ROOT/third_party/fetch_deps.sh"

# ---------------------------------------------------------------- SDL2, static
cmake -S "$ROOT/third_party/SDL2" -B "$B/sdl" -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_STATIC_PIC=ON -DSDL_TEST=OFF -DCMAKE_INSTALL_PREFIX="$B/sdl-inst"
ninja -C "$B/sdl" install >/dev/null
CFG="$B/sdl-inst/include/SDL2/SDL_config.h"
for d in VIDEO_DRIVER_X11 VIDEO_DRIVER_WAYLAND AUDIO_DRIVER_PIPEWIRE AUDIO_DRIVER_PULSEAUDIO AUDIO_DRIVER_ALSA; do
	if grep -q "#define SDL_$d 1" "$CFG"; then echo "  SDL: $d"
	elif [ -z "$SWR_ALLOW_SILENT" ]; then echo "ERROR: SDL built without $d (missing -dev package)"; exit 1
	else echo "  SDL: no $d (accepted: SWR_ALLOW_SILENT)"; fi
done

# ---------------------------------------------------------------- arcade engine
make -C "$ROOT/emu" -j"$JOBS" swracer SDL_STATIC="$B/sdl-inst"

# ---------------------------------------------------------------- Dreamcast engine
if [ -z "$SWR_SKIP_DC" ]; then
	"$ROOT/dreamcast/setup.sh"
	FC="$ROOT/dreamcast/flycast"
	cmake -S "$FC" -B "$B/dc" -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DSWRAOT_BLOCKS="$FC/core/hw/sh4/swraot/blk_stub.c" \
		-DUSE_BREAKPAD=OFF -DUSE_LUA=OFF -DUSE_HOST_SDL=OFF -DUSE_HOST_LIBZIP=OFF -DUSE_LIBAO=OFF \
		-DUSE_OPENMP=OFF -DSWR_NO_CURL=ON -DLIBUSB_ENABLE_UDEV=OFF \
		-DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
	ninja -C "$B/dc"
fi

# ---------------------------------------------------------------- portability check
# The binaries may only need libraries every desktop Linux has (SteamOS, Arch, Fedora, Debian...).
ALLOWED='^(linux-vdso|libc|libm|libdl|libpthread|librt|libgcc_s|libstdc\+\+|libz|libzstd|libGL|libOpenGL|libGLX|libEGL|libudev|libpulse|libasound|libpng16|ld-linux-x86-64)\.so'
check() {
	objdump -p "$1" | awk '/NEEDED/{print "  " "'"${1##*/}"'" " needs " $2}'
	local bad; bad=$(objdump -p "$1" | awk '/NEEDED/{print $2}' | grep -Ev "$ALLOWED" || true)
	[ -z "$bad" ] || { echo "ERROR: ${1##*/}: non-portable dependencies: $bad"; exit 1; }
	local g; g=$(objdump -T "$1" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
	echo "  ${1##*/}: highest glibc symbol $g"
	# glibc 2.35 (Ubuntu 22.04) baseline; building on a newer system raises it (SWR_ALLOW_NEW_GLIBC=1 for tests)
	if [ "$(printf '%s\n' GLIBC_2.35 "$g" | sort -V | tail -1)" != GLIBC_2.35 ] && [ -z "$SWR_ALLOW_NEW_GLIBC" ]; then
		echo "ERROR: ${1##*/} needs $g (build on Ubuntu 22.04)"; exit 1; fi
}
check "$ROOT/emu/swracer"
[ -n "$SWR_SKIP_DC" ] || check "$B/dc/flycast"

# ---------------------------------------------------------------- package
PKG="$OUT/StarWarsRacer-Recomp"
rm -rf "$PKG"
mkdir -p "$PKG/builder/sdk" "$PKG/roms" "$PKG/dreamcast"
cp "$ROOT/emu/swracer" "$PKG/"; strip "$PKG/swracer"
if [ -z "$SWR_SKIP_DC" ]; then
	cp "$B/dc/flycast" "$PKG/swracer-dc"; strip "$PKG/swracer-dc"
	# emu.cfg next to the program = portable mode: settings, saves and data stay in this folder
	tr -d '\r' < "$ROOT/package/windows/emu.cfg" > "$PKG/emu.cfg"
fi
cp "$ROOT/package/linux/build_game.sh" "$ROOT/package/linux/StarWarsRacer.sh" "$ROOT/package/linux/StarWarsRacer-DC.sh" "$PKG/"
cp "$ROOT/package/linux/LEEME-linux.txt" "$ROOT/package/linux/README-linux.txt" "$PKG/"
cp "$ROOT/builder/seeds_hikaru.txt" "$ROOT/builder/seeds_dreamcast.txt" "$PKG/builder/"
cp "$ROOT"/builder/sdk/* "$PKG/builder/sdk/"
cp "$ROOT/LICENSE" "$PKG/LICENSE.txt"
cp "$ROOT/icons/arcade.png" "$ROOT/icons/dreamcast.png" "$PKG/" 2>/dev/null || true
echo "Copy podrace.zip (or swracer.zip) here / Copia aquí podrace.zip (o swracer.zip)" > "$PKG/roms/PUT_podrace.zip_HERE.txt"
echo "Copy the Dreamcast image (.chd / .gdi) here / Copia aquí la imagen de Dreamcast (.chd / .gdi)" > "$PKG/dreamcast/PUT_THE_IMAGE_HERE.txt"
chmod +x "$PKG"/*.sh "$PKG/swracer" "$PKG/swracer-dc" 2>/dev/null || true
(cd "$OUT" && rm -f StarWarsRacer-Recomp-linux-x64.tar.gz && tar -czf StarWarsRacer-Recomp-linux-x64.tar.gz StarWarsRacer-Recomp)
echo "Package: $OUT/StarWarsRacer-Recomp-linux-x64.tar.gz"
