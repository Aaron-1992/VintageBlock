// tools/sim.cpp : headless gameplay simulation (no GL) to validate logic
#include "../src/vb_game.h"
#include "../src/vb_mesh.h"
#include "../src/vb_png.h"
#include <cstdio>

static World* g_w;
static long g_totalQuads = 0;
static long g_lodQuads[4] = { 0, 0, 0, 0 };

// mimic the streaming loop of the win32 build
static void stream(Game& g, float budgetMs) {
    World& w = g.world;
    int pcx = ifloordiv(floori(g.pos.x), CH);
    int pcz = ifloordiv(floori(g.pos.z), CH);
    int radius = (int)g.viewDistance + 1;
    struct Cand { float d; int cx, cz; };
    std::vector<Cand> cands;
    for (int dz = -radius; dz <= radius; dz++)
        for (int dx = -radius; dx <= radius; dx++) {
            int cx = pcx + dx, cz = pcz + dz;
            if (cx < 0 || cz < 0 || cx >= w.chunksX || cz >= w.chunksX) continue;
            float d = sqrtf((float)(dx * dx + dz * dz));
            if (d > radius) continue;
            cands.push_back({ d, cx, cz });
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
    clock_t start = clock();
    for (auto& cd : cands) {
        if ((clock() - start) * 1000.0 / CLOCKS_PER_SEC > budgetMs) break;
        Chunk* c = w.chunkAt(cd.cx, cd.cz);
        if (!c) { w.getOrCreateChunk(cd.cx, cd.cz); continue; }
        int lod = 0;
        float db = cd.d * CH;
        if (db > g.viewDistance * CH * 0.62f) lod = 1;
        if (db > g.viewDistance * CH * 0.82f) lod = 2;
        if (db > g.viewDistance * CH) lod = 3;
        if (!c->meshes[lod]) {
            static MeshData md;
            buildChunkMesh(w, *c, lod, md, (int)g.pos.y - 46);
            g_totalQuads += md.quads;
            g_lodQuads[lod] += md.quads;
            c->meshes[lod] = (ChunkMesh*)1;   // mark built
        }
    }
    // unload far chunks
    for (int cz = 0; cz < w.chunksX; cz++)
        for (int cx = 0; cx < w.chunksX; cx++) {
            Chunk* c = w.chunkAt(cx, cz);
            if (!c) continue;
            float dcx = (float)(cx - pcx), dcz = (float)(cz - pcz);
            if (sqrtf(dcx * dcx + dcz * dcz) > g.viewDistance + 2.5f) w.destroyChunk(c);
        }
}

int main(int argc, char** argv) {
    int size = 512;
    u64 seed = 1207;
    if (argc > 1) size = atoi(argv[1]);
    if (argc > 2) seed = (u64)atoll(argv[2]);
    initBlocks();
    initItems();

    Game g;
    printf("== starting world %d seed %llu ==\n", size, (unsigned long long)seed);
    g.startWorld(size, seed, GM_SURVIVAL);
    g.viewDistance = 6.f;
    printf("spawn at %.1f %.1f %.1f  biome %s\n", g.pos.x, g.pos.y, g.pos.z,
           g.biomeNameAt((int)g.pos.x, (int)g.pos.z));
    printf("temperature at spawn: %.1f C\n", g.groundTemperature());

    InputState in;
    // ---- 1. walk forward for a while
    float startX = g.pos.x, startZ = g.pos.z;
    int stuckFrames = 0;
    float lowestY = 999, highestY = -999;
    for (int f = 0; f < 900; f++) {
        in.dt = 1.f / 60.f;
        in.key['W'] = true;
        g.yaw += 0.004f;
        g.update(1.f / 60.f, in);
        in.endFrame();
        lowestY = fminf(lowestY, g.pos.y);
        highestY = fmaxf(highestY, g.pos.y);
        if (f % 180 == 0) stream(g, 12);
        if (g.pos.y < 2) { printf("!! fell out of the world at frame %d\n", f); break; }
    }
    printf("walked from (%.1f,%.1f) to (%.1f,%.1f)  y range %.1f..%.1f  health %.0f\n",
           startX, startZ, g.pos.x, g.pos.z, lowestY, highestY, g.health);
    if (fabsf(g.pos.x - startX) + fabsf(g.pos.z - startZ) < 1.f) { printf("!! player did not move\n"); stuckFrames++; }

    // ---- 2. stream the whole view with no time limit
    g_totalQuads = 0;
    stream(g, 100000);
    int loaded = 0;
    for (int cz = 0; cz < g.world.chunksX; cz++)
        for (int cx = 0; cx < g.world.chunksX; cx++) if (g.world.chunkAt(cx, cz)) loaded++;
    printf("streamed: %d chunks loaded, %ld quads (lod0 %ld lod1 %ld lod2 %ld lod3 %ld)\n",
           loaded, g_totalQuads, g_lodQuads[0], g_lodQuads[1], g_lodQuads[2], g_lodQuads[3]);
    printf("vertex memory ~ %.1f MB\n", g_totalQuads * 4 * sizeof(Vtx) / 1048576.0);

    // ---- 3. breaking + placing
    g.pitch = -0.9f;
    for (int i = 0; i < 40; i++) {
        g.hit = g.raycast(6.f);
        g.tryBreak(0.2f);
    }
    printf("after digging: hit=%d at %d %d %d block now %d\n", (int)g.hit.hit, g.hit.x, g.hit.y, g.hit.z,
           g.world.get(g.hit.x, g.hit.y, g.hit.z));
    int invBefore = 0;
    for (int i = 0; i < 30; i++) invBefore += g.inv[i].count;
    printf("inventory items after digging: %d\n", invBefore);

    // place a torch
    g.sel = 3;
    g.hit = g.raycast(6.f);
    int px = g.hit.x + g.hit.nx, py = g.hit.y + g.hit.ny, pz = g.hit.z + g.hit.nz;
    u16 before = g.world.get(px, py, pz);
    g.tryPlace();
    printf("placed %d at %d %d %d (was %d)\n", g.world.get(px, py, pz), px, py, pz, before);
    g.world.relightAround(px, py, pz);
    printf("light at torch: sky %d block %d\n", g.world.skyLight(px, py, pz), g.world.blockLight(px, py + 1, pz));

    // ---- 4. crafting
    g.addItem(I_FLINT, 4);
    g.addItem(I_STICK, 8);
    std::vector<int> avail;
    g.recipesAvailable(avail);
    printf("craftable recipes at spawn: %d\n", (int)avail.size());
    int flintKnife = -1;
    for (int i = 0; i < g_recipeCount; i++) if (g_recipes[i].out == I_KNIFE_FLINT) flintKnife = i;
    int n0 = g.countItem(I_KNIFE_FLINT);
    g.craft(flintKnife);
    printf("flint knife crafted: %d -> %d\n", n0, g.countItem(I_KNIFE_FLINT));

    // ---- 5. entities
    for (int i = 0; i < 20; i++) g.spawnMob(ENT_DRIFTER, g.pos + v3(3, 0, 0));
    for (int f = 0; f < 300; f++) {
        in.key['W'] = false;
        g.update(1.f / 60.f, in);
        in.endFrame();
        if (g.health < 20) break;
    }
    printf("after drifter fight: %d entities, health %.0f\n", (int)g.entities.size(), g.health);

    // ---- 6. day cycle + weather
    for (int i = 0; i < 12; i++) {
        g.dayTime += 1.f / 12.f;
        if (g.dayTime >= 1) g.dayTime -= 1;
        printf("  t=%.2f temp %.1fC sun%+.2f\n", g.dayTime, g.groundTemperature(),
               sinf(g.dayTime * TAU - PI * 0.5f));
    }

    // ---- 7. save / load
    g.world.set(10, 120, 10, B_TORCH);
    g.saveGame();
    Game g2;
    bool ok = g2.loadGame();
    printf("save/load: %s, edits %d, size %d seed %llu\n", ok ? "ok" : "FAILED",
           (int)g2.world.edits.size(), g2.world.size, (unsigned long long)g2.world.seed);
    printf("  player restored: pos %.1f %.1f %.1f (was %.1f %.1f %.1f) hp %.0f sel %d item0 %d\n",
           g2.pos.x, g2.pos.y, g2.pos.z, g.pos.x, g.pos.y, g.pos.z, g2.health, g2.sel, g2.inv[0].item);
    // fall damage check
    g2.pos.y += 30;
    g2.vel = v3(0, 0, 0);
    for (int f = 0; f < 240; f++) { InputState i2; i2.dt = 1.f/60.f; g2.update(1.f/60.f, i2); i2.endFrame(); if (g2.onGround) break; }
    printf("  fall from +30: health %.0f onGround %d\n", g2.health, (int)g2.onGround);

    // ---- 8. world border
    printf("border block at x=-1: %d  x=%d: %d  y=300: %d\n", g.world.get(-1, 100, 100),
           size, g.world.get(size, 100, 100), g.world.get(100, 300, 100));

    // ---- 9. all block types placeable / texturable
    int badTiles = 0;
    for (int b = 1; b < B_COUNT; b++) {
        if (!g_blocks[b].name) { printf("!! block %d has no name\n", b); badTiles++; }
        if (g_blocks[b].texSide >= T_COUNT) { printf("!! block %d bad tex\n", b); badTiles++; }
    }
    for (int i = 0; i < g_recipeCount; i++) {
        if (!itemDef(g_recipes[i].out)) { printf("!! recipe %s has no output def\n", g_recipes[i].name); badTiles++; }
        if (g_recipes[i].a && !itemDef(g_recipes[i].a)) { printf("!! recipe %s bad ingredient a\n", g_recipes[i].name); badTiles++; }
        if (g_recipes[i].b && !itemDef(g_recipes[i].b)) { printf("!! recipe %s bad ingredient b\n", g_recipes[i].name); badTiles++; }
        if (g_recipes[i].c && !itemDef(g_recipes[i].c)) { printf("!! recipe %s bad ingredient c\n", g_recipes[i].name); badTiles++; }
    }
    printf("registry problems: %d\n", badTiles);

    printf("== done ==\n");
    return 0;
}
