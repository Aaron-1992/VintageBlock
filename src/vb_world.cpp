// vb_world.cpp : voxel storage, lighting, save/load
#include "vb_world.h"
#include <cstdio>

// ------------------------------------------------------------------ Section
void Section::packFrom(const u16* src) {
    palCount = 0;
    for (int i = 0; i < 4096; i++) {
        u16 v = src[i];
        int f = -1;
        for (int k = 0; k < palCount; k++) if (pal[k] == v) { f = k; break; }
        if (f < 0) {
            if (palCount < 256) { pal[palCount++] = v; f = palCount - 1; }
            else { f = 0; }   // should not happen
        }
    }
    bool allAir = true, anyAir = false;
    for (int i = 0; i < 4096; i++) {
        if (src[i] != B_AIR) allAir = false; else anyAir = true;
    }
    empty = allAir;
    hasAir = anyAir;
    if (palCount == 1) { bits = 0; uniformVal = pal[0]; idx.clear(); return; }
    bits = palCount <= 16 ? 4 : 8;
    for (int k = palCount; k < 256; k++) pal[k] = 0;
    idx.assign(bits == 4 ? 2048 : 4096, 0);
    for (int i = 0; i < 4096; i++) {
        u16 v = src[i];
        int f = 0;
        for (int k = 0; k < palCount; k++) if (pal[k] == v) { f = k; break; }
        if (bits == 4) {
            if (i & 1) idx[i >> 1] = (u8)((idx[i >> 1] & 0x0F) | (f << 4));
            else idx[i >> 1] = (u8)((idx[i >> 1] & 0xF0) | (f & 15));
        } else idx[i] = (u8)f;
    }
}

static void expandTo(Section& s, int newBits) {
    u16 tmp[4096];
    for (int i = 0; i < 4096; i++) tmp[i] = s.get(i);
    s.bits = (u8)newBits;
    s.idx.assign(newBits == 4 ? 2048 : 4096, 0);
    for (int i = 0; i < 4096; i++) {
        int f = 0;
        for (int k = 0; k < s.palCount; k++) if (s.pal[k] == tmp[i]) { f = k; break; }
        if (newBits == 4) {
            if (i & 1) s.idx[i >> 1] = (u8)((s.idx[i >> 1] & 0x0F) | (f << 4));
            else s.idx[i >> 1] = (u8)((s.idx[i >> 1] & 0xF0) | (f & 15));
        } else s.idx[i] = (u8)f;
    }
}

void Section::set(int i, u16 v) {
    if (bits == 0) {
        if (v == uniformVal) return;
        pal[0] = uniformVal; palCount = 1;
        pal[palCount++] = v;
        bits = 4;
        idx.assign(2048, 0);
        for (int k = 0; k < 4096; k++) {
            int f = (k == i) ? 1 : 0;
            if (k & 1) idx[k >> 1] |= (u8)(f << 4);
            else idx[k >> 1] |= (u8)(f & 15);
        }
    } else {
        int f = -1;
        for (int k = 0; k < palCount; k++) if (pal[k] == v) { f = k; break; }
        if (f < 0) {
            if (palCount < 256) { f = palCount; pal[f] = v; palCount++; }
            else f = 0;
            int need = (bits == 4 && palCount > 16) ? 8 : bits;
            if (need != bits) expandTo(*this, need);
        }
        if (bits == 4) {
            if (i & 1) idx[i >> 1] = (u8)((idx[i >> 1] & 0x0F) | (f << 4));
            else idx[i >> 1] = (u8)((idx[i >> 1] & 0xF0) | (f & 15));
        } else idx[i] = (u8)f;
    }
    if (v != B_AIR) empty = false; else hasAir = true;
}

// ------------------------------------------------------------------ chunk
Chunk::~Chunk() {
    for (int y = 0; y < SEC_Y; y++)
        for (int z = 0; z < SEC_PER_CH; z++)
            for (int x = 0; x < SEC_PER_CH; x++) delete sec[y][z][x];
}

// ------------------------------------------------------------------ world
void World::init(int worldSize, u64 s) {
    shutdown();
    size = worldSize;
    seed = s;
    chunksX = size / CH;
    chunks.assign((size_t)chunksX * chunksX, nullptr);
    tick = 0;
    edits.clear();
    editOrder.clear();
    buildCoarseMap(*this);
}

void World::shutdown() {
    for (auto* c : chunks) if (c) { delete c; }
    chunks.clear();
    chunksX = 0;
}

Chunk* World::getOrCreateChunk(int cx, int cz) {
    Chunk* c = chunkAt(cx, cz);
    if (c) return c;
    if (cx < 0 || cz < 0 || cx >= chunksX || cz >= chunksX) return nullptr;
    c = new Chunk();
    c->cx = cx; c->cz = cz;
    chunks[(size_t)cz * chunksX + cx] = c;
    generateChunk(*this, *c);
    c->generated = true;
    applyEditsFor(c);
    // recompute height map
    for (int z = 0; z < CH; z++)
        for (int x = 0; x < CH; x++) {
            int wx = cx * CH + x, wz = cz * CH + z;
            int h = 0;
            for (int y = WH - 1; y >= 0; y--) {
                Section* sc = c->sec[y / SEC][z / SEC][x / SEC];
                if (sc && !sc->empty) {
                    u16 b = sc->get(((y & 15) * SEC + (z & 15)) * SEC + (x & 15));
                    if (b != B_AIR) { h = y + 1; break; }
                }
            }
            c->height[z * CH + x] = (u16)h;
            (void)wx; (void)wz;
        }
    relightChunk(c);
    return c;
}

void World::destroyChunk(Chunk* c) {
    if (!c) return;
    chunks[(size_t)c->cz * chunksX + c->cx] = nullptr;
    delete c;
}

void World::set(int x, int y, int z, u16 v, bool record) {
    if (y < 0 || y >= WH || !inside(x, z)) return;
    Chunk* c = chunkOf(x, z);
    if (!c) return;
    int sx = (x & (CH - 1)) / SEC, sz = (z & (CH - 1)) / SEC, sy = y / SEC;
    Section* s = c->sec[sy][sz][sx];
    int li = ((y & 15) * SEC + (z & 15)) * SEC + (x & 15);
    if (!s) {
        if (v == B_AIR) return;
        s = new Section();
        s->uniformVal = B_AIR; s->bits = 0; s->pal[0] = B_AIR; s->palCount = 1; s->empty = true;
        c->sec[sy][sz][sx] = s;
    }
    u16 old = s->get(li);
    if (old == v) return;
    s->set(li, v);
    // update column height
    int lx = x & (CH - 1), lz = z & (CH - 1);
    int& h = *(int*)&c->height[lz * CH + lx];
    if (v != B_AIR && y + 1 > h) h = y + 1;
    else if (v == B_AIR && y + 1 == h) {
        int nh = 0;
        for (int yy = y; yy >= 0; yy--) {
            Section* sc = c->sec[yy / SEC][sz][sx];
            if (sc && !sc->empty) {
                u16 b = sc->get(((yy & 15) * SEC + (z & 15)) * SEC + (x & 15));
                if (b != B_AIR) { nh = yy + 1; break; }
            }
        }
        h = nh;
    }
    c->modified = true;
    if (record) {
        u64 key = (u64)x | ((u64)z << 12) | ((u64)y << 24);
        if (edits.find(key) == edits.end()) editOrder.push_back(key);
        edits[key] = v;
    }
}

void World::applyEditsFor(Chunk* c) {
    if (edits.empty()) return;
    int x0 = c->cx * CH, z0 = c->cz * CH;
    for (auto& kv : edits) {
        int x = (int)(kv.first & 0xFFF);
        int z = (int)((kv.first >> 12) & 0xFFF);
        int y = (int)((kv.first >> 24) & 0xFF);
        if (x < x0 || x >= x0 + CH || z < z0 || z >= z0 + CH) continue;
        int sx = (x & (CH - 1)) / SEC, sz = (z & (CH - 1)) / SEC, sy = y / SEC;
        if (sy < 0 || sy >= SEC_Y) continue;
        Section* s = c->sec[sy][sz][sx];
        if (!s) {
            s = new Section();
            s->uniformVal = B_AIR; s->bits = 0; s->pal[0] = B_AIR; s->palCount = 1; s->empty = true;
            c->sec[sy][sz][sx] = s;
        }
        s->set(((y & 15) * SEC + (z & 15)) * SEC + (x & 15), kv.second);
    }
}

int World::heightAt(int x, int z) const {
    if (!inside(x, z)) return 0;
    Chunk* c = chunkOf(x, z);
    if (!c) return 0;
    return c->height[(z & (CH - 1)) * CH + (x & (CH - 1))];
}

// ------------------------------------------------------------------ lighting
static void propagate(std::vector<int>& q, size_t& head) { (void)q; (void)head; }

void World::relightChunk(Chunk* c) {
    if (!c) return;
    static std::vector<u8> tmp;
    tmp.assign((size_t)CH * CH * WH, 0);
    const int W = CH, H = WH;
    auto idxOf = [&](int lx, int ly, int lz) { return ((size_t)ly * W + lz) * W + lx; };

    // --- sky pass (column based)
    for (int lz = 0; lz < CH; lz++)
        for (int lx = 0; lx < CH; lx++) {
            int light = 15;
            for (int y = WH - 1; y >= 0; y--) {
                Section* s = c->sec[y / SEC][lz / SEC][lx / SEC];
                u16 b = s ? s->get(((y & 15) * SEC + (lz & 15)) * SEC + (lx & 15)) : B_AIR;
                if (bIsOpaque(b)) break;
                if (blockId(b) == B_WATER || (g_blocks[blockId(b)].flags & BF_LEAF)) light = imax(0, light - 2);
                tmp[idxOf(lx, y, lz)] = (u8)(light << 4);
            }
        }

    // --- seed BFS with boundary cells
    std::vector<int> q;
    q.reserve(4096);
    const int dirs[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
    for (int lz = 0; lz < CH; lz++)
        for (int lx = 0; lx < CH; lx++)
            for (int y = 0; y < WH; y++) {
                u8 l = tmp[idxOf(lx, y, lz)] >> 4;
                if (!l) continue;
                for (int d = 0; d < 6; d++) {
                    int nx = lx + dirs[d][0], ny = y + dirs[d][1], nz = lz + dirs[d][2];
                    if (nx < 0 || nz < 0 || nx >= CH || nz >= CH || ny < 0 || ny >= WH) continue;
                    if ((tmp[idxOf(nx, ny, nz)] >> 4) < l - 1) { q.push_back((int)idxOf(lx, y, lz) | 0); break; }
                }
            }
    size_t head = 0;
    while (head < q.size()) {
        int i = q[head++];
        int lx = i % W, lz = (i / W) % W, ly = i / (W * W);
        u8 l = tmp[i] >> 4;
        if (!l) continue;
        for (int d = 0; d < 6; d++) {
            int nx = lx + dirs[d][0], ny = ly + dirs[d][1], nz = lz + dirs[d][2];
            if (nx < 0 || nz < 0 || nx >= CH || nz >= CH || ny < 0 || ny >= WH) continue;
            Section* s = c->sec[ny / SEC][nz / SEC][nx / SEC];
            u16 b = s ? s->get(((ny & 15) * SEC + (nz & 15)) * SEC + (nx & 15)) : B_AIR;
            if (bIsOpaque(b)) continue;
            int att = (blockId(b) == B_WATER || (g_blocks[blockId(b)].flags & BF_LEAF)) ? 2 : 1;
            int nl = l - att;
            if (nl <= 0) continue;
            size_t ni = idxOf(nx, ny, nz);
            if ((tmp[ni] >> 4) >= nl) continue;
            tmp[ni] = (u8)((tmp[ni] & 0x0F) | (nl << 4));
            q.push_back((int)ni);
        }
    }

    // --- block light (torches, lava, fire)
    q.clear(); head = 0;
    for (int sy = 0; sy < SEC_Y; sy++)
        for (int sz = 0; sz < SEC_PER_CH; sz++)
            for (int sx = 0; sx < SEC_PER_CH; sx++) {
                Section* s = c->sec[sy][sz][sx];
                if (!s || s->empty) continue;
                for (int i = 0; i < 4096; i++) {
                    u16 b = s->get(i);
                    u8 em = g_blocks[blockId(b)].light;
                    if (!em) continue;
                    int lx = i & 15, lz = (i >> 4) & 15, ly = (i >> 8) + sy * SEC;
                    int gx = (int)(idxOf(lx, ly, lz));
                    tmp[gx] = (u8)((tmp[gx] & 0xF0) | em);
                    q.push_back(gx);
                }
            }
    head = 0;
    while (head < q.size()) {
        int i = q[head++];
        int lx = i % W, lz = (i / W) % W, ly = i / (W * W);
        u8 l = tmp[i] & 15;
        if (!l) continue;
        for (int d = 0; d < 6; d++) {
            int nx = lx + dirs[d][0], ny = ly + dirs[d][1], nz = lz + dirs[d][2];
            if (nx < 0 || nz < 0 || nx >= CH || nz >= CH || ny < 0 || ny >= WH) continue;
            Section* s = c->sec[ny / SEC][nz / SEC][nx / SEC];
            u16 b = s ? s->get(((ny & 15) * SEC + (nz & 15)) * SEC + (nx & 15)) : B_AIR;
            if (bIsOpaque(b)) continue;
            int nl = l - 1;
            if (nl <= 0) continue;
            size_t ni = idxOf(nx, ny, nz);
            if ((tmp[ni] & 15) >= nl) continue;
            tmp[ni] = (u8)((tmp[ni] & 0xF0) | nl);
            q.push_back((int)ni);
        }
    }

    // --- write back
    for (int sy = 0; sy < SEC_Y; sy++)
        for (int sz = 0; sz < SEC_PER_CH; sz++)
            for (int sx = 0; sx < SEC_PER_CH; sx++) {
                Section* s = c->sec[sy][sz][sx];
                if (!s || s->empty) continue;
                s->allocLight();
                for (int ly = 0; ly < SEC; ly++)
                    for (int lz = 0; lz < SEC; lz++) {
                        size_t src = idxOf(sx * SEC, sy * SEC + ly, sz * SEC + lz);
                        memcpy(&s->light[(ly * SEC + lz) * SEC], &tmp[src], SEC);
                    }
            }
    c->lit = true;
}

void World::relightAround(int x, int y, int z) {
    Chunk* c = chunkOf(x, z);
    if (!c) return;
    relightChunk(c);
    int lx = x & (CH - 1), lz = z & (CH - 1);
    if (lx == 0) relightChunk(chunkAt(c->cx - 1, c->cz));
    if (lx == CH - 1) relightChunk(chunkAt(c->cx + 1, c->cz));
    if (lz == 0) relightChunk(chunkAt(c->cx, c->cz - 1));
    if (lz == CH - 1) relightChunk(chunkAt(c->cx, c->cz + 1));
    (void)y;
}

// ------------------------------------------------------------------ save
static const char SAVE_MAGIC[8] = { 'V','B','L','K','S','A','V','1' };

bool World::save(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(SAVE_MAGIC, 1, 8, f);
    fwrite(&seed, 8, 1, f);
    fwrite(&size, 4, 1, f);
    u32 n = (u32)editOrder.size();
    fwrite(&n, 4, 1, f);
    for (u64 k : editOrder) {
        u16 v = edits.count(k) ? edits[k] : B_AIR;
        fwrite(&k, 8, 1, f);
        fwrite(&v, 2, 1, f);
    }
    fclose(f);
    return true;
}

bool World::load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, SAVE_MAGIC, 8) != 0) { fclose(f); return false; }
    u64 s = 0; int sz = 512; u32 n = 0;
    if (fread(&s, 8, 1, f) != 1 || fread(&sz, 4, 1, f) != 1 || fread(&n, 4, 1, f) != 1) { fclose(f); return false; }
    init(sz, s);
    for (u32 i = 0; i < n; i++) {
        u64 k; u16 v;
        if (fread(&k, 8, 1, f) != 1 || fread(&v, 2, 1, f) != 1) break;
        edits[k] = v;
        editOrder.push_back(k);
    }
    fclose(f);
    return true;
}
