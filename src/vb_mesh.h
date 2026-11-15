// vb_mesh.h : chunk / LOD mesh generation
#pragma once
#include "vb_world.h"

struct Vtx {
    float x, y, z;
    float u, v;
    u32 c;
};
struct MeshData {
    std::vector<Vtx> v;
    int quads = 0;
    void clear() { v.clear(); quads = 0; }
    bool empty() const { return v.empty(); }
    void quad(const Vtx& a, const Vtx& b, const Vtx& c, const Vtx& d) {
        v.push_back(a); v.push_back(b); v.push_back(c); v.push_back(d); quads++;
    }
};

// LOD0 = full voxel mesh (greedy free, per face culling + AO + light)
// LOD1..3 = height field mesh at 2/4/8 block cells
void buildChunkMesh(World& w, Chunk& c, int lod, MeshData& out, int cullBelowY = -99999);

// Far terrain built from the coarse map (whole world, cheap quads).
// `skipRadius` leaves a hole where the detailed chunks are drawn.
void buildCoarseMesh(const World& w, float centerX, float centerZ, float skipRadius,
                     float maxRadius, int cellStep, MeshData& out);

// tile uv helpers
void tileUV(int tile, float& u0, float& v0, float& u1, float& v1);
