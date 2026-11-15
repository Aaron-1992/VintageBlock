// vb_blocks.cpp : procedural 8x8 pixel art atlas + block/item registry
#include "vb_blocks.h"
#include <cstring>

BlockDef g_blocks[B_COUNT];
static const int ITEM_CACHE_SIZE = 2048;
static ItemDef* s_itemCache[ITEM_CACHE_SIZE];
static inline int itemIndex(u16 id) {
    if (id < 0x3000) return -1;
    int i = (int)id - 0x3000;
    return (i >= 0 && i < ITEM_CACHE_SIZE) ? i : -1;
}
static int s_texAvg[T_COUNT];

// ---------------------------------------------------------------- pixel canvas
struct Px {
    u32 c[8][8];
    Px() { memset(c, 0, sizeof(c)); }
};
static inline void setPx(Px& p, int x, int y, u32 col) {
    if (x >= 0 && x < 8 && y >= 0 && y < 8) p.c[y][x] = col;
}
static inline u32 getPx(const Px& p, int x, int y) { return p.c[clampi(y, 0, 7)][clampi(x, 0, 7)]; }

// dithered noise fill: two tone pixel art
static u32 mixc(u32 a, u32 b, float t) { return lerpColor(a, b, t); }
static void fillNoise(Px& p, u32 base, u32 alt, u32 seed, int density = 40) {
    alt = mixc(base, alt, 0.8f);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int h = (int)(hash2i(x, y, seed) % 100);
            p.c[y][x] = h < density ? alt : base;
        }
}
static void speckles(Px& p, u32 col, u32 seed, int count) {
    Rng r(seed * 7919u + 13);
    for (int i = 0; i < count; i++) setPx(p, r.irange(0, 7), r.irange(0, 7), col);
}
static void vShade(Px& p, float topF, float botF) {
    for (int y = 0; y < 8; y++) {
        float f = lerpf(topF, botF, y / 7.f);
        for (int x = 0; x < 8; x++) if ((p.c[y][x] >> 24) != 0) p.c[y][x] = shadeColor(p.c[y][x], f);
    }
}
// pixel art from 8 rows, chars '1'..'8' map to cols[0..7], '.' transparent
static void art(Px& p, const char* const rows[8], const u32 cols[8]) {
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            char ch = rows[y][x];
            if (ch >= '1' && ch <= '8') p.c[y][x] = cols[ch - '1'];
            else p.c[y][x] = 0;
        }
}

// ---------------------------------------------------------------- palette
static const u32 C_TRANSP = 0x00000000;
static u32 mix(u32 a, u32 b, float t) { return lerpColor(a, b, t); }

// ---------------------------------------------------------------- tile painters
static void paintTile(int t, Px& p) {
    u32 s = (u32)t * 2654435761u + 9091u;
    switch (t) {
    // ---------------- soil
    case T_GRASS_TOP: fillNoise(p, 0xdcdcdc, 0xe8e8e8, s, 42); speckles(p, 0xc4c4c4, s, 7);
        speckles(p, 0xf2f2f2, s + 3, 5); break;
    case T_GRASS_DRY_TOP: fillNoise(p, 0xe6e2d2, 0xf0ecdd, s, 42); speckles(p, 0xd2cebd, s, 7); break;
    case T_GRASS_SNOW_TOP: fillNoise(p, 0xe8ecef, 0xf4f7fa, s, 40); speckles(p, 0xd8dee4, s, 6); break;
    case T_GRASS_SIDE: {
        fillNoise(p, 0x7a5a3c, 0x866444, s, 40);
        for (int x = 0; x < 8; x++) {
            int d = 4 + (int)(hash2i(x, 3, s) % 4);
            for (int y = 0; y < d; y++) {
                if (y == d - 1 && (hash2i(x, y, s + 2) & 3) == 0) continue;   // ragged edge
                p.c[y][x] = (hash2i(x, y, s + 1) & 1) ? 0x648440 : 0x76944a;
            }
        }
    } break;
    case T_GRASS_DRY_SIDE: {
        fillNoise(p, 0x7a5a3c, 0x866444, s, 40);
        for (int x = 0; x < 8; x++) {
            int d = 4 + (int)(hash2i(x, 3, s) % 4);
            for (int y = 0; y < d; y++) {
                if (y == d - 1 && (hash2i(x, y, s + 2) & 3) == 0) continue;
                p.c[y][x] = (hash2i(x, y, s + 1) & 1) ? 0x969046 : 0xa8a052;
            }
        }
    } break;
    case T_GRASS_SNOW_SIDE: {
        fillNoise(p, 0x7a5a3c, 0x866444, s, 40);
        for (int x = 0; x < 8; x++) {
            int d = 2 + (int)(hash2i(x, 3, s) % 3);
            for (int y = 0; y < d; y++) p.c[y][x] = (hash2i(x, y, s + 1) & 1) ? 0xe8ecef : 0xf4f7fa;
        }
    } break;
    case T_DIRT: fillNoise(p, 0x7a5a3c, 0x866444, s, 42); speckles(p, 0x6a4c32, s, 6); break;
    case T_SAND: fillNoise(p, 0xd9c489, 0xe4d29a, s, 45); speckles(p, 0xc9b478, s, 5); break;
    case T_RED_SAND: fillNoise(p, 0xb06a3e, 0xc07a48, s, 45); speckles(p, 0x9a5a34, s, 5); break;
    case T_GRAVEL: {
        fillNoise(p, 0x8a8278, 0x6f6a62, s, 50);
        speckles(p, 0xa8a096, s, 7); speckles(p, 0x5c5850, s + 1, 6);
    } break;
    case T_CLAY: fillNoise(p, 0x9a9aa8, 0xa8a8b6, s, 35); speckles(p, 0x8a8a98, s, 5); break;
    case T_PEAT: fillNoise(p, 0x4a3a24, 0x3a2c1a, s, 45); speckles(p, 0x2e2214, s, 6); break;
    case T_SNOW: fillNoise(p, 0xeef2f5, 0xfbfdff, s, 40); break;
    case T_PERMAFROST: fillNoise(p, 0x8a8070, 0x9a9080, s, 40); speckles(p, 0xb0b8c0, s, 4); break;
    // ---------------- rock
    case T_GRANITE: fillNoise(p, 0x9a8f88, 0x8a7f78, s, 50);
        speckles(p, 0xb0a49a, s, 6); speckles(p, 0x6e645e, s + 1, 5); break;
    case T_ANDESITE: fillNoise(p, 0x8a8a86, 0x7c7c78, s, 50);
        speckles(p, 0x9a9a96, s, 5); speckles(p, 0x66665f, s + 1, 4); break;
    case T_BASALT: fillNoise(p, 0x585a60, 0x4a4c52, s, 50); speckles(p, 0x6c6e74, s, 4); break;
    case T_LIMESTONE: fillNoise(p, 0xbfb79f, 0xcdc6ae, s, 45); speckles(p, 0xa8a08a, s, 5); break;
    case T_CHALK: fillNoise(p, 0xc9c4b3, 0xd8d3c2, s, 40); speckles(p, 0xbcb7a6, s, 4); break;
    case T_SLATE: fillNoise(p, 0x6a7080, 0x5e6474, s, 45);
        for (int x = 0; x < 8; x++) if (hash2i(x, 1, s) % 3 == 0) p.c[3][x] = 0x565c6c; break;
    case T_SANDSTONE: {
        fillNoise(p, 0xcaa878, 0xd8b98a, s, 45);
        for (int y = 0; y < 8; y += 3) for (int x = 0; x < 8; x++) p.c[y][x] = shadeColor(p.c[y][x], 0.88f);
    } break;
    case T_PERIDOTITE: fillNoise(p, 0x6a7a55, 0x5c6c48, s, 45); speckles(p, 0x8a9a6a, s, 6); break;
    case T_KIMBERLITE: fillNoise(p, 0x55687a, 0x475a6c, s, 48); speckles(p, 0x7a8ea0, s, 5); break;
    case T_SHALE: fillNoise(p, 0x777f88, 0x696f78, s, 45);
        for (int y = 0; y < 8; y += 2) for (int x = 0; x < 8; x++) if (hash2i(x, y, s) & 1) p.c[y][x] = 0x60666e; break;
    case T_COBBLE_GREY: case T_COBBLE_WARM: case T_COBBLE_DARK: {
        u32 base = t == T_COBBLE_GREY ? 0x8a8a86 : (t == T_COBBLE_WARM ? 0x9a8b7a : 0x5e6066);
        fillNoise(p, base, shadeColor(base, 1.15f), s, 30);
        // mortar lines
        for (int x = 0; x < 8; x++) { p.c[0][x] = shadeColor(base, 0.6f); p.c[4][x] = shadeColor(base, 0.65f); }
        for (int y = 0; y < 4; y++) p.c[y][3] = shadeColor(base, 0.65f);
        for (int y = 4; y < 8; y++) p.c[y][6] = shadeColor(base, 0.65f);
        for (int x = 0; x < 8; x++) p.c[7][x] = shadeColor(base, 0.75f);
    } break;
    case T_BRICKS: {
        fillNoise(p, 0xa05a44, 0xac6650, s, 35);
        for (int x = 0; x < 8; x++) { p.c[0][x] = 0x8a8078; p.c[4][x] = 0x8a8078; }
        p.c[1][3] = p.c[2][3] = p.c[3][3] = 0x8a8078;
        p.c[5][6] = p.c[6][6] = p.c[7][6] = 0x8a8078;
    } break;
    case T_STONE_BRICK: {
        fillNoise(p, 0x9a968e, 0x8e8a82, s, 30);
        for (int x = 0; x < 8; x++) { p.c[0][x] = 0x6e6a64; p.c[4][x] = 0x6e6a64; }
        p.c[1][3] = p.c[2][3] = p.c[3][3] = 0x6e6a64;
        p.c[5][6] = p.c[6][6] = p.c[7][6] = 0x6e6a64;
        p.c[0][0] = p.c[4][4] = shadeColor(0xb0aca4, 0.9f);
    } break;
    case T_POLISHED: fillNoise(p, 0xa8a49a, 0xb2aea4, s, 18); break;
    // ---------------- ores (dark stone host + mineral cluster)
    case T_ORE_COPPER: case T_ORE_TIN: case T_ORE_LEAD: case T_ORE_ZINC: case T_ORE_IRON:
    case T_ORE_MAGNETITE: case T_ORE_GOLD: case T_ORE_SILVER: case T_ORE_COAL:
    case T_ORE_LIGNITE: case T_ORE_QUARTZ: {
        fillNoise(p, 0x7a7a76, 0x6a6a66, s, 45);
        u32 c1 = 0xff8833, c2 = 0xcc6622;
        switch (t) {
        case T_ORE_COPPER: c1 = 0x2ea88a; c2 = 0x1f7f68; break;
        case T_ORE_TIN: c1 = 0x9aa8b8; c2 = 0x76848f; break;
        case T_ORE_LEAD: c1 = 0x8a8a90; c2 = 0x60606a; break;
        case T_ORE_ZINC: c1 = 0xa8b0a0; c2 = 0x808a78; break;
        case T_ORE_IRON: c1 = 0xb06a4a; c2 = 0x8a4a34; break;
        case T_ORE_MAGNETITE: c1 = 0x3a3a42; c2 = 0x24242a; break;
        case T_ORE_GOLD: c1 = 0xf0c040; c2 = 0xc09a2a; break;
        case T_ORE_SILVER: c1 = 0xd8dde4; c2 = 0xa8b0b8; break;
        case T_ORE_COAL: c1 = 0x2c2c30; c2 = 0x141416; break;
        case T_ORE_LIGNITE: c1 = 0x4a3a28; c2 = 0x332618; break;
        case T_ORE_QUARTZ: c1 = 0xe8e4dc; c2 = 0xc0bcb4; break;
        }
        Rng r(s);
        for (int k = 0; k < 3; k++) {
            int cx = r.irange(1, 5), cy = r.irange(1, 5);
            setPx(p, cx, cy, c1);
            setPx(p, cx + 1, cy, c1); setPx(p, cx, cy + 1, c2); setPx(p, cx - 1, cy + 1, c2);
            setPx(p, cx + 1, cy + 1, c2); setPx(p, cx - 1, cy, c2);
        }
    } break;
    // ---------------- wood
    case T_LOG_PINE_S: case T_LOG_OAK_S: case T_LOG_BIRCH_S: case T_LOG_MAPLE_S:
    case T_LOG_ACACIA_S: case T_LOG_KAPOK_S: {
        u32 bark = 0x4a3527;
        switch (t) {
        case T_LOG_PINE_S: bark = 0x4a3527; break;
        case T_LOG_OAK_S: bark = 0x5c452e; break;
        case T_LOG_BIRCH_S: bark = 0xd8d4c8; break;
        case T_LOG_MAPLE_S: bark = 0x6b4a30; break;
        case T_LOG_ACACIA_S: bark = 0x6a4a34; break;
        case T_LOG_KAPOK_S: bark = 0x7a6a52; break;
        }
        fillNoise(p, bark, shadeColor(bark, 1.12f), s, 35);
        for (int x = 0; x < 8; x++) {
            if (hash2i(x, 0, s) % 3 == 0) for (int y = 0; y < 8; y++) p.c[y][x] = shadeColor(bark, 0.82f);
        }
        if (t == T_LOG_BIRCH_S) { for (int x = 0; x < 8; x += 3) { p.c[2][x] = 0x3a3a38; p.c[2][x + 1] = 0x3a3a38; p.c[6][x] = 0x3a3a38; } }
    } break;
    case T_LOG_PINE_T: case T_LOG_OAK_T: case T_LOG_BIRCH_T: case T_LOG_MAPLE_T:
    case T_LOG_ACACIA_T: case T_LOG_KAPOK_T: {
        u32 wood = 0x9a8058, bark = 0x4a3527;
        switch (t) {
        case T_LOG_PINE_T: wood = 0xa88f62; bark = 0x4a3527; break;
        case T_LOG_OAK_T: wood = 0x9a7d4e; bark = 0x5c452e; break;
        case T_LOG_BIRCH_T: wood = 0xc4b48e; bark = 0xd8d4c8; break;
        case T_LOG_MAPLE_T: wood = 0xb08a5a; bark = 0x6b4a30; break;
        case T_LOG_ACACIA_T: wood = 0xa87a4a; bark = 0x6a4a34; break;
        case T_LOG_KAPOK_T: wood = 0xb89c70; bark = 0x7a6a52; break;
        }
        fillNoise(p, wood, shadeColor(wood, 1.08f), s, 35);
        for (int i = 0; i < 8; i++) {
            p.c[0][i] = p.c[7][i] = p.c[i][0] = p.c[i][7] = bark;
        }
        for (int i = 2; i < 6; i++) p.c[2][i] = p.c[5][i] = p.c[i][2] = p.c[i][5] = shadeColor(wood, 0.85f);
        p.c[3][3] = p.c[3][4] = p.c[4][3] = p.c[4][4] = shadeColor(wood, 0.7f);
    } break;
    case T_PLANK_PINE: case T_PLANK_OAK: case T_PLANK_BIRCH: case T_PLANK_MAPLE:
    case T_PLANK_ACACIA: case T_PLANK_KAPOK: {
        u32 wood = 0xb08a5a;
        switch (t) {
        case T_PLANK_PINE: wood = 0xc0a070; break;
        case T_PLANK_OAK: wood = 0xa8865a; break;
        case T_PLANK_BIRCH: wood = 0xd8c8a0; break;
        case T_PLANK_MAPLE: wood = 0xb89060; break;
        case T_PLANK_ACACIA: wood = 0xb07a4a; break;
        case T_PLANK_KAPOK: wood = 0xc0a87a; break;
        }
        fillNoise(p, wood, shadeColor(wood, 1.06f), s, 30);
        for (int x = 0; x < 8; x++) { p.c[3][x] = shadeColor(wood, 0.82f); p.c[7][x] = shadeColor(wood, 0.78f); }
        p.c[1][2] = p.c[1][5] = shadeColor(wood, 0.9f);
        p.c[5][3] = p.c[5][6] = shadeColor(wood, 0.9f);
    } break;
    case T_LEAF_PINE: case T_LEAF_OAK: case T_LEAF_BIRCH: case T_LEAF_MAPLE:
    case T_LEAF_ACACIA: case T_LEAF_KAPOK: {
        u32 g = 0x5a7d3a, g2 = 0x6d9147;
        switch (t) {
        case T_LEAF_PINE: g = 0x385230; g2 = 0x46643c; break;
        case T_LEAF_OAK: g = 0x5a7d3a; g2 = 0x6d9147; break;
        case T_LEAF_BIRCH: g = 0x7aa04a; g2 = 0x8db058; break;
        case T_LEAF_MAPLE: g = 0x86a040; g2 = 0x98b252; break;
        case T_LEAF_ACACIA: g = 0x6b8a44; g2 = 0x7d9c52; break;
        case T_LEAF_KAPOK: g = 0x467a3a; g2 = 0x568c48; break;
        }
        Rng r(s);
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int h = (int)(hash2i(x, y, s) % 100);
                if (h < 16) p.c[y][x] = 0;                 // gaps
                else p.c[y][x] = (h < 55) ? g : g2;
            }
        (void)r;
    } break;
    // ---------------- plants
    case T_TALLGRASS: {
        const char* const rows[8] = {
            "........", "...2....", "..12..2.", "..12.12.", ".212.12.", ".21121 2", "12112112", "11121111" };
        const u32 cols[8] = { 0xb4b4b4, 0xd2d2d2, 0xe8e8e8, 0, 0, 0, 0, 0 };
        art(p, rows, cols); break;
    }
    case T_TALLGRASS_DRY: {
        const char* const rows[8] = {
            "........", "....2...", "..2.12..", "..12.1..", ".212.12.", ".21121.2", "12112112", "11121111" };
        const u32 cols[8] = { 0x8a7a38, 0xa89a48, 0xc0b058, 0, 0, 0, 0, 0 };
        art(p, rows, cols); break;
    }
    case T_FERN: {
        const char* const rows[8] = {
            "........", "...2....", "..212...", ".21212..", "..2121..", ".212.21.", "..2...2.", "........" };
        const u32 cols[8] = { 0xa8a8a8, 0xc8c8c8, 0xe4e4e4, 0, 0, 0, 0, 0 };
        art(p, rows, cols); break;
    }
    case T_FLOWER_RED: case T_FLOWER_YELLOW: case T_FLOWER_BLUE: case T_FLOWER_PURPLE: {
        u32 petal = 0xc83c3c, core = 0xf0e070;
        if (t == T_FLOWER_YELLOW) { petal = 0xe8c840; core = 0xc08a20; }
        if (t == T_FLOWER_BLUE) { petal = 0x4a6ec8; core = 0xd8e0f0; }
        if (t == T_FLOWER_PURPLE) { petal = 0x9a5ac0; core = 0xe0d0f0; }
        const char* const rows[8] = {
            "........", "..333...", ".34443..", "..333...", "...2....", "...2....", "..121...", ".111...." };
        const u32 cols[8] = { 0x3e6a34, 0x4f8442, 0x3e6a34, petal, core, 0x4f8442, 0, 0 };
        art(p, rows, cols); break;
    }
    case T_HEATHER: {
        const char* const rows[8] = {
            "........", ".2.2.2..", "2323232.", ".232323.", "..2.2.2.", ".121212.", ".111111.", ".111111." };
        const u32 cols[8] = { 0x4a5a34, 0x8a6aa8, 0xa88ac0, 0, 0, 0, 0, 0 };
        art(p, rows, cols); break;
    }
    case T_CATTAIL: {
        const char* const rows[8] = {
            "..4.....", "..3.....", "..3.....", ".232....", "..2.2...", ".2.12...", "..1.2...", ".11....." };
        const u32 cols[8] = { 0x4a7038, 0x5c8a44, 0x4a3a24, 0x8a6a3a, 0, 0, 0, 0 };
        art(p, rows, cols); break;
    }
    case T_REED: {
        const char* const rows[8] = {
            "..2..2..", ".212.12.", "..2..2..", ".212.12.", "..2..2..", "11211121", "..2..2..", "..1..1.." };
        const u32 cols[8] = { 0x5e7a3a, 0x7a9448, 0, 0, 0, 0, 0, 0 };
        art(p, rows, cols); break;
    }
    case T_MUSHROOM_RED: case T_MUSHROOM_BROWN: {
        u32 cap = t == T_MUSHROOM_RED ? 0xc03c30 : 0x9a7448;
        u32 spot = t == T_MUSHROOM_RED ? 0xe8dcd0 : 0xb08a5c;
        const char* const rows[8] = {
            "........", "..4444..", ".454445.", "44434444", "..333...", "...33...", "...33...", "..3333.." };
        const u32 cols[8] = { 0x8a8070, 0x8a8070, 0xd8d0c0, cap, spot, 0, 0, 0 };
        art(p, rows, cols); break;
    }
    case T_BERRYBUSH: {
        const char* const rows[8] = {
            "........", "..2.2...", ".21212..", "2.212.2.", ".21212..", "..3.3...", ".3...3..", "........" };
        const u32 cols[8] = { 0x3e6a34, 0x4f8442, 0xc03c50, 0, 0, 0, 0, 0 };
        art(p, rows, cols); break;
    }
    case T_DEADBUSH: {
        const char* const rows[8] = {
            "........", "..2...2.", ".2.2.2..", "..2.2...", "...2....", "..2.2...", ".2...2..", "........" };
        const u32 cols[8] = { 0x6a5a3a, 0x8a7a4a, 0, 0, 0, 0, 0, 0 };
        art(p, rows, cols); break;
    }
    case T_SAPLING: {
        const char* const rows[8] = {
            "........", "...2....", "..222...", ".2232...", "..222...", "...2....", "...2....", "..111..." };
        const u32 cols[8] = { 0x4a3527, 0x5c8a44, 0x3e6a34, 0, 0, 0, 0, 0 };
        art(p, rows, cols); break;
    }
    case T_CACTUS_S: {
        fillNoise(p, 0x4a7a44, 0x568a4c, s, 35);
        for (int y = 0; y < 8; y++) { p.c[y][0] = 0x3a6034; p.c[y][7] = 0x3a6034; }
        for (int y = 1; y < 8; y += 3) { p.c[y][3] = 0xdfe0c0; p.c[y][5] = 0xdfe0c0; }
    } break;
    case T_CACTUS_T: fillNoise(p, 0x568a4c, 0x4a7a44, s, 30); speckles(p, 0xdfe0c0, s, 4); break;
    case T_CROP_0: case T_CROP_1: case T_CROP_2: case T_CROP_3: case T_CROP_4: case T_CROP_5: {
        int stage = t - T_CROP_0;
        int hgt = 1 + stage;
        const char* const rows[8] = {
            "........", "........", "........", "........", "........", "........", "........", "........" };
        char buf[8][9];
        for (int y = 0; y < 8; y++) { memcpy(buf[y], rows[y], 8); buf[y][8] = 0; }
        for (int x = 0; x < 8; x += 2) {
            for (int y = 7; y > 7 - hgt; y--) { buf[y][x] = '1'; }
            if (stage >= 3) { buf[7 - hgt][x] = '2'; buf[6 - hgt][x + 1] = '2'; }
            if (stage >= 5) { buf[7 - hgt][x] = '3'; }
        }
        const char* r2[8] = { buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7] };
        const u32 cols[8] = { 0x4a7a34, 0x6a9a44, 0xd0b040, 0, 0, 0, 0, 0 };
        art(p, r2, cols); break;
    }
    // ---------------- misc blocks
    case T_TORCH: {
        const char* const rows[8] = {
            "..3.....", "..32....", "..33....", "..2.....", "..2.....", "..2.....", "..2.....", "..2....." };
        const u32 cols[8] = { 0x9a7a4a, 0x6a4a2c, 0xffcc55, 0xff8833, 0, 0, 0, 0 };
        art(p, rows, cols); break;
    }
    case T_FIRE: {
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int h = (int)(hash2i(x, y, s) % 100);
                if (y < 2) p.c[y][x] = 0;
                else if (h < 40 + y * 6) p.c[y][x] = (h < 20) ? 0xffdd66 : 0xff9933;
                else p.c[y][x] = (y > 5) ? 0xcc5522 : 0;
            }
    } break;
    case T_GLASS: {
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) p.c[y][x] = 0x00000000;
        for (int i = 0; i < 8; i++) { p.c[0][i] = 0xc8e0ea | 0x60000000; p.c[7][i] = 0xc8e0ea | 0x60000000; p.c[i][0] = 0xc8e0ea | 0x60000000; p.c[i][7] = 0xc8e0ea | 0x60000000; }
    } break;
    case T_THATCH: {
        fillNoise(p, 0xc0a050, 0xa88a40, s, 40);
        for (int y = 0; y < 8; y += 3) for (int x = 0; x < 8; x++) p.c[y][x] = 0x8a7030;
    } break;
    case T_CHARCOAL: fillNoise(p, 0x30302e, 0x26262a, s, 45); speckles(p, 0x444442, s, 5); break;
    case T_ICE: {
        fillNoise(p, 0x9ec8e8, 0xb8dcf4, s, 35);
        for (int i = 0; i < 8; i++) { p.c[i][i] = 0xd8eef8; }
    } break;
    case T_WATER: fillNoise(p, 0x2a6b78, 0x337d88, s, 40); break;
    case T_LAVA: fillNoise(p, 0xd8541e, 0xff8a2a, s, 50); speckles(p, 0xffd060, s, 6); break;
    case T_CRATE: {
        fillNoise(p, 0xa8865a, 0x98784e, s, 25);
        for (int i = 0; i < 8; i++) { p.c[0][i] = p.c[7][i] = p.c[i][0] = p.c[i][7] = 0x6a4a2c; }
        for (int i = 1; i < 7; i++) { p.c[i][i] = 0x8a6a44; }
    } break;
    case T_FIREPIT: {
        fillNoise(p, 0x6a6660, 0x585450, s, 40);
        for (int x = 1; x < 7; x++) { p.c[2][x] = 0x3a3834; p.c[3][x] = 0x3a3834; }
        p.c[2][3] = p.c[2][4] = p.c[3][3] = p.c[3][4] = 0xff9933;
    } break;
    case T_BEDROCK: fillNoise(p, 0x3a3a3c, 0x2a2a2c, s, 55); speckles(p, 0x4e4e50, s, 6); break;
    case T_SLAG: fillNoise(p, 0x3e3a38, 0x2e2a28, s, 55); speckles(p, 0x6a4a3a, s, 5); break;
    case T_BLOOM: fillNoise(p, 0x8a7a6a, 0x6a5a4a, s, 50); speckles(p, 0xd8a860, s, 6); break;
    case T_SCAFFOLD: {
        for (int i = 0; i < 8; i++) for (int x = 0; x < 8; x++) p.c[i][x] = 0;
        for (int i = 0; i < 8; i++) { p.c[i][1] = 0xb09060; p.c[i][2] = 0x907850; p.c[i][6] = 0xb09060; p.c[i][5] = 0x907850; }
        for (int i = 0; i < 8; i += 4) for (int x = 0; x < 8; x++) p.c[i][x] = 0xa08050;
    } break;
    // ---------------- entity skins
    case T_E_FUR_BROWN: fillNoise(p, 0x8a6440, 0x7a5636, s, 40); speckles(p, 0x6a4a30, s, 6); break;
    case T_E_FUR_GREY: fillNoise(p, 0x7a7a78, 0x6a6a68, s, 42); speckles(p, 0x585856, s, 6); break;
    case T_E_FUR_WHITE: fillNoise(p, 0xd8d4c8, 0xc8c4b8, s, 38); break;
    case T_E_DRIFTER: {
        fillNoise(p, 0x2e2a34, 0x241f2a, s, 45);
        speckles(p, 0x4a3a5a, s, 5);
        for (int i = 0; i < 8; i++) p.c[i][i] = 0x3a2a48;
    } break;
    case T_E_DRIFTER_EYE: {
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) p.c[y][x] = 0x1a1420;
        p.c[3][3] = p.c[3][4] = p.c[4][3] = p.c[4][4] = 0xd04a3a;
    } break;
    case T_E_BEAK: fillNoise(p, 0xd8a040, 0xc89030, s, 35); break;
    case T_E_EYE: {
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) p.c[y][x] = 0xe8e4d8;
        p.c[3][3] = p.c[4][3] = p.c[3][4] = p.c[4][4] = 0x201818;
    } break;
    // ---------------- item icons
    default: break;
    }
}

// tools share one shape set, coloured per tier
static void paintToolIcon(int t, Px& p) {
    // decode: which tool and which tier
    struct Shape { const char* rows[8]; };
    static const Shape axe =  { { "........", "..111...", ".11121..", ".11121..", "..1.21..", "....21..", "....21..", "........" } };
    static const Shape pick = { { "........", "1.......", ".11.222.", "..111...", "...21...", "...21...", "...21...", "........" } };
    static const Shape shovel = { { "........", "..111...", "..121...", "...2....", "...2....", "..2.....", "..2.....", "........" } };
    static const Shape knife = { { "........", "........", "...111..", "..1112..", ".111.2..", ".11..2..", ".....2..", "........" } };
    static const Shape sword = { { "........", ".....11.", "....111.", "...111..", "..111...", ".11.....", "1..2....", "........" } };
    static const Shape hammer = { { "........", "..1111..", "..1221..", "..1111..", "...2....", "...2....", "...2....", "........" } };
    static const Shape prospick = { { "........", "11111...", "..112...", "...12...", "...12...", "...12...", "...12...", "........" } };
    static const Shape chisel = { { "........", "....111.", "....121.", "....11..", "...2....", "..2.....", ".2......", "........" } };
    const Shape* sh = &axe;
    u32 metal = 0xc87a4a;
    int tiers = 0;
    int base = t;
    // flint tools
    if (t >= T_I_KNIFE_FLINT && t <= T_I_PICK_FLINT) {
        metal = 0x8a8a86;
        switch (t) {
        case T_I_KNIFE_FLINT: sh = &knife; break;
        case T_I_AXE_FLINT: sh = &axe; break;
        case T_I_SHOVEL_FLINT: sh = &shovel; break;
        default: sh = &pick; break;
        }
        tiers = 1;
    } else {
        int g = (t - T_I_AXE_C);
        int shapeIdx = g % 8;
        int tier = g / 8;
        switch (shapeIdx) {
        case 0: sh = &axe; break;
        case 1: sh = &pick; break;
        case 2: sh = &shovel; break;
        case 3: sh = &knife; break;
        case 4: sh = &sword; break;
        case 5: sh = &hammer; break;
        case 6: sh = &prospick; break;
        default: sh = &chisel; break;
        }
        metal = tier == 0 ? 0xc87a4a : (tier == 1 ? 0xc8a050 : 0xc0c4cc);
    }
    (void)tiers; (void)base;
    const u32 cols[8] = { metal, 0x8a6a44 };
    art(p, sh->rows, cols);
}

static void paintItemIcon(int t, Px& p) {
    switch (t) {
    case T_I_INGOT: {
        const char* const rows[8] = { "........", "........", "..1111..", ".111111.", ".122221.", ".122221.", "........", "........" };
        const u32 cols[8] = { 0xffffff, 0x808080 };  // filled white, tinted red at draw time
        art(p, rows, cols); break;
    }
    case T_I_NUGGET: {
        const char* const rows[8] = { "........", "...11...", "..1111..", ".111111.", ".121111.", "..1111..", "...11...", "........" };
        const u32 cols[8] = { 0xffffff, 0x909090 };
        art(p, rows, cols); break;
    }
    case T_I_STICK: {
        const char* const rows[8] = { "........", "......1.", ".....11.", "....11..", "...11...", "..11....", ".11.....", "........" };
        const u32 cols[8] = { 0x9a7448 };
        art(p, rows, cols); break;
    }
    case T_I_FLINT: {
        const char* const rows[8] = { "........", "...1....", "..121...", ".12211..", ".12211..", "..111...", "...1....", "........" };
        const u32 cols[8] = { 0x5a5a5e, 0x9a9a9e };
        art(p, rows, cols); break;
    }
    case T_I_SEEDS: {
        const char* const rows[8] = { "........", "..1.1...", ".1.1.1..", "..1.1...", "...1.1..", "..1.1...", "........", "........" };
        const u32 cols[8] = { 0xc8b060 };
        art(p, rows, cols); break;
    }
    case T_I_BERRIES: {
        const char* const rows[8] = { "........", "..1..1..", ".1211.1.", "..121...", ".1.121..", "..1..1..", "........", "........" };
        const u32 cols[8] = { 0xc03c50, 0xe86a7a };
        art(p, rows, cols); break;
    }
    case T_I_MEAT: {
        const char* const rows[8] = { "........", "..111...", ".11221..", "1122211.", ".11221..", "..111...", "........", "........" };
        const u32 cols[8] = { 0xa8463c, 0xd87a6a };
        art(p, rows, cols); break;
    }
    case T_I_CLAYBALL: {
        const char* const rows[8] = { "........", "...11...", "..1111..", ".111111.", ".111111.", "..1111..", "...11...", "........" };
        const u32 cols[8] = { 0x9a9aa8 };
        art(p, rows, cols); break;
    }
    case T_I_BOWL: {
        const char* const rows[8] = { "........", "........", ".1....1.", ".111111.", ".111111.", "..1111..", "...11...", "........" };
        const u32 cols[8] = { 0xb09060 };
        art(p, rows, cols); break;
    }
    case T_I_POT: {
        const char* const rows[8] = { "........", "..1111..", ".111111.", ".111111.", ".122111.", ".111111.", "..1111..", "........" };
        const u32 cols[8] = { 0x9a7a5a, 0x6a4a34 };
        art(p, rows, cols); break;
    }
    case T_I_ROPE: {
        const char* const rows[8] = { "........", ".11..11.", "1..11..1", ".1..1..1", "1..11..1", ".11..11.", "........", "........" };
        const u32 cols[8] = { 0xc0a870 };
        art(p, rows, cols); break;
    }
    case T_I_TORCH: {
        const char* const rows[8] = { "..33....", "..32....", "..2.....", "..2.....", "..2.....", "..2.....", "..2.....", "..2....." };
        const u32 cols[8] = { 0x9a7a4a, 0x6a4a2c, 0xffcc55, 0xff8833 };
        art(p, rows, cols); break;
    }
    case T_I_GEAR: {
        const char* const rows[8] = { "..1.1...", ".11111..", "..121...", "1112111.", "..121...", ".11111..", "..1.1...", "........" };
        const u32 cols[8] = { 0xa08a60, 0x4a4038 };
        art(p, rows, cols); break;
    }
    case T_I_CROPS: {
        const char* const rows[8] = { ".....1..", "....11..", "...111..", "..1111..", "...11...", "...11...", "........", "........" };
        const u32 cols[8] = { 0xd0b040, 0x6a9a44 };
        art(p, rows, cols); break;
    }
    case T_I_BREAD: {
        const char* const rows[8] = { "........", "..1111..", ".112211.", "11222211", "11222211", ".111111.", "........", "........" };
        const u32 cols[8] = { 0xc09050, 0xe0b878 };
        art(p, rows, cols); break;
    }
    case T_I_STEW: {
        const char* const rows[8] = { "........", "..1111..", ".122221.", ".111111.", ".111111.", "..1111..", "...11...", "........" };
        const u32 cols[8] = { 0xb09060, 0x8a5a3a };
        art(p, rows, cols); break;
    }
    case T_I_TALLOW: {
        const char* const rows[8] = { "........", "...11...", "...11...", "..1111..", "..1211..", "..1111..", "...11...", "........" };
        const u32 cols[8] = { 0xe8dcb0, 0xc0a870 };
        art(p, rows, cols); break;
    }
    case T_I_COAL: {
        const char* const rows[8] = { "........", "..111...", ".11211..", ".1121 1.", ".11111..", "..111...", "........", "........" };
        const u32 cols[8] = { 0x30303a, 0x585864 };
        art(p, rows, cols); break;
    }
    case T_I_ARROWHEAD: {
        const char* const rows[8] = { "...1....", "..111...", "..121...", ".11111..", "..121...", "...1....", "...1....", "........" };
        const u32 cols[8] = { 0xa8a8a8, 0x707070 };
        art(p, rows, cols); break;
    }
    case T_I_LOOM: {
        const char* const rows[8] = { "1.1.1.1.", "1111111.", "1.1.1.1.", "1.1.1.1.", "1111111.", "1.1.1.1.", "1.1.1.1.", "........" };
        const u32 cols[8] = { 0xb0a080 };
        art(p, rows, cols); break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------- atlas build
static void normalizeAlpha(Px& p) {
    // colours authored as 0xRRGGBB implicitly mean opaque; 0 means transparent
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            u32 c = p.c[y][x];
            if (c != 0 && (c >> 24) == 0) p.c[y][x] = c | 0xFF000000u;
        }
}

static void paintAny(int t, Px& p) {
    memset(&p, 0, sizeof(p));
    if (t >= T_I_AXE_C && t <= T_I_PICK_FLINT) paintToolIcon(t, p);
    else if (t >= T_I_INGOT) paintItemIcon(t, p);
    else paintTile(t, p);
    normalizeAlpha(p);
}

static u32 avgOf(const Px& p) {
    u64 r = 0, g = 0, b = 0; int n = 0;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        u32 c = p.c[y][x];
        if (c == 0) continue;
        r += c >> 16 & 255; g += c >> 8 & 255; b += c & 255; n++;
    }
    if (!n) return 0x808080;
    return rgb((int)(r / n), (int)(g / n), (int)(b / n));
}

void buildAtlasLevel(u32* out, int size, int level) {
    memset(out, 0, (size_t)size * size * 4);
    int cell = ATLAS_CELL >> level;      // 16, 8, 4
    int art = 8 >> level;                // 8, 4, 2
    int pad = (cell - art) / 2;
    for (int t = 1; t < T_COUNT; t++) {
        Px base; paintAny(t, base);
        int col = (t % ATLAS_GRID) * cell, row = (t / ATLAS_GRID) * cell;
        if (level == 0) {
            // write art, then replicate borders outward
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                    out[(size_t)(row + pad + y) * size + (col + pad + x)] = base.c[y][x];
            // border replication
            for (int y = -pad; y < art + pad; y++)
                for (int x = -pad; x < art + pad; x++) {
                    int sx = clampi(x, 0, art - 1), sy = clampi(y, 0, art - 1);
                    out[(size_t)(row + pad + y) * size + (col + pad + x)] = base.c[sy][sx];
                }
        } else {
            // downsample from level-0 art
            Px src; paintAny(t, src);
            int f = 1 << level;
            for (int y = 0; y < art; y++)
                for (int x = 0; x < art; x++) {
                    u32 r = 0, g = 0, b = 0, a = 0; int n = 0;
                    for (int sy = 0; sy < f; sy++)
                        for (int sx = 0; sx < f; sx++) {
                            u32 c = src.c[y * f + sy][x * f + sx];
                            r += c >> 16 & 255; g += c >> 8 & 255; b += c & 255; a += c >> 24 & 255; n++;
                        }
                    out[(size_t)(row + pad + y) * size + (col + pad + x)] =
                        rgb((int)(r / n), (int)(g / n), (int)(b / n)) | ((a / n) << 24);
                }
            for (int y = -pad; y < art + pad; y++)
                for (int x = -pad; x < art + pad; x++) {
                    int sx = clampi(x, 0, art - 1), sy = clampi(y, 0, art - 1);
                    out[(size_t)(row + pad + y) * size + (col + pad + x)] =
                        out[(size_t)(row + pad + sy) * size + (col + pad + sx)];
                }
        }
    }
}

const u32* getAtlasRGBA() {
    static std::vector<u32> atlas;
    if (atlas.empty()) {
        atlas.resize((size_t)ATLAS_SIZE * ATLAS_SIZE);
        buildAtlasLevel(atlas.data(), ATLAS_SIZE, 0);
    }
    return atlas.data();
}

// tile averages for LOD colours and minimaps
static void computeAvgColors() {
    for (int t = 1; t < T_COUNT; t++) {
        Px p; paintAny(t, p);
        s_texAvg[t] = (int)avgOf(p);
    }
}
int textureAvgColor(int t) { return (t > 0 && t < T_COUNT) ? s_texAvg[t] : 0x808080; }

// ---------------------------------------------------------------- registry
static void defBlock(BlockId id, const char* name, int texS, int texT, int texB, u16 flags,
                     u8 hardness, u8 tool, u8 light = 0, u8 tint = TINT_NONE, u16 drop = 0, u8 minTier = 0) {
    BlockDef& d = g_blocks[id];
    d.name = name; d.texSide = (u16)texS; d.texTop = (u16)texT; d.texBottom = (u16)texB;
    d.flags = flags; d.hardness = hardness; d.tool = tool; d.light = light; d.tint = tint;
    d.drop = drop; d.minTier = minTier; d.top = 0;
}

void initBlocks() {
    computeAvgColors();
    memset(g_blocks, 0, sizeof(g_blocks));
    const u16 SLD = BF_SOLID | BF_OPAQUE;
    // rocks
    struct R { BlockId id; const char* n; int tex; u8 hard; };
    const R rocks[] = {
        { B_GRANITE, "Granite", T_GRANITE, 60 }, { B_ANDESITE, "Andesite", T_ANDESITE, 55 },
        { B_BASALT, "Basalt", T_BASALT, 60 }, { B_LIMESTONE, "Limestone", T_LIMESTONE, 40 },
        { B_CHALK, "Chalk", T_CHALK, 30 }, { B_SLATE, "Slate", T_SLATE, 45 },
        { B_SANDSTONE, "Sandstone", T_SANDSTONE, 35 }, { B_PERIDOTITE, "Peridotite", T_PERIDOTITE, 65 },
        { B_KIMBERLITE, "Kimberlite", T_KIMBERLITE, 70 }, { B_SHALE, "Shale", T_SHALE, 40 },
    };
    for (auto& r : rocks) defBlock(r.id, r.n, r.tex, r.tex, r.tex, SLD, r.hard, TOOL_PICK);
    defBlock(B_BEDROCK, "Bedrock", T_BEDROCK, T_BEDROCK, T_BEDROCK, SLD, 255, TOOL_PICK);
    // soil
    defBlock(B_GRASS, "Grass", T_GRASS_SIDE, T_GRASS_TOP, T_DIRT, SLD | BF_SOLID, 12, TOOL_SHOVEL, 0, TINT_GRASS, B_DIRT);
    defBlock(B_GRASS_DRY, "Dry Grass", T_GRASS_DRY_SIDE, T_GRASS_DRY_TOP, T_DIRT, SLD, 12, TOOL_SHOVEL, 0, TINT_GRASS, B_DIRT);
    defBlock(B_GRASS_SNOW, "Snowy Grass", T_GRASS_SNOW_SIDE, T_GRASS_SNOW_TOP, T_DIRT, SLD, 12, TOOL_SHOVEL, 0, TINT_NONE, B_DIRT);
    defBlock(B_DIRT, "Dirt", T_DIRT, T_DIRT, T_DIRT, SLD, 10, TOOL_SHOVEL);
    defBlock(B_DIRT_DRY, "Dry Dirt", T_DIRT, T_DIRT, T_DIRT, SLD, 10, TOOL_SHOVEL);
    defBlock(B_SAND, "Sand", T_SAND, T_SAND, T_SAND, SLD, 8, TOOL_SHOVEL);
    defBlock(B_RED_SAND, "Red Sand", T_RED_SAND, T_RED_SAND, T_RED_SAND, SLD, 8, TOOL_SHOVEL);
    defBlock(B_GRAVEL, "Gravel", T_GRAVEL, T_GRAVEL, T_GRAVEL, SLD, 9, TOOL_SHOVEL);
    defBlock(B_CLAY, "Clay", T_CLAY, T_CLAY, T_CLAY, SLD, 10, TOOL_SHOVEL);
    defBlock(B_PEAT, "Peat", T_PEAT, T_PEAT, T_PEAT, SLD, 10, TOOL_SHOVEL);
    defBlock(B_SNOW, "Snow Block", T_SNOW, T_SNOW, T_SNOW, SLD, 6, TOOL_SHOVEL);
    defBlock(B_PERMAFROST, "Permafrost", T_PERMAFROST, T_PERMAFROST, T_PERMAFROST, SLD, 30, TOOL_PICK);
    // ores
    struct O { BlockId id; const char* n; int tex; u8 hard; };
    const O ores[] = {
        { B_ORE_COPPER, "Copper Ore", T_ORE_COPPER, 70 }, { B_ORE_TIN, "Cassiterite", T_ORE_TIN, 70 },
        { B_ORE_LEAD, "Galena", T_ORE_LEAD, 70 }, { B_ORE_ZINC, "Sphalerite", T_ORE_ZINC, 70 },
        { B_ORE_IRON, "Hematite", T_ORE_IRON, 80 }, { B_ORE_MAGNETITE, "Magnetite", T_ORE_MAGNETITE, 80 },
        { B_ORE_GOLD, "Native Gold", T_ORE_GOLD, 75 }, { B_ORE_SILVER, "Native Silver", T_ORE_SILVER, 75 },
        { B_ORE_COAL, "Bituminous Coal", T_ORE_COAL, 55 }, { B_ORE_LIGNITE, "Lignite", T_ORE_LIGNITE, 45 },
        { B_ORE_QUARTZ, "Quartz", T_ORE_QUARTZ, 60 },
    };
    for (auto& o : ores) defBlock(o.id, o.n, o.tex, o.tex, o.tex, SLD, o.hard, TOOL_PICK, 0, TINT_NONE, 0, 1);
    // wood
    struct W { BlockId log, plank, leaf; const char* n; int logS, logT, plankT, leafT; };
    const W woods[] = {
        { B_LOG_PINE, B_PLANK_PINE, B_LEAF_PINE, "Pine", T_LOG_PINE_S, T_LOG_PINE_T, T_PLANK_PINE, T_LEAF_PINE },
        { B_LOG_OAK, B_PLANK_OAK, B_LEAF_OAK, "Oak", T_LOG_OAK_S, T_LOG_OAK_T, T_PLANK_OAK, T_LEAF_OAK },
        { B_LOG_BIRCH, B_PLANK_BIRCH, B_LEAF_BIRCH, "Birch", T_LOG_BIRCH_S, T_LOG_BIRCH_T, T_PLANK_BIRCH, T_LEAF_BIRCH },
        { B_LOG_MAPLE, B_PLANK_MAPLE, B_LEAF_MAPLE, "Maple", T_LOG_MAPLE_S, T_LOG_MAPLE_T, T_PLANK_MAPLE, T_LEAF_MAPLE },
        { B_LOG_ACACIA, B_PLANK_ACACIA, B_LEAF_ACACIA, "Acacia", T_LOG_ACACIA_S, T_LOG_ACACIA_T, T_PLANK_ACACIA, T_LEAF_ACACIA },
        { B_LOG_KAPOK, B_PLANK_KAPOK, B_LEAF_KAPOK, "Kapok", T_LOG_KAPOK_S, T_LOG_KAPOK_T, T_PLANK_KAPOK, T_LEAF_KAPOK },
    };
    for (auto& w : woods) {
        defBlock(w.log, w.n, w.logS, w.logT, w.logT, SLD, 30, TOOL_AXE);
        defBlock(w.plank, w.n, w.plankT, w.plankT, w.plankT, SLD, 22, TOOL_AXE);
        defBlock(w.leaf, w.n, w.leafT, w.leafT, w.leafT, BF_LEAF, 3, TOOL_NONE, 0, TINT_FOLIAGE);
    }
    // plants
    defBlock(B_TALLGRASS, "Tall Grass", T_TALLGRASS, T_TALLGRASS, T_TALLGRASS, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE, 0, TINT_GRASS);
    defBlock(B_TALLGRASS_DRY, "Dry Grass", T_TALLGRASS_DRY, T_TALLGRASS_DRY, T_TALLGRASS_DRY, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE, 0, TINT_GRASS);
    defBlock(B_FERN, "Fern", T_FERN, T_FERN, T_FERN, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE, 0, TINT_FOLIAGE);
    defBlock(B_FLOWER_RED, "Red Flower", T_FLOWER_RED, T_FLOWER_RED, T_FLOWER_RED, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE);
    defBlock(B_FLOWER_YELLOW, "Yellow Flower", T_FLOWER_YELLOW, T_FLOWER_YELLOW, T_FLOWER_YELLOW, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE);
    defBlock(B_FLOWER_BLUE, "Blue Flower", T_FLOWER_BLUE, T_FLOWER_BLUE, T_FLOWER_BLUE, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE);
    defBlock(B_FLOWER_PURPLE, "Purple Flower", T_FLOWER_PURPLE, T_FLOWER_PURPLE, T_FLOWER_PURPLE, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE);
    defBlock(B_HEATHER, "Heather", T_HEATHER, T_HEATHER, T_HEATHER, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE);
    defBlock(B_CATTAIL, "Cattail", T_CATTAIL, T_CATTAIL, T_CATTAIL, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE);
    defBlock(B_REED, "Reed", T_REED, T_REED, T_REED, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE);
    defBlock(B_MUSHROOM_RED, "Red Mushroom", T_MUSHROOM_RED, T_MUSHROOM_RED, T_MUSHROOM_RED, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE);
    defBlock(B_MUSHROOM_BROWN, "Brown Mushroom", T_MUSHROOM_BROWN, T_MUSHROOM_BROWN, T_MUSHROOM_BROWN, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE);
    defBlock(B_BERRYBUSH, "Berry Bush", T_BERRYBUSH, T_BERRYBUSH, T_BERRYBUSH, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 2, TOOL_NONE);
    defBlock(B_DEADBUSH, "Dead Bush", T_DEADBUSH, T_DEADBUSH, T_DEADBUSH, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE);
    defBlock(B_SAPLING, "Sapling", T_SAPLING, T_SAPLING, T_SAPLING, BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE);
    defBlock(B_CACTUS, "Cactus", T_CACTUS_S, T_CACTUS_T, T_CACTUS_T, SLD, 8, TOOL_NONE);
    for (int i = 0; i < 6; i++)
        defBlock((BlockId)(B_CROP_0 + i), "Crop", T_CROP_0 + i, T_CROP_0 + i, T_CROP_0 + i,
                 BF_CROSS | BF_PLANT | BF_REPLACEABLE, 1, TOOL_NONE);
    // misc
    defBlock(B_COBBLE_GREY, "Cobblestone", T_COBBLE_GREY, T_COBBLE_GREY, T_COBBLE_GREY, SLD, 45, TOOL_PICK);
    defBlock(B_COBBLE_WARM, "Cobblestone", T_COBBLE_WARM, T_COBBLE_WARM, T_COBBLE_WARM, SLD, 45, TOOL_PICK);
    defBlock(B_COBBLE_DARK, "Cobblestone", T_COBBLE_DARK, T_COBBLE_DARK, T_COBBLE_DARK, SLD, 45, TOOL_PICK);
    defBlock(B_BRICKS, "Bricks", T_BRICKS, T_BRICKS, T_BRICKS, SLD, 45, TOOL_PICK);
    defBlock(B_STONE_BRICK, "Stone Bricks", T_STONE_BRICK, T_STONE_BRICK, T_STONE_BRICK, SLD, 50, TOOL_PICK);
    defBlock(B_POLISHED, "Polished Stone", T_POLISHED, T_POLISHED, T_POLISHED, SLD, 50, TOOL_PICK);
    defBlock(B_TORCH, "Torch", T_TORCH, T_TORCH, T_TORCH, BF_CROSS | BF_PLANT, 1, TOOL_NONE, 14);
    defBlock(B_FIRE, "Fire", T_FIRE, T_FIRE, T_FIRE, BF_CROSS | BF_TRANSLUCENT, 1, TOOL_NONE, 15);
    defBlock(B_GLASS, "Glass", T_GLASS, T_GLASS, T_GLASS, BF_SOLID | BF_TRANSLUCENT, 4, TOOL_NONE);
    defBlock(B_THATCH, "Thatch", T_THATCH, T_THATCH, T_THATCH, SLD, 12, TOOL_NONE);
    defBlock(B_CHARCOAL, "Charcoal Pile", T_CHARCOAL, T_CHARCOAL, T_CHARCOAL, SLD, 25, TOOL_SHOVEL);
    defBlock(B_ICE, "Ice", T_ICE, T_ICE, T_ICE, BF_SOLID | BF_TRANSLUCENT, 8, TOOL_PICK);
    defBlock(B_WATER, "Water", T_WATER, T_WATER, T_WATER, BF_LIQUID | BF_TRANSLUCENT | BF_REPLACEABLE | BF_NOCOLLIDE, 255, TOOL_NONE);
    defBlock(B_LAVA, "Lava", T_LAVA, T_LAVA, T_LAVA, BF_LIQUID | BF_REPLACEABLE | BF_NOCOLLIDE, 255, TOOL_NONE, 12);
    defBlock(B_CRATE, "Crate", T_CRATE, T_CRATE, T_CRATE, SLD, 20, TOOL_AXE);
    defBlock(B_FIREPIT, "Firepit", T_FIREPIT, T_FIREPIT, T_FIREPIT, BF_SOLID | BF_NOCOLLIDE, 30, TOOL_PICK, 10);
    defBlock(B_SLAG, "Slag", T_SLAG, T_SLAG, T_SLAG, SLD, 40, TOOL_PICK);
    defBlock(B_BLOOM, "Bloomery", T_BLOOM, T_BLOOM, T_BLOOM, SLD, 45, TOOL_PICK, 6);
    defBlock(B_SCAFFOLD, "Scaffold", T_SCAFFOLD, T_SCAFFOLD, T_SCAFFOLD, BF_SOLID | BF_TRANSLUCENT, 6, TOOL_AXE);
    // derived block properties
    g_blocks[B_GRASS].flags = SLD;
    g_blocks[B_SAND].flags |= BF_SOLID;
}

// ---------------------------------------------------------------- items
static ItemDef s_blockItems[B_COUNT];
static ItemDef s_toolItems[64];
static int s_toolCount = 0;

static void addTool(u16 id, const char* name, int tex, u8 tool, u8 tier, u16 dmg, u16 dur) {
    ItemDef& d = s_toolItems[s_toolCount++];
    d.name = name; d.tex = tex; d.tool = tool; d.tier = tier; d.damage = dmg;
    d.durability = dur; d.stack = 1; d.food = 0; d.placeBlock = 0; d.isBlock = false;
    int ix = itemIndex(id); if (ix >= 0) s_itemCache[ix] = &d;
}

void initItems() {
    for (int i = 0; i < ITEM_CACHE_SIZE; i++) s_itemCache[i] = nullptr;
    for (int i = 0; i < B_COUNT; i++) {
        ItemDef& d = s_blockItems[i];
        d.name = g_blocks[i].name ? g_blocks[i].name : "Air";
        d.tex = g_blocks[i].texSide;
        d.tool = TOOL_NONE; d.tier = 0; d.damage = 0; d.durability = 0;
        d.stack = 100; d.food = 0; d.placeBlock = (u8)(i < 256 ? i : 0); d.isBlock = true;
        if (i >= 256) d.placeBlock = 0;
    }
    const u16 tierDur[4] = { 0, 120, 260, 600 };
    const u16 tierDmg[4] = { 0, 3, 5, 8 };
    const char* metalNames[3] = { "Copper", "Bronze", "Iron" };
    for (int tier = 0; tier < 3; tier++) {
        for (int shape = 0; shape < 8; shape++) {
            static const char* shapes[8] = { "Axe", "Pickaxe", "Shovel", "Knife", "Sword", "Hammer", "Prospecting Pick", "Chisel" };
            static const u8 tools[8] = { TOOL_AXE, TOOL_PICK, TOOL_SHOVEL, TOOL_KNIFE, TOOL_NONE, TOOL_HAMMER, TOOL_PICK, TOOL_PICK };
            char* nm = (char*)malloc(48);
            snprintf(nm, 48, "%s %s", metalNames[tier], shapes[shape]);
            u16 id = (u16)(I_AXE_COPPER + tier * 0x10 + shape);
            addTool(id, nm, T_I_AXE_C + tier * 8 + shape, tools[shape], (u8)(tier + 1),
                    tierDmg[tier + 1], tierDur[tier + 1]);
        }
    }
    struct Simple { u16 id; const char* name; int tex; u8 stack; u8 food; u8 place; };
    const Simple simples[] = {
        { I_STICK, "Stick", T_I_STICK, 64, 0, 0 }, { I_FLINT, "Flint", T_I_FLINT, 64, 0, 0 },
        { I_KNIFE_FLINT, "Flint Knife", T_I_KNIFE_FLINT, 1, 0, 0 },
        { I_AXE_FLINT, "Flint Axe", T_I_AXE_FLINT, 1, 0, 0 },
        { I_SHOVEL_FLINT, "Flint Shovel", T_I_SHOVEL_FLINT, 1, 0, 0 },
        { I_PICK_FLINT, "Flint Pickaxe", T_I_PICK_FLINT, 1, 0, 0 },
        { I_NUGGET_COPPER, "Copper Nugget", T_I_NUGGET, 64, 0, 0 },
        { I_NUGGET_GOLD, "Gold Nugget", T_I_NUGGET, 64, 0, 0 },
        { I_NUGGET_IRON, "Iron Nugget", T_I_NUGGET, 64, 0, 0 },
        { I_INGOT_COPPER, "Copper Ingot", T_I_INGOT, 16, 0, 0 },
        { I_INGOT_BRONZE, "Bronze Ingot", T_I_INGOT, 16, 0, 0 },
        { I_INGOT_IRON, "Iron Ingot", T_I_INGOT, 16, 0, 0 },
        { I_INGOT_TIN, "Tin Ingot", T_I_INGOT, 16, 0, 0 },
        { I_SEEDS, "Seeds", T_I_SEEDS, 64, 0, 0 }, { I_BERRIES, "Berries", T_I_BERRIES, 64, 6, 0 },
        { I_MEAT_RAW, "Raw Meat", T_I_MEAT, 32, 8, 0 }, { I_MEAT_COOKED, "Cooked Meat", T_I_MEAT, 32, 22, 0 },
        { I_CLAYBALL, "Clay", T_I_CLAYBALL, 64, 0, 0 }, { I_BOWL, "Bowl", T_I_BOWL, 8, 0, 0 },
        { I_POT, "Cooking Pot", T_I_POT, 4, 0, 0 }, { I_ROPE, "Rope", T_I_ROPE, 32, 0, 0 },
        { I_CROP, "Flax", T_I_CROPS, 64, 0, 0 }, { I_BREAD, "Bread", T_I_BREAD, 16, 30, 0 },
        { I_STEW, "Stew", T_I_STEW, 4, 40, 0 }, { I_TALLOW, "Tallow", T_I_TALLOW, 32, 0, 0 },
        { I_COAL, "Coal", T_I_COAL, 64, 0, 0 }, { I_GEAR, "Gear", T_I_GEAR, 8, 0, 0 },
        { I_ARROWHEAD, "Arrowhead", T_I_ARROWHEAD, 32, 0, 0 },
    };
    for (const auto& s : simples) {
        ItemDef& d = s_toolItems[s_toolCount++];
        d.name = s.name; d.tex = s.tex; d.tool = TOOL_NONE; d.tier = 0; d.damage = 0;
        d.durability = 0; d.stack = s.stack; d.food = s.food; d.placeBlock = s.place; d.isBlock = false;
        int ix = itemIndex(s.id); if (ix >= 0) s_itemCache[ix] = &d;
    }
}

ItemDef* itemDef(u16 id) {
    if (id < B_COUNT) return &s_blockItems[id];
    int ix = itemIndex(id);
    return ix >= 0 ? s_itemCache[ix] : nullptr;
}
bool itemIsBlock(u16 id) { return id < B_COUNT && id != B_AIR; }
u16 blockToItem(u16 b) { return b; }
int itemTexture(u16 id) {
    ItemDef* d = itemDef(id);
    if (d) return d->tex;
    return T_I_STICK;
}
