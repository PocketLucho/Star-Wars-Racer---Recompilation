#!/bin/bash
# Builds the distributable Windows x64 package: both engines (no game code), the launcher and the builder
# (CONSTRUIR_JUEGO.bat / BUILD_GAME.bat) that turns the user's own ROM / disc into swr_game.dll / swrdc_game.dll.
#   LLVM_MINGW=/path/to/llvm-mingw tools/package_windows.sh [outdir]
# Cross-compiled from Linux. Needs: cmake ninja-build zip git, and llvm-mingw (ucrt, x86_64 host).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/dist}"
export TC="${LLVM_MINGW:?set LLVM_MINGW to the llvm-mingw folder}"
B="$ROOT/build-windows"
mkdir -p "$B" "$OUT"

"$ROOT/third_party/fetch_deps.sh"

# arcade engine + launcher
(cd "$ROOT/emu" && OUT="$B/arcade" ./build_win.sh)

# Dreamcast engine
"$ROOT/dreamcast/setup.sh"
FC="$ROOT/dreamcast/flycast"
cmake -S "$FC" -B "$B/dc" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$ROOT/dreamcast/mingw.cmake" \
	-DSWRAOT_BLOCKS="$FC/core/hw/sh4/swraot/blk_stub.c" \
	-DUSE_BREAKPAD=OFF -DUSE_LUA=OFF -DUSE_OPENMP=OFF -DUSE_HOST_SDL=OFF -DUSE_HOST_LIBZIP=OFF
ninja -C "$B/dc"

PKG="$OUT/StarWarsRacer-Recomp-win64"
rm -rf "$PKG"
mkdir -p "$PKG/builder/sdk" "$PKG/roms" "$PKG/dreamcast"
cp "$B/arcade/StarWarsRacer-Recomp.exe" "$B/arcade/SWRacer-Launcher.exe" "$PKG/"
cp "$B/dc/flycast.exe" "$PKG/SWRacer-DC-Recomp.exe"
cp "$TC/x86_64-w64-mingw32/bin/libwinpthread-1.dll" "$PKG/"
cp "$ROOT"/package/windows/* "$PKG/"
cp "$ROOT/builder/build_game.ps1" "$ROOT/builder/Makefile" "$ROOT/builder/seeds_hikaru.txt" "$ROOT/builder/seeds_dreamcast.txt" "$PKG/builder/"
cp "$ROOT"/builder/sdk/* "$PKG/builder/sdk/"
cp "$ROOT/LICENSE" "$PKG/LICENSE.txt"
echo "Copia aqui podrace.zip (o swracer.zip) / Copy podrace.zip (or swracer.zip) here" > "$PKG/roms/PON_AQUI_podrace.zip.txt"
echo "Copia aqui la imagen .chd / .gdi / Copy the .chd / .gdi image here" > "$PKG/dreamcast/PON_AQUI_LA_IMAGEN_CHD.txt"
for f in "$PKG"/*.txt "$PKG"/*.bat "$PKG"/*.ini "$PKG"/*.cfg "$PKG"/roms/*.txt "$PKG"/dreamcast/*.txt; do
	sed -i 's/\r*$/\r/' "$f"		# Windows line endings for Notepad and cmd
done
(cd "$OUT" && rm -f StarWarsRacer-Recomp-win64.zip && zip -qr StarWarsRacer-Recomp-win64.zip StarWarsRacer-Recomp-win64)
echo "Package: $OUT/StarWarsRacer-Recomp-win64.zip"
