#!/bin/sh
# Prepares the Dreamcast engine: Flycast at the pinned commit + our SH-4 core as its executor.
#   ./setup.sh            clone + patch into dreamcast/flycast
# Then:
#   Linux:    cmake -S flycast -B flycast/build -DCMAKE_BUILD_TYPE=Release -DLIBUSB_ENABLE_UDEV=OFF && cmake --build flycast/build -j
#   Windows (cross, llvm-mingw in $TC):
#             cmake -S flycast -B flycast/build-win -DCMAKE_TOOLCHAIN_FILE=$PWD/mingw.cmake -DCMAKE_BUILD_TYPE=Release -DUSE_OPENMP=OFF
#             cmake --build flycast/build-win -j
# The result (flycast / flycast.exe) is shipped as swracer-dc / SWRacer-DC-Recomp.exe. It contains no game code:
# the recompiled blocks are loaded from libswrdc_game.so / swrdc_game.dll, which the builder makes from the user's disc.
set -e
cd "$(dirname "$0")"
COMMIT=7adbf3caef251b153e348df3f469a0f35878339d
if [ ! -d flycast ]; then
    git clone https://github.com/flyinghead/flycast.git
    (cd flycast && git checkout $COMMIT && git submodule update --init --recursive)
fi
cd flycast
git apply --check ../flycast-swraot.patch && git apply ../flycast-swraot.patch
D=core/hw/sh4/swraot; mkdir -p $D
cp ../swraot/swraot_executor.cpp ../swraot/swraot_bus.c $D/
for f in sh4.h sh4exec.h sh4sem.h optable.h sh4interp.c blockrt.c blockrt.h swrmod.h blk_module.c blk_stub.c; do cp ../../emu/$f $D/; done
cp ../../icons/dreamcast.ico shell/windows/flycast.ico
echo "ok: see the header of this script for the cmake commands"
