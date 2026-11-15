// vb_mesh.cpp : voxel chunk mesher + height field LOD mesher
#include "vb_mesh.h"

void tileUV(int tile, float& u0, float& v0, float& u1, float& v1) {
    int col = tile % ATLAS_GRID, row = tile / ATLAS_GRID;
    float cell = (float)ATLAS_CELL / (float)ATLAS_SIZE;      // 16/256
    float pad = 4.f / (float)ATLAS_SIZE;
    float inset = 0.35f / (float)ATLAS_SIZE;
    u0 = col * cell + pad + inset;
    v0 = row * cell + pad + inset;
    u1 = u0 + 8.f / (float)ATLAS_SIZE - inset * 2;
    v1 = v0 + 8.f / (float)ATLAS_SIZE - inset * 2;
}

// ------------------------------------------------------------------ helpers
static inline float luma(float sky, float blk) {
    float b = sky > blk ? sky : blk;
    return 0.10f + 0.90f * powf(saturate(b), 1.15f);
}

struct FaceDir { int dx, dy, dz; };

static const FaceDir FACES[6] = {
    { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
};

// corner offsets (in block units) for each face, order: 4 corners ccw
static const int FACE_CORNERS[6][4][3] = {
    // +X
    { {1,0,0}, {1,1,0}, {1,1,1}, {1,0,1} },
    // -X
    { {0,0,1}, {0,1,1}, {0,1,0}, {0,0,0} },
    // +Y
    { {0,1,0}, {0,1,1}, {1,1,1}, {1,1,0} },
    // -Y
    { {0,0,0}, {1,0,0}, {1,0,1}, {0,0,1} },
    // +Z
    { {1,0,1}, {1,1,1}, {0,1,1}, {0,0,1} },
    // -Z
    { {0,0,0}, {0,1,0}, {1,1,0}, {1,0,0} },
};

// ambient occlusion for a face corner
static float vertexAO(bool s1, bool s2, bool cn) {
    if (s1 && s2) return 0.55f;
    int n = (int)s1 + (int)s2 + (int)cn;
    if (n >= 3) return 0.55f;
    if (n == 2) return 0.72f;
    if (n == 1) return 0.86f;
    return 1.0f;
}

struct SectionCache {
    u16 blk[18 * 18 * 18];
    u8  lit[18 * 18 * 18];
    inline int at(int x, int y, int z) const { return ((y + 1) * 18 + (z + 1)) * 18 + (x + 1); }
};

// ------------------------------------------------------------------ LOD0 mesh
static void meshSection(World& w, Chunk& c, int sy, int sz, int sx, MeshData& out) {
    static SectionCache cs;
    Section* self = c.sec[sy][sz][sx];
    int bx = c.cx * CH + sx * SEC, bz = c.cz * CH + sz * SEC, by = sy * SEC;

    // interior
    for (int y = 0; y < SEC; y++)
        for (int z = 0; z < SEC; z++)
            for (int x = 0; x < SEC; x++) {
                int i = (y * SEC + z) * SEC + x;
                u16 b = self ? self->get(i) : B_AIR;
                u8 l = self ? self->getLight(i) : 0xF0;
                int p = cs.at(x, y, z);
                cs.blk[p] = b; cs.lit[p] = l;
            }
    // border shell
    for (int y = -1; y <= SEC; y++)
        for (int z = -1; z <= SEC; z++)
            for (int x = -1; x <= SEC; x++) {
                if (x >= 0 && x < SEC && z >= 0 && z < SEC && y >= 0 && y < SEC) continue;
                int wx = bx + x, wy = by + y, wz = bz + z;
                int p = cs.at(x, y, z);
                if (wy < 0) { cs.blk[p] = B_BEDROCK; cs.lit[p] = 0; continue; }
                if (wy >= WH) { cs.blk[p] = B_AIR; cs.lit[p] = 0xF0; continue; }
                // treat not-yet-generated neighbours as solid so we never mesh a wall
                Chunk* nc = w.chunkOf(wx, wz);
                if (!nc) { cs.blk[p] = B_BEDROCK; cs.lit[p] = 0; continue; }
                cs.blk[p] = w.get(wx, wy, wz);
                cs.lit[p] = w.lightAt(wx, wy, wz);
            }

    const u32* tint = c.tint;
    for (int y = 0; y < SEC; y++)
        for (int z = 0; z < SEC; z++)
            for (int x = 0; x < SEC; x++) {
                int p = cs.at(x, y, z);
                u16 v = cs.blk[p];
                if (v == B_AIR) continue;
                int id = blockId(v);
                BlockDef& d = g_blocks[id];
                int wx = bx + x, wy = by + y, wz = bz + z;

                if (d.flags & BF_CROSS) {
                    int shy = cs.lit[p];
                    float sky = (float)(shy >> 4) / 15.f, blk = (float)(shy & 15) / 15.f;
                    float bright = luma(sky, blk);
                    u32 col = 0xFFFFFFFF;
                    if (d.tint) {
                        u32 t = tint[(wz & (CH - 1)) * CH + (wx & (CH - 1))];
                        col = shadeColor(t, bright);
                    } else col = rgb((int)(255 * bright), (int)(255 * bright), (int)(255 * bright));
                    // two crossed quads
                    float u0, v0, u1, v1;
                    tileUV(d.texSide, u0, v0, u1, v1);
                    float x0 = (float)wx, x1 = x0 + 1.f, z0 = (float)wz, z1 = z0 + 1.f;
                    float y0 = (float)wy, y1 = y0 + 1.f;
                    float i0 = 0.15f, i1 = 0.85f;
                    Vtx a, b, cc, dd;
                    a = { x0 + i0, y0, z0 + i0, u0, v1, col };
                    b = { x0 + i0, y1, z0 + i0, u0, v0, col };
                    cc = { x1 - i0, y1, z1 - i0, u1, v0, col };
                    dd = { x1 - i0, y0, z1 - i0, u1, v1, col };
                    out.quad(a, b, cc, dd);
                    a = { x1 - i0, y0, z0 + i0, u0, v1, col };
                    b = { x1 - i0, y1, z0 + i0, u0, v0, col };
                    cc = { x0 + i0, y1, z1 - i0, u1, v0, col };
                    dd = { x0 + i0, y0, z1 - i0, u1, v1, col };
                    out.quad(a, b, cc, dd);
                    continue;
                }
                if (d.flags & BF_LIQUID) {
                    // only the surface of liquids is drawn
                    u16 above = cs.blk[cs.at(x, y + 1, z)];
                    if (blockId(above) == id || bIsOpaque(above)) {
                        // sides still get drawn against air
                    }
                }
                for (int f = 0; f < 6; f++) {
                    int nx = x + FACES[f].dx, ny = y + FACES[f].dy, nz = z + FACES[f].dz;
                    u16 nb = cs.blk[cs.at(nx, ny, nz)];
                    u16 nid = blockId(nb);
                    if (bIsOpaque(nb)) continue;
                    if (nid == v) continue;                       // same block (water/leaves stacks)
                    if ((d.flags & BF_LEAF) && (g_blocks[nid].flags & BF_LEAF)) continue;  // fast leaves
                    if ((d.flags & BF_LIQUID) && bIsLiquid(nb)) continue;
                    if ((d.flags & BF_LIQUID) && f == 2) {
                        // water top slightly lower for a nicer surface
                    }
                    int tile = (f == 2) ? d.texTop : (f == 3 ? d.texBottom : d.texSide);
                    float u0, v0, u1, v1;
                    tileUV(tile, u0, v0, u1, v1);
                    float lit = cs.lit[cs.at(nx, ny, nz)];
                    float sky = (float)((int)lit >> 4) / 15.f;
                    float blk = (float)((int)lit & 15) / 15.f;
                    float bright = luma(sky, blk);
                    static const float faceShade[6] = { 0.90f, 0.80f, 1.00f, 0.62f, 0.86f, 0.74f };
                    bright *= faceShade[f];
                    // subtle per block variation so large flat areas are not uniform
                    bright *= 0.975f + 0.05f * ((hash3i(wx, wy, wz, 17u) % 100) / 100.f);

                    u32 base = 0xFFFFFFFF;
                    bool tinted = d.tint && f == 2;   // only the top surface picks up the climate tint
                    if (tinted) base = tint[(wz & (CH - 1)) * CH + (wx & (CH - 1))];
                    // AO for the four corners
                    static const int AOOFF[4][2] = { {0,0},{0,1},{1,1},{1,0} };
                    Vtx vv[4];
                    for (int k = 0; k < 4; k++) {
                        const int* oc = FACE_CORNERS[f][k];
                        const int* co = AOOFF[k];
                        // compute the two edge neighbours + corner in the face plane
                        int e1[3] = { nx, ny, nz }, e2[3] = { nx, ny, nz }, cn[3] = { nx, ny, nz };
                        int ax = 0, az = 0, ay = 0;
                        // pick the two tangent axes
                        if (FACES[f].dx != 0) { ax = 1; az = 2; }
                        else if (FACES[f].dy != 0) { ay = 1; ax = 0; az = 2; }
                        else { az = 1; ax = 0; }
                        int s1 = (co[0] == 0) ? -1 : 1;
                        int s2 = (co[1] == 0) ? -1 : 1;
                        if (FACES[f].dx != 0) {
                            e1[2] = nz + s1; e2[1] = ny + s2; cn[2] = nz + s1; cn[1] = ny + s2;
                        } else if (FACES[f].dy != 0) {
                            e1[0] = nx + s1; e2[2] = nz + s2; cn[0] = nx + s1; cn[2] = nz + s2;
                        } else {
                            e1[0] = nx + s1; e2[1] = ny + s2; cn[0] = nx + s1; cn[1] = ny + s2;
                        }
                        bool b1 = bIsOpaque(cs.blk[cs.at(e1[0], e1[1], e1[2])]);
                        bool b2 = bIsOpaque(cs.blk[cs.at(e2[0], e2[1], e2[2])]);
                        bool bc = bIsOpaque(cs.blk[cs.at(cn[0], cn[1], cn[2])]);
                        float ao = vertexAO(b1, b2, bc);
                        float br = bright * ao;
                        u32 col = tinted ? shadeColor(base, br)
                                         : rgb((int)(255 * br), (int)(255 * br), (int)(255 * br));
                        float vx = (float)(wx + oc[0]);
                        float vy = (float)(wy + oc[1]);
                        float vz = (float)(wz + oc[2]);
                        // liquid surface sits a little below the block top
                        if ((d.flags & BF_LIQUID) && oc[1] == 1) vy -= 0.12f;
                        float tu = (oc[0] + oc[2]) ? u1 : u0;
                        float tv = oc[1] ? v0 : v1;
                        if (f == 2 || f == 3) { tu = oc[0] ? u1 : u0; tv = oc[2] ? v1 : v0; }
                        vv[k] = { vx, vy, vz, tu, tv, col };
                        (void)ax; (void)ay; (void)az;
                    }
                    out.quad(vv[0], vv[1], vv[2], vv[3]);
                }
            }
}

void buildChunkMesh(World& w, Chunk& c, int lod, MeshData& out, int cullBelowY) {
    out.clear();
    if (lod == 0) {
        for (int sy = 0; sy < SEC_Y; sy++)
            for (int sz = 0; sz < SEC_PER_CH; sz++)
                for (int sx = 0; sx < SEC_PER_CH; sx++) {
                    if ((sy + 1) * SEC < cullBelowY) continue;   // deep sections are hidden below the player
                    Section* s = c.sec[sy][sz][sx];
                    if (!s || s->empty) continue;
                    // sections whose air is completely enclosed by solid neighbours
                    // cannot be seen from outside -> skip the whole section
                    {
                        bool sealed = true;
                        for (int f = 0; f < 6 && sealed; f++) {
                            int nx = sx + FACES[f].dx, ny = sy + FACES[f].dy, nz = sz + FACES[f].dz;
                            if (nx < 0 || nx >= SEC_PER_CH || nz < 0 || nz >= SEC_PER_CH || ny < 0 || ny >= SEC_Y) sealed = false;
                            else { Section* n = c.sec[ny][nz][nx]; if (!n || n->hasAir) sealed = false; }
                        }
                        if (sealed) continue;
                    }
                    // fast reject: fully opaque section surrounded by opaque sections
                    if (s->bits == 0 && bIsOpaque(s->uniformVal)) {
                        bool hidden = true;
                        for (int f = 0; f < 6 && hidden; f++) {
                            int nx = sx + FACES[f].dx, ny = sy + FACES[f].dy, nz = sz + FACES[f].dz;
                            if (nx >= 0 && nx < SEC_PER_CH && nz >= 0 && nz < SEC_PER_CH && ny >= 0 && ny < SEC_Y) {
                                Section* n = c.sec[ny][nz][nx];
                                if (!n || !(n->bits == 0 && bIsOpaque(n->uniformVal))) hidden = false;
                            } else hidden = false;
                        }
                        if (hidden) continue;
                    }
                    meshSection(w, c, sy, sz, sx, out);
                }
        return;
    }
    // ---- LOD height field
    int step = 1 << lod;                 // 2,4,8
    int cells = CH / step;               // 16,8,4
    int baseX = c.cx * CH, baseZ = c.cz * CH;
    struct Cell { int h; u32 col; bool water; };
    static std::vector<Cell> grid;
    grid.assign((size_t)(cells + 2) * (cells + 2), Cell{ 0, 0, false });
    auto gi = [&](int x, int z) { return (z + 1) * (cells + 2) + (x + 1); };
    for (int z = -1; z <= cells; z++)
        for (int x = -1; x <= cells; x++) {
            int hmax = 0;
            u32 col = 0;
            int n = 0; u64 r = 0, g = 0, b = 0;
            for (int dz = 0; dz < step; dz++)
                for (int dx = 0; dx < step; dx++) {
                    int lx = x * step + dx, lz = z * step + dz;
                    int wx = baseX + clampi(lx, 0, CH - 1), wz = baseZ + clampi(lz, 0, CH - 1);
                    int h = 0;
                    if (lx >= 0 && lx < CH && lz >= 0 && lz < CH) h = c.height[lz * CH + lx];
                    else {
                        Chunk* nc = w.chunkOf(wx, wz);
                        h = nc ? nc->height[(wz & (CH - 1)) * CH + (wx & (CH - 1))] : 0;
                    }
                    if (h > hmax) hmax = h;
                }
            // colour from the topmost block of the centre column
            int cx = clampi(x * step + step / 2, 0, CH - 1), cz = clampi(z * step + step / 2, 0, CH - 1);
            int wx = baseX + cx, wz = baseZ + cz;
            int h = (x >= 0 && x < cells && z >= 0 && z < cells) ? c.height[cz * CH + cx] : hmax;
            if (h <= 0) h = hmax;
            u16 top = w.get(wx, imax(0, h - 1), wz);
            BlockDef& d = g_blocks[blockId(top)];
            u32 tcol = textureAvgColor(blockId(top) == B_AIR ? T_GRASS_TOP : d.texTop);
            if (d.tint) {
                u32 t = c.tint[cz * CH + cx];
                tcol = lerpColor(tcol, t, 0.6f);
            }
            bool water = hmax <= SEA_LEVEL;
            if (water) { hmax = SEA_LEVEL + 1; tcol = textureAvgColor(T_WATER); }
            (void)n; (void)r; (void)g; (void)b; (void)col;
            Cell& cc = grid[gi(x, z)];
            cc.h = hmax; cc.col = tcol; cc.water = water;
        }

    // emit top + side quads
    for (int z = 0; z < cells; z++)
        for (int x = 0; x < cells; x++) {
            Cell& cc = grid[gi(x, z)];
            int x0 = baseX + x * step, z0 = baseZ + z * step;
            int x1 = x0 + step, z1 = z0 + step;
            int h = cc.h;
            float bright = cc.water ? 0.85f : 1.0f;
            u32 col = shadeColor(cc.col, bright * 1.0f);
            float u0, v0, u1, v1;
            tileUV(cc.water ? T_WATER : T_GRASS_TOP, u0, v0, u1, v1);
            Vtx a = { (float)x0, (float)h, (float)z0, u0, v1, col };
            Vtx b = { (float)x0, (float)h, (float)z1, u0, v0, col };
            Vtx c2 = { (float)x1, (float)h, (float)z1, u1, v0, col };
            Vtx d2 = { (float)x1, (float)h, (float)z0, u1, v1, col };
            out.quad(a, b, c2, d2);
            // side walls where neighbours are lower
            int hn[4] = { grid[gi(x, z - 1)].h, grid[gi(x, z + 1)].h, grid[gi(x - 1, z)].h, grid[gi(x + 1, z)].h };
            int nxs[4] = { x0, x0, x0, x1 };
            int nzs[4] = { z0, z1, z0, z0 };
            int dxs[4] = { 0, 0, 0, 0 };
            int dzs[4] = { 0, 0, 0, 0 };
            dxs[3] = 0; (void)dxs; (void)dzs;
            for (int side = 0; side < 4; side++) {
                if (hn[side] >= h) continue;
                int yLo = imax(0, hn[side] - 2);
                u32 scol = shadeColor(cc.col, 0.72f);
                if (side == 0) {
                    Vtx p0 = { (float)x0, (float)h, (float)z0, u0, v0, scol };
                    Vtx p1 = { (float)x1, (float)h, (float)z0, u1, v0, scol };
                    Vtx p2 = { (float)x1, (float)yLo, (float)z0, u1, v1, scol };
                    Vtx p3 = { (float)x0, (float)yLo, (float)z0, u0, v1, scol };
                    out.quad(p0, p1, p2, p3);
                } else if (side == 1) {
                    Vtx p0 = { (float)x1, (float)h, (float)z1, u1, v0, scol };
                    Vtx p1 = { (float)x0, (float)h, (float)z1, u0, v0, scol };
                    Vtx p2 = { (float)x0, (float)yLo, (float)z1, u0, v1, scol };
                    Vtx p3 = { (float)x1, (float)yLo, (float)z1, u1, v1, scol };
                    out.quad(p0, p1, p2, p3);
                } else if (side == 2) {
                    Vtx p0 = { (float)x0, (float)h, (float)z1, u0, v0, scol };
                    Vtx p1 = { (float)x0, (float)h, (float)z0, u1, v0, scol };
                    Vtx p2 = { (float)x0, (float)yLo, (float)z0, u1, v1, scol };
                    Vtx p3 = { (float)x0, (float)yLo, (float)z1, u0, v1, scol };
                    out.quad(p0, p1, p2, p3);
                } else {
                    Vtx p0 = { (float)x1, (float)h, (float)z0, u1, v0, scol };
                    Vtx p1 = { (float)x1, (float)h, (float)z1, u0, v0, scol };
                    Vtx p2 = { (float)x1, (float)yLo, (float)z1, u0, v1, scol };
                    Vtx p3 = { (float)x1, (float)yLo, (float)z0, u1, v1, scol };
                    out.quad(p0, p1, p2, p3);
                }
                (void)nxs; (void)nzs;
            }
            // skirt on chunk borders to hide LOD seams
            bool border = (x == 0 || z == 0 || x == cells - 1 || z == cells - 1);
            if (border) {
                u32 scol = shadeColor(cc.col, 0.62f);
                int yLo = imax(0, h - step * 2 - 2);
                float sx0 = (float)x0, sx1 = (float)x1, sz0 = (float)z0, sz1 = (float)z1;
                Vtx p0 = { sx0, (float)h, sz0, u0, v0, scol };
                Vtx p1 = { sx1, (float)h, sz0, u1, v0, scol };
                Vtx p2 = { sx1, (float)yLo, sz0, u1, v1, scol };
                Vtx p3 = { sx0, (float)yLo, sz0, u0, v1, scol };
                out.quad(p0, p1, p2, p3);
                Vtx q0 = { sx1, (float)h, sz1, u1, v0, scol };
                Vtx q1 = { sx0, (float)h, sz1, u0, v0, scol };
                Vtx q2 = { sx0, (float)yLo, sz1, u0, v1, scol };
                Vtx q3 = { sx1, (float)yLo, sz1, u1, v1, scol };
                out.quad(q0, q1, q2, q3);
                Vtx r0 = { sx1, (float)h, sz0, u1, v0, scol };
                Vtx r1 = { sx1, (float)h, sz1, u0, v0, scol };
                Vtx r2 = { sx1, (float)yLo, sz1, u0, v1, scol };
                Vtx r3 = { sx1, (float)yLo, sz0, u1, v1, scol };
                out.quad(r0, r1, r2, r3);
                Vtx s0 = { sx0, (float)h, sz1, u0, v0, scol };
                Vtx s1v = { sx0, (float)h, sz0, u1, v0, scol };
                Vtx s2 = { sx0, (float)yLo, sz0, u1, v1, scol };
                Vtx s3 = { sx0, (float)yLo, sz1, u0, v1, scol };
                out.quad(s0, s1v, s2, s3);
            }
        }
}

// ------------------------------------------------------------------ coarse far mesh
void buildCoarseMesh(const World& w, float centerX, float centerZ, float skipRadius,
                     float maxRadius, int cellStep, MeshData& out) {
    out.clear();
    const CoarseMap& m = w.coarse;
    if (m.cells == 0) return;
    int cstep = cellStep / m.res;          // coarse cells per mesh cell
    if (cstep < 1) cstep = 1;
    int cells = m.cells / cstep;
    float sqrtSkip2 = skipRadius * skipRadius;
    float maxR2 = maxRadius * maxRadius;
    for (int cz = 0; cz < cells; cz++)
        for (int cx = 0; cx < cells; cx++) {
            float wx = (float)(cx * cstep * m.res) + m.res * cstep * 0.5f;
            float wz = (float)(cz * cstep * m.res) + m.res * cstep * 0.5f;
            float dx = wx - centerX, dz = wz - centerZ;
            float d2 = dx * dx + dz * dz;
            if (d2 < sqrtSkip2 || d2 > maxR2) continue;
            // sample the coarse map (max of the covered cells)
            int h = 0;
            u32 col = 0;
            bool water = false;
            int n = 0; u64 ar = 0, ag = 0, ab = 0;
            for (int z = 0; z < cstep; z++)
                for (int x = 0; x < cstep; x++) {
                    int i = m.at(cx * cstep + x, cz * cstep + z);
                    if (m.getHeight(cx * cstep + x, cz * cstep + z) / 2 > h) h = m.height[i];
                    if (m.surf[i]) water = true;
                    u32 c = m.color[i];
                    ar += c >> 16 & 255; ag += c >> 8 & 255; ab += c & 255; n++;
                }
            col = rgb((int)(ar / n), (int)(ag / n), (int)(ab / n));
            int hh = h * 2;
            if (water && hh <= SEA_LEVEL) { hh = SEA_LEVEL; col = 0x2f6a8e; }
            float x0 = wx - m.res * cstep * 0.5f, z0 = wz - m.res * cstep * 0.5f;
            float x1 = x0 + m.res * cstep, z1 = z0 + m.res * cstep;
            float u0, v0, u1, v1;
            tileUV(T_GRASS_TOP, u0, v0, u1, v1);
            float f = hh / 256.f;
            col = shadeColor(col, 1.0f);
            Vtx a = { x0, (float)hh, z0, u0, v1, col };
            Vtx b = { x0, (float)hh, z1, u0, v0, col };
            Vtx c2 = { x1, (float)hh, z1, u1, v0, col };
            Vtx e2 = { x1, (float)hh, z0, u1, v1, col };
            out.quad(a, b, c2, e2);
            (void)f;
            // rough side walls towards the camera side for a thicker silhouette
            u32 scol = shadeColor(col, 0.7f);
            int yLo = imax(0, hh - 6);
            Vtx p0 = { x0, (float)hh, z0, u0, v0, scol };
            Vtx p1 = { x1, (float)hh, z0, u1, v0, scol };
            Vtx p2 = { x1, (float)yLo, z0, u1, v1, scol };
            Vtx p3 = { x0, (float)yLo, z0, u0, v1, scol };
            out.quad(p0, p1, p2, p3);
            Vtx q0 = { x1, (float)hh, z1, u1, v0, scol };
            Vtx q1 = { x0, (float)hh, z1, u0, v0, scol };
            Vtx q2 = { x0, (float)yLo, z1, u0, v1, scol };
            Vtx q3 = { x1, (float)yLo, z1, u1, v1, scol };
            out.quad(q0, q1, q2, q3);
        }
}
