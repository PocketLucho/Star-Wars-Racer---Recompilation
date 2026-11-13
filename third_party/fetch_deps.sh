#!/bin/sh
# Fetches the header-only dependencies that are not vendored (pinned versions).
#   Vulkan-Headers (Apache-2.0 / MIT)  - needed by every build
#   SDL2 headers   (zlib licence)      - needed by the Linux / Steam Deck frontend (links the system libSDL2)
set -e
cd "$(dirname "$0")"
[ -d Vulkan-Headers ] || git clone --depth 1 --branch v1.4.367 https://github.com/KhronosGroup/Vulkan-Headers.git
[ -d SDL2 ] || git clone --depth 1 --branch release-2.30.0 https://github.com/libsdl-org/SDL.git SDL2
echo "ok"
