#!/bin/bash
# Cross-compile the Windows engine (llvm-mingw; TC = toolchain folder). It contains no game code:
# the recompiled blocks come from swr_game.dll, built on the user's PC by the builder.
set -e
TC=${TC:?set TC to the llvm-mingw folder}
CC="$TC/bin/x86_64-w64-mingw32-clang"
OUT=${OUT:-win64}
mkdir -p $OUT/obj
# headers are not tracked per object: if any header (or this script) is newer than the stamp, rebuild everything
STAMP=$OUT/obj/.stamp
if [ ! -f $STAMP ] || [ -n "$(find . ../third_party/volk ../third_party/miniz -name '*.h' -newer $STAMP -print -quit)" ]; then
  echo "headers changed: clean rebuild"; rm -f $OUT/obj/*.o
fi
[ build_win.sh -nt $STAMP ] && rm -f $OUT/obj/*.o
CFLAGS="-O2 -I. -I../third_party/miniz -I../third_party/volk -I../third_party/Vulkan-Headers/include -DVK_NO_PROTOTYPES -DVK_USE_PLATFORM_WIN32_KHR -w"
SRC="sh4interp.c hikaru.c x76f100.c gpu.c mie.c z80.c crypt5881.c blockrt.c blk_module.c blk_stub.c cheats.c hikaru_cp.c hikaru_gfx.c swrender.c romzip.c arm7.c aica.c emu_core.c vkrender.c gfx_vk.c settings_win32.c main_win32.c ../third_party/volk/volk.c ../third_party/miniz/miniz.c"
OBJS=""
for f in $SRC; do
  o=$OUT/obj/$(echo $f | tr '/.' '__').o
  OBJS="$OBJS $o"
  if [ ! -f $o ] || [ $f -nt $o ]; then echo "cc $f"; $CC $CFLAGS -c $f -o $o & fi
  while [ $(jobs -r | wc -l) -ge ${JOBS:-2} ]; do sleep 0.2; done
done
wait
cp ../icons/arcade.ico ../icons/launcher.ico .
$TC/bin/x86_64-w64-mingw32-windres arcade.rc -O coff -o $OUT/arcade.res
$TC/bin/x86_64-w64-mingw32-windres launcher.rc -O coff -o $OUT/launcher.res
rm -f arcade.ico launcher.ico
$CC -o $OUT/StarWarsRacer-Recomp.exe $OBJS $OUT/arcade.res -mwindows -lwinmm -static
touch $STAMP
ls -la $OUT/StarWarsRacer-Recomp.exe
$CC -O2 -w -o $OUT/SWRacer-Launcher.exe launcher_win32.c $OUT/launcher.res -mwindows -municode -static
