// vb_world.h : voxel storage (palette compressed sections), lighting, chunks
#pragma once
#include "vb_common.h"
#include "vb_blocks.h"

static const int CH  = 32;               // chunk width/depth in blocks
static const int WH  = 256;              // world height
static const int SEC = 16;               // section edge
static const int SEC_Y = WH / SEC;       // 16 sections per column
static const int SEC_PER_CH = CH / SEC;  // 2
static const int SEA_LEVEL = 100;

static const int WORLD_SIZES[3] = { 256, 512, 1024 };

// ------------------------------------------------------------------ section
// 16x16x16 voxels, palette compressed (0/4/8/16 bits per index) + 4/4 light
struct Section {
    u16 uniformVal = 0;
    u8  bits = 0;            // 0 uniform, else 4/8/16
    u16 pal[256];
    int palCount = 0;
    std::vector<u8> idx;     // packed indices (4096*bits/8 bytes)
    std::vector<u8> light;   // 4096 bytes: high nibble sky, low nibble block
    bool empty = true;      // contains no blocks at all
    bool hasAir = true;     // contains at least one air voxel

    Section() { memset(pal, 0, sizeof(pal)); }
    inline int count() const { return 4096; }
    u16 get(int i) const {
        if (bits == 0) return uniformVal;
        if (bits == 4) { u8 b = idx[i >> 1]; return pal[(i & 1) ? (b >> 4) : (b & 15)]; }
        if (bits == 8) return pal[idx[i]];
        return ((const u16*)(const void*)idx.data())[i];
    }
    void set(int i, u16 v);
    void packFrom(const u16* src);
    inline u8 getLight(int i) const { return light.empty() ? 0 : light[i]; }
    inline void setLight(int i, u8 v) { if (!light.empty()) light[i] = v; }
    void allocLight() { if (light.empty()) light.assign(4096, 0); }
};

// ------------------------------------------------------------------ chunk
struct ChunkMesh;   // gpu mesh, defined by the renderer

struct Chunk {
    int cx = 0, cz = 0;
    Section* sec[SEC_Y][SEC_PER_CH][SEC_PER_CH];
    u16 height[CH * CH];      // topmost non-air height per column (+1)
    u32 tint[CH * CH];        // per column grass/foliage tint from the climate
    bool generated = false;
    bool lit = false;
    bool modified = false;    // needs save
    u32 lastUse = 0;
    ChunkMesh* meshes[4] = { nullptr, nullptr, nullptr, nullptr };  // LOD 0..3
    Chunk() {
        memset(sec, 0, sizeof(sec));
        memset(height, 0, sizeof(height));
    }
    ~Chunk();
};

// ------------------------------------------------------------------ world
struct CoarseMap {
    int size = 0;         // world size in blocks
    int res = 4;          // blocks per cell
    int cells = 0;        // size/res
    std::vector<u8> height;      // height/2
    std::vector<u32> color;      // surface colour
    std::vector<u8> surf;        // 0 land, 1 water
    std::vector<u8> biome;       // biome id per cell
    int at(int cx, int cz) const { return cz * cells + cx; }
    int getHeight(int cx, int cz) const { return height[at(cx, cz)] * 2; }
};

struct World {
    int size = 512;
    int chunksX = 0;
    u64 seed = 1234;
    std::vector<Chunk*> chunks;
    std::vector<Chunk*> pendingLight;
    CoarseMap coarse;
    u32 tick = 0;

    // edits are kept so worlds can be saved / regenerated
    std::unordered_map<u64, u16> edits;
    std::vector<u64> editOrder;

    void init(int worldSize, u64 seed);
    void shutdown();
    inline bool inside(int x, int z) const { return x >= 0 && z >= 0 && x < size && z < size; }
    inline Chunk* chunkAt(int cx, int cz) const {
        if (cx < 0 || cz < 0 || cx >= chunksX || cz >= chunksX) return nullptr;
        return chunks[(size_t)cz * chunksX + cx];
    }
    inline Chunk* chunkOf(int x, int z) const { return chunkAt(ifloordiv(x, CH), ifloordiv(z, CH)); }
    Chunk* getOrCreateChunk(int cx, int cz);
    void destroyChunk(Chunk* c);

    inline Section* sectionAt(int x, int y, int z) const {
        if (y < 0 || y >= WH) return nullptr;
        Chunk* c = chunkOf(x, z);
        if (!c) return nullptr;
        return c->sec[y / SEC][(z & (CH - 1)) / SEC][(x & (CH - 1)) / SEC];
    }
    u16 get(int x, int y, int z) const {
        if (y < 0) return B_BEDROCK;
        if (y >= WH) return B_AIR;
        if (!inside(x, z)) return B_BEDROCK;   // world border is solid rock
        Section* s = sectionAt(x, y, z);
        if (!s) return B_AIR;
        return s->get(((y & 15) * SEC + (z & 15)) * SEC + (x & 15));
    }
    void set(int x, int y, int z, u16 v, bool record = true);
    bool isSolid(int x, int y, int z) const { return bIsSolid(get(x, y, z)); }
    int heightAt(int x, int z) const;
    void checkPendingLight();

    // lighting
    void relightChunk(Chunk* c);
    void relightAround(int x, int y, int z);
    inline u8 lightAt(int x, int y, int z) const {
        if (y < 0) return 0;
        if (y >= WH) return 0xF0;
        Section* s = sectionAt(x, y, z);
        if (!s) return 0xF0;     // empty section => open sky
        return s->getLight(((y & 15) * SEC + (z & 15)) * SEC + (x & 15));
    }
    inline int skyLight(int x, int y, int z) const { return lightAt(x, y, z) >> 4; }
    inline int blockLight(int x, int y, int z) const { return lightAt(x, y, z) & 15; }

    // save / load (edits + seed + size)
    bool save(const char* path);
    bool load(const char* path);
    void applyEditsFor(Chunk* c);
};

// worldgen
void generateChunk(World& w, Chunk& c);
void buildCoarseMap(World& w);

// biome info returned by the terrain sampler (also used to colour LOD maps)
int biomeCount();
const char* biomeName(int id);
int biomeTemp(int id);          // 0 cold .. 100 hot
int biomeAt(float temp, float rain, float height01);
