#!/usr/bin/env bash
# build.sh - cross compile VintageBlock.exe for Windows from Linux/macOS.
#
#   Linux/macOS  : needs zig (0.11+).      pip install ziglang   (or use a zig binary)
#   Windows      : see build.bat
#
# Output: dist/VintageBlock.exe  (x86_64, no runtime dependencies)
set -e
cd "$(dirname "$0")"

SRC="src/vb_math.cpp src/vb_blocks.cpp src/vb_world.cpp src/vb_worldgen.cpp \
     src/vb_mesh.cpp src/vb_game.cpp src/vb_font.cpp src/vb_render.cpp \
     src/vb_ui.cpp src/vb_main.cpp src/app.rc"

CXX=${CXX:-zig c++}
TARGET=${TARGET:-x86_64-windows-gnu}
OPTS="-target $TARGET -O2 -std=c++17 -Wl,--subsystem,windows"
LIBS="-lopengl32 -lgdi32 -luser32"

mkdir -p dist
echo "==> compiling for $TARGET"
$CXX $OPTS $SRC -o dist/VintageBlock.exe $LIBS
echo "==> dist/VintageBlock.exe ($(du -h dist/VintageBlock.exe | cut -f1))"

# ---- optional: host-native tools (soft renderers, no GPU needed)
if command -v g++ >/dev/null 2>&1; then
    echo "==> building host tools"
    mkdir -p out
    g++ -std=c++17 -O2 -I. -o out/preview tools/preview.cpp \
        src/vb_world.cpp src/vb_worldgen.cpp src/vb_mesh.cpp src/vb_blocks.cpp src/vb_math.cpp
    g++ -std=c++17 -O2 -I. -o out/atlas_dump tools/atlas_dump.cpp src/vb_blocks.cpp src/vb_math.cpp
    g++ -std=c++17 -O2 -I. -Itools -Itools/shim -o out/uishot tools/uishot.cpp \
        src/vb_ui.cpp src/vb_game.cpp src/vb_world.cpp src/vb_worldgen.cpp src/vb_mesh.cpp \
        src/vb_blocks.cpp src/vb_math.cpp src/vb_font.cpp
    g++ -std=c++17 -O2 -I. -o out/sim tools/sim.cpp src/vb_world.cpp src/vb_worldgen.cpp \
        src/vb_mesh.cpp src/vb_blocks.cpp src/vb_math.cpp src/vb_game.cpp src/vb_font.cpp
    echo "==> tools in out/"
fi
