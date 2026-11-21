#!/bin/sh
# Prepares the Dreamcast engine: Flycast at the pinned commit + our SH-4 core as its executor.
#   ./setup.sh            clone + patch into dreamcast/flycast
#   tools/package_linux.sh and tools/package_windows.sh build it with the right options (portable Linux build:
#   no libcurl/libao/OpenMP, static SDL/libzip/libstdc++; Windows: cross-compiled with llvm-mingw).
# The result (flycast / flycast.exe) is shipped as swracer-dc / SWRacer-DC-Recomp.exe. It contains no game code:
# the recompiled blocks are loaded from libswrdc_game.so / swrdc_game.dll, which the builder makes from the user's disc.
set -e
cd "$(dirname "$0")"
COMMIT=7adbf3caef251b153e348df3f469a0f35878339d
if [ ! -d flycast ]; then
    git clone https://github.com/flyinghead/flycast.git
    (cd flycast && git checkout -q $COMMIT)
fi
(
    cd flycast
    # Spout (texture sharing, Windows only) is not used and does not build with mingw: leave it out
    git config submodule.core/deps/Spout.update none
    [ -f core/deps/Spout/CMakeLists.txt ] && git submodule deinit -f -q core/deps/Spout
    git submodule update --init --recursive || {
        # gitlab.freedesktop.org unreachable: same pinned freetype commit from its GitHub mirror
        git -c url."https://github.com/freetype/freetype.git".insteadOf=https://gitlab.freedesktop.org/freetype/freetype.git \
            submodule update --init --recursive
    }
)
cd flycast
if git apply --reverse --check ../flycast-swraot.patch 2>/dev/null; then echo "patch already applied"
else git apply ../flycast-swraot.patch; fi
D=core/hw/sh4/swraot; mkdir -p $D
cp ../swraot/swraot_executor.cpp ../swraot/swraot_bus.c $D/
for f in sh4.h sh4exec.h sh4sem.h optable.h sh4interp.c blockrt.c blockrt.h swrmod.h blk_module.c blk_stub.c; do cp ../../emu/$f $D/; done
cp ../../icons/dreamcast.ico shell/windows/flycast.ico
echo "ok: see the header of this script for the cmake commands"
