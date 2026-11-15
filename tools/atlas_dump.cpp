// tools/atlas_dump.cpp : renders the 8x8 texture atlas to a PNG for inspection
#include "../src/vb_blocks.h"
#include "../src/vb_png.h"

int main(int argc, char** argv) {
    initBlocks();
    initItems();
    const u32* atlas = getAtlasRGBA();
    int scale = 4;
    int outSize = ATLAS_SIZE * scale;
    std::vector<u32> img((size_t)outSize * outSize);
    for (int y = 0; y < outSize; y++)
        for (int x = 0; x < outSize; x++) {
            u32 c = atlas[(size_t)(y / scale) * ATLAS_SIZE + (x / scale)];
            u32 a = c >> 24;
            // composite over mid grey so transparency is visible
            int r = (int)((c >> 16 & 255) * a / 255 + 128 * (255 - a) / 255);
            int g = (int)((c >> 8 & 255) * a / 255 + 128 * (255 - a) / 255);
            int b = (int)((c & 255) * a / 255 + 128 * (255 - a) / 255);
            img[(size_t)y * outSize + x] = rgb(r, g, b);
        }
    // grid lines
    for (int i = 0; i <= ATLAS_GRID; i++) {
        for (int s = 0; s < outSize; s++) {
            int p = i * ATLAS_CELL * scale;
            if (p < outSize) { img[(size_t)s * outSize + p] = 0xFF202020; img[(size_t)p * outSize + s] = 0xFF202020; }
        }
    }
    const char* path = argc > 1 ? argv[1] : "atlas.png";
    writePNG(path, outSize, outSize, img.data());
    printf("wrote %s (%dx%d), %d tiles\n", path, outSize, outSize, (int)T_COUNT);
    for (int i = 1; i < B_COUNT; i++) if (!g_blocks[i].name) printf("block %d has no name!\n", i);
    return 0;
}
