// vb_worldgen.cpp : climate driven terrain, rock strata, ores, caves, features
#include "vb_world.h"

// ------------------------------------------------------------------ biomes
// temperature / rainfall driven, like Vintage Story's climate maps
enum BiomeId {
    BI_ICE_TUNDRA = 0, BI_TAIGA, BI_BOREAL, BI_TEMPERATE_FOREST, BI_PLAINS, BI_STEPPE,
    BI_DESERT, BI_SAVANNA, BI_SUBTROP, BI_RAINFOREST, BI_OCEAN, BI_COUNT
};

struct BiomeDef { const char* name; int grass; int dry; int soil; u8 treeA, treeB, treeC; int temp; };

// trees: B_LOG_* ids
static const BiomeDef s_biomes[BI_COUNT] = {
    { "Ice Tundra",   0xa9b49b, 0xb5bda4, B_DIRT,      B_LOG_PINE, B_LOG_PINE, B_LOG_BIRCH, 0 },
    { "Taiga",        0x6f8059, 0x7c8c64, B_DIRT,      B_LOG_PINE, B_LOG_PINE, B_LOG_BIRCH, 15 },
    { "Boreal Forest",0x718450, 0x7e9058, B_DIRT,      B_LOG_PINE, B_LOG_BIRCH, B_LOG_MAPLE, 30 },
    { "Temperate Forest", 0x7d9058, 0x8a9a62, B_DIRT,  B_LOG_OAK, B_LOG_BIRCH, B_LOG_MAPLE, 50 },
    { "Plains",       0x8b9c5d, 0x98a866, B_DIRT,      B_LOG_OAK, B_LOG_MAPLE, B_LOG_OAK, 55 },
    { "Steppe",       0x9c9d64, 0xaaa86c, B_DIRT,      B_LOG_ACACIA, B_LOG_OAK, B_LOG_OAK, 60 },
    { "Desert",       0xab9c69, 0xb6a774, B_SAND,      B_LOG_ACACIA, B_LOG_ACACIA, B_LOG_ACACIA, 90 },
    { "Savanna",      0x9e9a60, 0xaba569, B_DIRT,      B_LOG_ACACIA, B_LOG_ACACIA, B_LOG_OAK, 80 },
    { "Subtropical",  0x759050, 0x839a5c, B_DIRT,      B_LOG_ACACIA, B_LOG_KAPOK, B_LOG_OAK, 78 },
    { "Rainforest",   0x5b7f4b, 0x678a56, B_DIRT,      B_LOG_KAPOK, B_LOG_KAPOK, B_LOG_ACACIA, 92 },
    { "Ocean",        0x7d9058, 0x8a9a62, B_SAND,      B_LOG_OAK, B_LOG_OAK, B_LOG_OAK, 50 },
};
int biomeCount() { return BI_COUNT; }
const char* biomeName(int id) { return s_biomes[clampi(id, 0, BI_COUNT - 1)].name; }
int biomeTemp(int id) { return s_biomes[clampi(id, 0, BI_COUNT - 1)].temp; }

int biomeAt(float temp, float rain, float h01) {
    (void)h01;
    if (temp < 0.18f) return BI_ICE_TUNDRA;
    if (temp < 0.33f) return rain > 0.32f ? BI_TAIGA : BI_STEPPE;
    if (temp < 0.46f) return rain > 0.42f ? BI_BOREAL : BI_STEPPE;
    if (temp < 0.64f) {
        if (rain > 0.55f) return BI_TEMPERATE_FOREST;
        if (rain > 0.36f) return BI_PLAINS;
        return BI_STEPPE;
    }
    if (temp < 0.78f) {
        if (rain > 0.52f) return BI_SUBTROP;
        if (rain > 0.30f) return BI_SAVANNA;
        return BI_DESERT;
    }
    if (rain > 0.45f) return BI_RAINFOREST;
    return BI_DESERT;
}

// ------------------------------------------------------------------ column sample
struct TerrainGen {
    Noise nCont, nHill, nMount, nRidge, nRiver, nDetail, nRock, nTemp, nRain, nCave1, nCave2, nCave3, nTerr, nForest;
    u64 seed = 0;
    void init(u64 s) {
        seed = s;
        nCont.init((u32)s * 2 + 1);  nHill.init((u32)s * 2 + 3);   nMount.init((u32)s * 2 + 5);
        nRidge.init((u32)s * 2 + 7); nRiver.init((u32)s * 2 + 9);  nDetail.init((u32)s * 2 + 11);
        nRock.init((u32)s * 2 + 13); nTemp.init((u32)s * 2 + 15);  nRain.init((u32)s * 2 + 17);
        nCave1.init((u32)s * 2 + 19); nCave2.init((u32)s * 2 + 21); nCave3.init((u32)s * 2 + 27);
        nTerr.init((u32)s * 2 + 23);
        nForest.init((u32)s * 2 + 25);
    }
};

struct Col {
    int height = 0;        // y of the topmost solid block
    int biome = BI_PLAINS;
    u16 top = B_GRASS, fill = B_DIRT, rock = B_GRANITE;
    float temp = 0.5f, rain = 0.5f;
    bool ocean = false;
};

// rock region -> base strata sequence
static const u16 ROCK_IDS[10] = { B_GRANITE, B_ANDESITE, B_BASALT, B_LIMESTONE, B_CHALK,
                                  B_SLATE, B_SANDSTONE, B_PERIDOTITE, B_KIMBERLITE, B_SHALE };

static float terrNoise(const TerrainGen& g, float x, float z) {
    return g.nTerr.n2(x * 0.0045f, z * 0.0045f);
}

static float rawHeight(const TerrainGen& g, float x, float z) {
    float cont = g.nCont.fbm2(x * 0.00075f, z * 0.00075f, 4);
    float e = 124.f + cont * 52.f;
    // mountain belts
    float mask = smoothstepf(-0.08f, 0.42f, g.nMount.fbm2(x * 0.0011f + 31.f, z * 0.0011f - 77.f, 3) * 1.9f);
    if (mask > 0.001f) {
        float rid = g.nRidge.ridged2(x * 0.0016f, z * 0.0016f, 4);
        e += mask * (0.30f + 0.70f * (rid * 0.5f + 0.5f)) * 84.f;
    }
    // rolling hills
    e += g.nHill.fbm2(x * 0.0062f, z * 0.0062f, 3) * 9.f;
    e += g.nDetail.fbm2(x * 0.031f, z * 0.031f, 2) * 1.6f;
    // river valleys
    float r = fabsf(g.nRiver.fbm2(x * 0.0021f, z * 0.0021f, 3));
    float riverMask = 1.f - smoothstepf(0.f, 0.022f, r);
    e = lerpf(e, (float)(SEA_LEVEL - 5), riverMask * 0.9f);
    // terracing : flat plateaus separated by short cliffs (the Vintage Story look)
    float stepN = terrNoise(g, x, z);
    float step = 1.f + (stepN * 0.5f + 0.5f) * 1.3f;
    if (mask < 0.35f) {
        float q = e / step;
        float fq = floorf(q);
        float fr = q - fq;
        e = (fq + smoothstepf(0.68f, 1.0f, fr)) * step;
    }
    return e;
}

static Col sampleColumn(const TerrainGen& g, int x, int z) {
    Col c;
    float e = rawHeight(g, (float)x, (float)z);
    c.height = clampi((int)floorf(e), 4, WH - 20);

    // climate
    float tnoise = g.nTemp.fbm2(x * 0.0013f + 55.f, z * 0.0013f - 21.f, 3);
    float rnoise = g.nRain.fbm2(x * 0.0017f - 13.f, z * 0.0017f + 44.f, 3);
    c.temp = clampf(0.5f + tnoise * 0.95f, 0.f, 1.f);
    c.rain = clampf(0.5f + rnoise * 1.05f, 0.f, 1.f);
    // altitude cooling
    c.temp -= imax(0, c.height - 128) * 0.0026f;
    c.temp = clampf(c.temp, 0.f, 1.f);
    c.biome = (c.height <= SEA_LEVEL - 1) ? BI_OCEAN : biomeAt(c.temp, c.rain, c.height / 256.f);

    // rock strata : dipping bands below a per-region base rock
    u32 cell = 0;
    g.nRock.worley2(x * 0.0042f, z * 0.0042f, 77u, &cell);
    int regionRock = (int)(cell % 10);
    float dip = g.nRock.fbm2(x * 0.011f, z * 0.011f, 3) * 14.f
              + 8.f * sinf(x * 0.013f) + 5.f * sinf(z * 0.017f);
    // we compute the rock for the surface here; deeper layers are interpolated below
    c.rock = ROCK_IDS[(int)((regionRock + 10 - (int)floorf((c.height - dip) / 26.f) % 10)) % 10];

    // surface material
    const BiomeDef& b = s_biomes[c.biome];
    c.top = B_GRASS; c.fill = B_DIRT;
    if (c.biome == BI_DESERT) { c.top = B_SAND; c.fill = B_SANDSTONE; }
    else if (c.biome == BI_SAVANNA || c.biome == BI_STEPPE) { c.top = B_GRASS_DRY; c.fill = B_DIRT; }
    else if (c.biome == BI_ICE_TUNDRA) { c.top = B_GRASS_SNOW; c.fill = B_PERMAFROST; }
    else if (c.biome == BI_RAINFOREST || c.biome == BI_SUBTROP) { c.top = B_GRASS; c.fill = B_DIRT; }
    else if (c.biome == BI_OCEAN) { c.top = B_SAND; c.fill = B_SAND; }
    (void)b;

    // beaches & river beds
    if (c.height <= SEA_LEVEL + 1 && c.height >= SEA_LEVEL - 6) {
        if (c.temp < 0.2f) { c.top = B_GRAVEL; c.fill = B_GRAVEL; }
        else { c.top = B_SAND; c.fill = B_SAND; }
    }
    // snow above the snow line
    if (c.height > 150 && c.temp < 0.34f) { c.top = B_GRASS_SNOW; c.fill = B_PERMAFROST; }
    if (c.height > 168) { c.top = B_SNOW; c.fill = B_PERMAFROST; }
    return c;
}

static u32 tintFor(const Col& c) {
    const BiomeDef& b = s_biomes[c.biome];
    u32 base = c.top == B_GRASS_DRY ? (u32)b.dry : (u32)b.grass;
    return base;
}

// ------------------------------------------------------------------ ore setup
struct OreType { u16 block; int rockMask; int minY, maxY; float freq; };
// rockMask bits: which rocks host this ore (bit index = ROCK_IDS index)
static const OreType ORES[] = {
    { B_ORE_COPPER,    0x2E7, 20, 180, 1.00f },  // granite andesite basalt slate sandstone shale
    { B_ORE_TIN,       0x001, 30, 150, 0.45f },  // granite
    { B_ORE_LEAD,      0x018, 20, 140, 0.55f },  // limestone chalk
    { B_ORE_ZINC,      0x0C8, 20, 150, 0.50f },  // limestone slate shale
    { B_ORE_IRON,      0x301, 10, 160, 0.85f },  // granite andesite
    { B_ORE_MAGNETITE, 0x088, 5,  120, 0.40f },  // basalt peridotite
    { B_ORE_GOLD,      0x003, 5,  90,  0.22f },  // granite andesite
    { B_ORE_SILVER,    0x010, 10, 110, 0.30f },  // limestone
    { B_ORE_COAL,      0x2A0, 15, 150, 0.90f },  // limestone slate sandstone shale
    { B_ORE_LIGNITE,   0x280, 20, 120, 0.70f },  // limestone sandstone shale
    { B_ORE_QUARTZ,    0x002, 5,  160, 0.35f },  // andesite
};
static const int ORE_COUNT = (int)(sizeof(ORES) / sizeof(ORES[0]));

static int rockBit(const Col& c) {
    for (int i = 0; i < 10; i++) if (ROCK_IDS[i] == c.rock) return i;
    return 0;
}

// ------------------------------------------------------------------ helpers
static inline int bufIdx(int x, int y, int z) { return (y * CH + z) * CH + x; }

struct TreeShape { int trunkHeight; int canopyR; int canopyH; bool conical; };

static TreeShape treeFor(u16 log) {
    switch (log) {
    case B_LOG_PINE: return { 13, 3, 6, true };
    case B_LOG_OAK: return { 9, 3, 3, false };
    case B_LOG_BIRCH: return { 10, 2, 3, false };
    case B_LOG_MAPLE: return { 8, 3, 3, false };
    case B_LOG_ACACIA: return { 7, 4, 2, false };
    case B_LOG_KAPOK: return { 18, 4, 4, false };
    default: return { 7, 3, 3, false };
    }
}

static u16 leafFor(u16 log) {
    switch (log) {
    case B_LOG_PINE: return B_LEAF_PINE;
    case B_LOG_OAK: return B_LEAF_OAK;
    case B_LOG_BIRCH: return B_LEAF_BIRCH;
    case B_LOG_MAPLE: return B_LEAF_MAPLE;
    case B_LOG_ACACIA: return B_LEAF_ACACIA;
    case B_LOG_KAPOK: return B_LEAF_KAPOK;
    default: return B_LEAF_OAK;
    }
}

static u16 plankFor(u16 log) {
    switch (log) {
    case B_LOG_PINE: return B_PLANK_PINE;
    case B_LOG_OAK: return B_PLANK_OAK;
    case B_LOG_BIRCH: return B_PLANK_BIRCH;
    case B_LOG_MAPLE: return B_PLANK_MAPLE;
    case B_LOG_ACACIA: return B_PLANK_ACACIA;
    default: return B_PLANK_KAPOK;
    }
}

// ------------------------------------------------------------------ chunk gen
struct GenCtx {
    World* w;
    Chunk* c;
    u16* buf;
    Rng rng;
    int baseX, baseZ;
    inline bool inChunk(int x, int z) const { return x >= baseX && x < baseX + CH && z >= baseZ && z < baseZ + CH; }
    inline void put(int x, int y, int z, u16 v, bool overwrite = true) {
        if (!inChunk(x, z) || y < 0 || y >= WH) return;
        int i = bufIdx(x - baseX, y, z - baseZ);
        if (!overwrite && buf[i] != B_AIR) return;
        buf[i] = v;
    }
    inline u16 getAt(int x, int y, int z) const {
        if (!inChunk(x, z) || y < 0 || y >= WH) return B_AIR;
        return buf[bufIdx(x - baseX, y, z - baseZ)];
    }
};

static void placeTree(GenCtx& g, int x, int y, int z, u16 log) {
    TreeShape t = treeFor(log);
    u16 leaf = leafFor(log);
    Rng r(hash3i(x, y, z, 991u));
    int th = t.trunkHeight + r.irange(-2, 2);
    if (th < 3) th = 3;
    // trunk
    for (int i = 0; i < th; i++) g.put(x, y + i, z, log + 0 * 0, false);
    if (t.conical) {
        int top = y + th;
        for (int layer = 0; layer < th - 2; layer++) {
            int ly = top - layer;
            int rad = 1 + (layer * (t.canopyR + 1)) / imax(1, (th - 3));
            if (rad > t.canopyR) rad = t.canopyR;
            for (int dz = -rad; dz <= rad; dz++)
                for (int dx = -rad; dx <= rad; dx++) {
                    if (abs(dx) + abs(dz) > rad + 1) continue;
                    if (dx == 0 && dz == 0 && layer < th - 3) continue;
                    g.put(x + dx, ly, z + dz, leaf, false);
                }
        }
        g.put(x, y + th, z, leaf, false);
    } else {
        int top = y + th - 1;
        int r0 = t.canopyR;
        for (int dy = -3; dy <= t.canopyH; dy++) {
            int yy = top + dy;
            int rad = r0 - (dy > t.canopyH - 2 ? 1 : 0);
            if (dy < 0) rad = imax(2, r0 - 1);
            for (int dz = -rad; dz <= rad; dz++)
                for (int dx = -rad; dx <= rad; dx++) {
                    int d2 = dx * dx + dz * dz;
                    if (d2 > rad * rad + 1) continue;
                    if (d2 > (rad - 1) * (rad - 1) && (hash3i(x + dx, yy, z + dz, 7u) & 3) == 0) continue;
                    g.put(x + dx, yy, z + dz, leaf, false);
                }
        }
        // a couple of branch logs for a less generic silhouette
        if (r.chance(0.5f) && t.canopyR >= 3) {
            int dir = r.irange(0, 3);
            int dx = (dir == 0) ? 1 : (dir == 1 ? -1 : 0);
            int dz = (dir == 2) ? 1 : (dir == 3 ? -1 : 0);
            g.put(x + dx, top - 1, z + dz, log, false);
            g.put(x + dx * 2, top - 1, z + dz * 2, log, false);
        }
    }
}

// surface boulders / rock outcrops
static void placeBoulder(GenCtx& g, int x, int y, int z, u16 rock, Rng& r) {
    int rad = r.irange(1, 2);
    for (int dy = -1; dy <= 1; dy++)
        for (int dz = -rad; dz <= rad; dz++)
            for (int dx = -rad; dx <= rad; dx++) {
                if (dx * dx + dz * dz + dy * dy * 2 > rad * rad + 1) continue;
                g.put(x + dx, y + dy, z + dz, rock, true);
            }
}

// roots / fallen logs
static void placeFallenLog(GenCtx& g, int x, int y, int z, u16 log, Rng& r) {
    int len = r.irange(3, 6);
    int dx = r.chance(0.5f) ? 1 : 0, dz = dx ? 0 : 1;
    for (int i = 0; i < len; i++) g.put(x + dx * i, y, z + dz * i, log, false);
}

void generateChunk(World& w, Chunk& c) {
    static TerrainGen g;
    static u64 gSeed = ~0ull;
    if (gSeed != w.seed) { g.init(w.seed); gSeed = w.seed; }

    static std::vector<u16> bufs;
    bufs.assign((size_t)CH * CH * WH, B_AIR);
    u16* buf = bufs.data();

    GenCtx gc;
    gc.w = &w; gc.c = &c; gc.buf = buf;
    gc.baseX = c.cx * CH; gc.baseZ = c.cz * CH;
    gc.rng = Rng(hash3i(c.cx, c.cz, 0, (u32)w.seed ^ 0x9E3779B9u));

    // ---- cave field sampled on an 8 block grid, trilinearly interpolated
    const int GX = CH / 8 + 1, GZ = CH / 8 + 1, GY = WH / 8 + 1;
    static std::vector<float> cav1, cav2, cav3;
    cav1.assign((size_t)GX * GY * GZ, 0.f);
    cav2.assign((size_t)GX * GY * GZ, 0.f);
    cav3.assign((size_t)GX * GY * GZ, 0.f);
    auto cIdx = [&](int gx, int gy, int gz) { return ((size_t)gy * GZ + gz) * GX + gx; };
    for (int gy = 0; gy < GY; gy++)
        for (int gz = 0; gz < GZ; gz++)
            for (int gx = 0; gx < GX; gx++) {
                float wx = (float)(gc.baseX + gx * 8), wy = (float)(gy * 8), wz = (float)(gc.baseZ + gz * 8);
                cav1[cIdx(gx, gy, gz)] = g.nCave1.n3(wx * 0.021f, wy * 0.038f, wz * 0.021f)
                                       + g.nCave1.n3(wx * 0.048f, wy * 0.07f, wz * 0.048f) * 0.4f;
                cav2[cIdx(gx, gy, gz)] = g.nCave2.n3(wx * 0.019f, wy * 0.031f, wz * 0.019f);
                cav3[cIdx(gx, gy, gz)] = g.nCave3.n3(wx * 0.011f, wy * 0.018f, wz * 0.011f);
            }

    // ---- column grid (chunk + 1 block border) for slope aware materials
    static std::vector<Col> cols;
    cols.assign((size_t)(CH + 2) * (CH + 2), Col());
    auto colAt = [&](int x, int z) -> Col& { return cols[(size_t)(z + 1) * (CH + 2) + (x + 1)]; };
    for (int z = -1; z <= CH; z++)
        for (int x = -1; x <= CH; x++)
            colAt(x, z) = sampleColumn(g, gc.baseX + x, gc.baseZ + z);

    // ---- columns
    for (int z = 0; z < CH; z++)
        for (int x = 0; x < CH; x++) {
            int wx = gc.baseX + x, wz = gc.baseZ + z;
            Col& col = colAt(x, z);
            int h = col.height;
            // slope decides whether we get soil or bare rock (Vintage Story style cliffs)
            int slope = 0;
            {
                static const int off[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
                for (auto& o : off) {
                    int nh = colAt(x + o[0], z + o[1]).height;
                    slope = imax(slope, abs(nh - h));
                }
            }
            u16 surfTop = col.top, surfFill = col.fill;
            int soilDepth = 2;
            if (slope >= 4) { surfTop = col.rock; surfFill = col.rock; soilDepth = 0; }
            else if (slope >= 3) { surfTop = col.rock; surfFill = col.fill; soilDepth = 1; }
            else if (slope == 2) { soilDepth = 1; }
            if (h > 186) { surfTop = col.rock; surfFill = col.rock; soilDepth = 0; }     // high peaks are bare
            if (surfTop == B_SAND) { surfFill = B_SANDSTONE; }
            c.tint[z * CH + x] = shadeColor(tintFor(col), 0.96f + 0.08f * ((hash2i(wx, wz, 7u) % 100) / 100.f));

            int rockIdx = rockBit(col);
            for (int y = 0; y <= h; y++) {
                u16 v;
                if (y <= 2) v = B_BEDROCK;
                else {
                    // strata banding
                    float dip = g.nRock.fbm2(wx * 0.011f, wz * 0.011f, 3) * 14.f
                              + 8.f * sinf(wx * 0.013f) + 5.f * sinf(wz * 0.017f);
                    int band = (int)floorf((y + dip) / 22.f);
                    v = ROCK_IDS[(int)(((u32)rockIdx + (u32)(band < 0 ? -band : band) * 3u) % 10u)];
                    if (y >= h - soilDepth) v = surfFill;
                    if (y == h) v = surfTop;
                }
                buf[bufIdx(x, y, z)] = v;
            }
            // water
            if (h < SEA_LEVEL) {
                for (int y = h + 1; y <= SEA_LEVEL; y++) buf[bufIdx(x, y, z)] = B_WATER;
                if (col.temp < 0.16f && h < SEA_LEVEL) buf[bufIdx(x, SEA_LEVEL, z)] = B_ICE;
            }
            // caves
            for (int y = 3; y < h - 2; y++) {
                float fx = x / 8.f, fy = y / 8.f, fz = z / 8.f;
                int i0x = (int)fx, i0y = (int)fy, i0z = (int)fz;
                float tx = fx - i0x, ty = fy - i0y, tz = fz - i0z;
                int i1x = imin(i0x + 1, GX - 1), i1y = imin(i0y + 1, GY - 1), i1z = imin(i0z + 1, GZ - 1);
                auto tri = [&](const std::vector<float>& f, int i0x, int i0y, int i0z, int i1x, int i1y, int i1z,
                               float tx, float ty, float tz) {
                    float a = lerpf(f[cIdx(i0x, i0y, i0z)], f[cIdx(i1x, i0y, i0z)], tx);
                    float b = lerpf(f[cIdx(i0x, i0y, i1z)], f[cIdx(i1x, i0y, i1z)], tx);
                    float c = lerpf(f[cIdx(i0x, i1y, i0z)], f[cIdx(i1x, i1y, i0z)], tx);
                    float d = lerpf(f[cIdx(i0x, i1y, i1z)], f[cIdx(i1x, i1y, i1z)], tx);
                    return lerpf(lerpf(a, b, tz), lerpf(c, d, tz), ty);
                };
                float v1 = tri(cav1, i0x, i0y, i0z, i1x, i1y, i1z, tx, ty, tz);
                float v2 = tri(cav2, i0x, i0y, i0z, i1x, i1y, i1z, tx, ty, tz);
                float v3 = tri(cav3, i0x, i0y, i0z, i1x, i1y, i1z, tx, ty, tz);
                bool cave = (fabsf(v1) < 0.075f && fabsf(v2) < 0.075f) || (v3 > 0.42f);
                if (cave && buf[bufIdx(x, y, z)] != B_BEDROCK) buf[bufIdx(x, y, z)] = B_AIR;
            }
            // expose caves to the surface occasionally
            if (h > SEA_LEVEL + 3) {
                for (int y = h - 2; y <= h; y++) {
                    float v1 = g.nCave1.n3(wx * 0.022f, y * 0.038f, wz * 0.022f);
                    if (fabsf(v1) < 0.02f) buf[bufIdx(x, y, z)] = B_AIR;
                }
            }
        }

    // ---- ore veins (blobs placed per chunk, gated by host rock)
    int veinCount = 26;
    for (int i = 0; i < veinCount; i++) {
        int vx = gc.rng.irange(-8, CH + 7);
        int vz = gc.rng.irange(-8, CH + 7);
        int vy = gc.rng.irange(4, 150);
        const OreType& ot = ORES[gc.rng.irange(0, ORE_COUNT - 1)];
        if (vy < ot.minY || vy > ot.maxY) continue;
        Col col = sampleColumn(g, gc.baseX + clampi(vx, 0, CH - 1), gc.baseZ + clampi(vz, 0, CH - 1));
        if (!(ot.rockMask & (1 << rockBit(col)))) continue;
        int rad = gc.rng.irange(1, 3);
        int len = gc.rng.irange(3, 9);
        int dx = gc.rng.chance(0.5f) ? 1 : 0, dz = dx ? 0 : 1;
        for (int s = 0; s < len; s++) {
            int cx = vx + dx * s, cz = vz + dz * s;
            int cy = vy + (int)(sinf(s * 1.3f) * 1.5f);
            for (int ddy = -rad; ddy <= rad; ddy++)
                for (int ddz = -rad; ddz <= rad; ddz++)
                    for (int ddx = -rad; ddx <= rad; ddx++) {
                        if (ddx * ddx + ddy * ddy + ddz * ddz > rad * rad) continue;
                        if (hash3i(cx + ddx, cy + ddy, cz + ddz, 33u) & 7) continue;
                        u16 cur = gc.getAt(cx + ddx, cy + ddy, cz + ddz);
                        if (blockId(cur) == B_AIR) continue;
                        if (bIsOpaque(cur) && !(g_blocks[blockId(cur)].flags & BF_LEAF))
                            gc.put(cx + ddx, cy + ddy, cz + ddz, ot.block, true);
                    }
        }
    }

    // ---- surface features : plants, boulders, trees, surface ore
    for (int z = 0; z < CH; z++)
        for (int x = 0; x < CH; x++) {
            int wx = gc.baseX + x, wz = gc.baseZ + z;
            int h = WH - 1;
            while (h > 0 && buf[bufIdx(x, h, z)] == B_AIR) h--;
            u16 top = buf[bufIdx(x, h, z)];
            if (top == B_WATER || top == B_ICE) continue;
            if (!bIsSolid(top) && top != B_AIR) continue;
            u32 ph = hash2i(wx, wz, (u32)w.seed ^ 0x51ed270bu);
            Col col = sampleColumn(g, wx, wz);

            // plants
            float grassChance = 0.10f;
            switch (col.biome) {
            case BI_PLAINS: grassChance = 0.20f; break;
            case BI_TEMPERATE_FOREST: grassChance = 0.18f; break;
            case BI_BOREAL: grassChance = 0.15f; break;
            case BI_TAIGA: grassChance = 0.18f; break;
            case BI_SAVANNA: grassChance = 0.26f; break;
            case BI_STEPPE: grassChance = 0.14f; break;
            case BI_DESERT: grassChance = 0.02f; break;
            case BI_RAINFOREST: grassChance = 0.24f; break;
            case BI_SUBTROP: grassChance = 0.18f; break;
            case BI_ICE_TUNDRA: grassChance = 0.06f; break;
            default: break;
            }
            if (top == B_GRASS || top == B_GRASS_DRY || top == B_GRASS_SNOW || top == B_DIRT) {
                float f = (ph % 1000) / 1000.f;
                if (f < grassChance) {
                    u16 plant = (col.biome == BI_SAVANNA || col.biome == BI_STEPPE) ? B_TALLGRASS_DRY
                              : (col.biome == BI_RAINFOREST ? B_FERN : B_TALLGRASS);
                    buf[bufIdx(x, h + 1, z)] = plant;
                } else if (f > grassChance && f < grassChance + 0.022f) {
                    u32 r = (ph >> 8) % 100;
                    u16 fl = r < 35 ? B_FLOWER_RED : (r < 60 ? B_FLOWER_YELLOW : (r < 82 ? B_FLOWER_BLUE : B_FLOWER_PURPLE));
                    if (col.biome == BI_PLAINS || col.biome == BI_TEMPERATE_FOREST || col.biome == BI_SUBTROP
                        || col.biome == BI_BOREAL || col.biome == BI_RAINFOREST)
                        buf[bufIdx(x, h + 1, z)] = fl;
                    else if (col.biome == BI_TAIGA && r < 30) buf[bufIdx(x, h + 1, z)] = B_HEATHER;
                } else if (f > 0.44f && f < 0.452f) {
                    if ((ph >> 4) % 3 == 0) buf[bufIdx(x, h + 1, z)] = B_BERRYBUSH;
                }
            }
            if (col.biome == BI_DESERT) {
                float f = (ph % 1000) / 1000.f;
                if (f < 0.006f && top == B_SAND) {
                    int th = 2 + (int)((ph >> 6) % 3);
                    for (int i = 1; i <= th; i++) if (h + i < WH) buf[bufIdx(x, h + i, z)] = B_CACTUS;
                } else if (f > 0.02f && f < 0.028f && top == B_SAND) buf[bufIdx(x, h + 1, z)] = B_DEADBUSH;
            }
            // reeds at the water line
            if (h + 1 == SEA_LEVEL && (top == B_SAND || top == B_GRAVEL || top == B_GRASS)) {
                float f = (ph % 1000) / 1000.f;
                if (f < 0.10f) buf[bufIdx(x, h + 1, z)] = (f < 0.05f) ? B_CATTAIL : B_REED;
            }
            // boulders
            float bf = ((ph >> 3) % 4000) / 4000.f;
            if (bf < 0.0007f && top != B_SAND && h > SEA_LEVEL) {
                Rng r(ph);
                placeBoulder(gc, wx, h + 1, wz, col.rock, r);
            }
            // surface ore hints (nuggets weathering out of the ground above a vein)
            if (bf > 0.0007f && bf < 0.0011f && col.height > SEA_LEVEL) {
                for (int i = 0; i < ORE_COUNT; i++)
                    if (ORES[i].rockMask & (1 << rockBit(col))) {
                        gc.put(wx, h, wz, ORES[(ph >> 7) % ORE_COUNT].block, true);
                        break;
                    }
            }
        }

    // ---- trees (cells are scanned beyond the chunk so canopies cross borders)
    int cellSize = 9;
    for (int gz = (gc.baseZ - 8) / cellSize; gz <= (gc.baseZ + CH + 8) / cellSize; gz++)
        for (int gx = (gc.baseX - 8) / cellSize; gx <= (gc.baseX + CH + 8) / cellSize; gx++) {
            u32 hh = hash2i(gx, gz, (u32)w.seed ^ 0x7f4a7c15u);
            Rng r(hh);
            int tx = gx * cellSize + r.irange(1, cellSize - 2);
            int tz = gz * cellSize + r.irange(1, cellSize - 2);
            if (tx < gc.baseX - 6 || tx > gc.baseX + CH + 6) continue;
            if (tz < gc.baseZ - 6 || tz > gc.baseZ + CH + 6) continue;
            if (!w.inside(tx, tz)) continue;
            Col col = sampleColumn(g, tx, tz);
            if (col.height <= SEA_LEVEL + 1) continue;
            float forest = g.nForest.fbm2(tx * 0.0075f, tz * 0.0075f, 3) * 0.5f + 0.5f;
            float density = 0.f;
            switch (col.biome) {
            case BI_RAINFOREST: density = 0.70f; break;
            case BI_TEMPERATE_FOREST: density = 0.52f; break;
            case BI_BOREAL: density = 0.50f; break;
            case BI_TAIGA: density = 0.42f; break;
            case BI_SUBTROP: density = 0.35f; break;
            case BI_PLAINS: density = 0.06f; break;
            case BI_SAVANNA: density = 0.10f; break;
            case BI_STEPPE: density = 0.05f; break;
            case BI_DESERT: density = 0.02f; break;
            case BI_ICE_TUNDRA: density = 0.08f; break;
            default: break;
            }
            if (r.f() > density * (0.35f + forest * 0.9f)) continue;
            u16 log = col.biome == BI_DESERT ? B_LOG_ACACIA
                    : (col.biome == BI_TAIGA || col.biome == BI_BOREAL || col.biome == BI_ICE_TUNDRA) ? B_LOG_PINE
                    : (col.biome == BI_RAINFOREST ? B_LOG_KAPOK
                    : (col.biome == BI_TEMPERATE_FOREST ? (r.chance(0.5f) ? B_LOG_OAK : (r.chance(0.5f) ? B_LOG_BIRCH : B_LOG_MAPLE))
                    : (r.chance(0.6f) ? B_LOG_OAK : B_LOG_BIRCH)));
            if (col.biome == BI_TAIGA && r.chance(0.25f)) log = B_LOG_BIRCH;
            // ground height at the trunk
            int gy = col.height;
            placeTree(gc, tx, gy + 1, tz, log);
            if (r.chance(0.06f)) placeFallenLog(gc, tx + 3, gy + 1, tz, log, r);
        }

    // ---- pack the buffer into sections
    u16 seccol[4096];
    for (int sy = 0; sy < SEC_Y; sy++)
        for (int sz = 0; sz < SEC_PER_CH; sz++)
            for (int sx = 0; sx < SEC_PER_CH; sx++) {
                bool any = false;
                for (int y = 0; y < SEC; y++) {
                    for (int z = 0; z < SEC; z++) {
                        for (int x = 0; x < SEC; x++) {
                            u16 v = buf[bufIdx(sx * SEC + x, sy * SEC + y, sz * SEC + z)];
                            seccol[(y * SEC + z) * SEC + x] = v;
                            if (v != B_AIR) any = true;
                        }
                    }
                }
                if (!any) { c.sec[sy][sz][sx] = nullptr; continue; }
                Section* s = new Section();
                s->packFrom(seccol);
                c.sec[sy][sz][sx] = s;
            }
}

// ------------------------------------------------------------------ coarse map
void buildCoarseMap(World& w) {
    TerrainGen g;
    g.init(w.seed);
    int res = 4;
    int cells = w.size / res;
    CoarseMap& m = w.coarse;
    m.size = w.size; m.res = res; m.cells = cells;
    m.height.assign((size_t)cells * cells, 0);
    m.color.assign((size_t)cells * cells, 0xFF00FF00);
    m.surf.assign((size_t)cells * cells, 0);
    m.biome.assign((size_t)cells * cells, (u8)BI_PLAINS);
    for (int cz = 0; cz < cells; cz++)
        for (int cx = 0; cx < cells; cx++) {
            int wx = cx * res + res / 2, wz = cz * res + res / 2;
            Col col = sampleColumn(g, wx, wz);
            int h = col.height;
            // tree canopy approximation so the LOD matches the real world
            float forest = g.nForest.fbm2(wx * 0.0075f, wz * 0.0075f, 3) * 0.5f + 0.5f;
            float density = 0.f;
            switch (col.biome) {
            case BI_RAINFOREST: density = 0.70f; break;
            case BI_TEMPERATE_FOREST: density = 0.52f; break;
            case BI_BOREAL: density = 0.50f; break;
            case BI_TAIGA: density = 0.42f; break;
            case BI_SUBTROP: density = 0.35f; break;
            case BI_PLAINS: density = 0.06f; break;
            case BI_SAVANNA: density = 0.10f; break;
            case BI_STEPPE: density = 0.05f; break;
            case BI_DESERT: density = 0.02f; break;
            case BI_ICE_TUNDRA: density = 0.08f; break;
            default: break;
            }
            float treeProb = saturate(density * (0.35f + forest * 0.9f));
            bool hasTree = h > SEA_LEVEL + 1 && treeProb > 0.25f;
            int topY = h;
            u32 col1 = textureAvgColor(g_blocks[col.top].texTop);
            if (g_blocks[col.top].tint == TINT_GRASS) col1 = shadeColor(col1, 1.f) ;
            if (g_blocks[col.top].tint) col1 = lerpColor(col1, shadeColor(tintFor(col), 1.0f), 0.55f);
            if (hasTree) {
                topY = h + (int)(2.f + 7.f * treeProb);
                u16 log = (col.biome == BI_TAIGA || col.biome == BI_BOREAL || col.biome == BI_ICE_TUNDRA) ? B_LOG_PINE
                        : (col.biome == BI_RAINFOREST ? B_LOG_KAPOK : (col.biome == BI_DESERT ? B_LOG_ACACIA : B_LOG_OAK));
                col1 = lerpColor(col1, textureAvgColor(g_blocks[leafFor(log)].texTop), clampf(treeProb * 1.15f, 0.f, 0.9f));
            }
            if (h < SEA_LEVEL) { topY = SEA_LEVEL; col1 = 0x2f6a8e; m.surf[m.at(cx, cz)] = 1; }
            m.height[m.at(cx, cz)] = (u8)(clampi(topY, 0, 254) / 2);
            m.color[m.at(cx, cz)] = col1;
            m.biome[m.at(cx, cz)] = (u8)col.biome;
        }
}
