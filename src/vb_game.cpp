// vb_game.cpp : gameplay implementation
#include "vb_game.h"

// ------------------------------------------------------------------ recipes
const Recipe g_recipes[] = {
    { "OAK PLANKS",      B_PLANK_OAK,    4, B_LOG_OAK, 1, 0,0, 0,0, 0 },
    { "PINE PLANKS",     B_PLANK_PINE,   4, B_LOG_PINE, 1, 0,0, 0,0, 0 },
    { "BIRCH PLANKS",    B_PLANK_BIRCH,  4, B_LOG_BIRCH, 1, 0,0, 0,0, 0 },
    { "STICKS",          I_STICK,        2, B_PLANK_OAK, 1, 0,0, 0,0, 0 },
    { "FLINT KNIFE",     I_KNIFE_FLINT,  1, I_FLINT, 1, I_STICK, 1, 0,0, 0 },
    { "FLINT AXE",       I_AXE_FLINT,    1, I_FLINT, 2, I_STICK, 1, 0,0, 0 },
    { "FLINT PICKAXE",   I_PICK_FLINT,   1, I_FLINT, 2, I_STICK, 2, 0,0, 0 },
    { "FLINT SHOVEL",    I_SHOVEL_FLINT, 1, I_FLINT, 1, I_STICK, 2, 0,0, 0 },
    { "TORCHES",         B_TORCH,        4, I_STICK, 1, I_COAL, 1, 0,0, 0 },
    { "COPPER INGOT",    I_INGOT_COPPER, 1, I_NUGGET_COPPER, 3, I_COAL, 1, 0,0, 1 },
    { "BRONZE INGOT",    I_INGOT_BRONZE, 1, I_INGOT_COPPER, 1, I_INGOT_TIN, 1, I_COAL, 1, 1 },
    { "IRON INGOT",      I_INGOT_IRON,   1, I_NUGGET_IRON, 3, I_COAL, 1, 0,0, 1 },
    { "COPPER AXE",      I_AXE_COPPER,   1, I_INGOT_COPPER, 1, I_STICK, 1, 0,0, 1 },
    { "COPPER PICKAXE",  I_PICK_COPPER,  1, I_INGOT_COPPER, 2, I_STICK, 1, 0,0, 1 },
    { "COPPER SHOVEL",   I_SHOVEL_COPPER,1, I_INGOT_COPPER, 1, I_STICK, 2, 0,0, 1 },
    { "COPPER KNIFE",    I_KNIFE_COPPER, 1, I_INGOT_COPPER, 1, I_STICK, 1, 0,0, 1 },
    { "HAMMER",          I_HAMMER_COPPER,1, I_INGOT_COPPER, 2, I_STICK, 1, 0,0, 1 },
    { "PROSPECTING PICK",I_PROSPICK_COPPER, 1, I_INGOT_COPPER, 2, I_STICK, 1, 0,0, 1 },
    { "COPPER SWORD",    I_SWORD_COPPER, 1, I_INGOT_COPPER, 1, I_STICK, 1, 0,0, 1 },
    { "CLAY BOWL",       I_BOWL,         1, I_CLAYBALL, 2, 0,0, 0,0, 0 },
    { "COOKING POT",     I_POT,          1, I_CLAYBALL, 5, 0,0, 0,0, 1 },
    { "ROPE",            I_ROPE,         1, I_CROP, 2, 0,0, 0,0, 0 },
    { "BREAD",           I_BREAD,        1, I_CROP, 3, I_COAL, 1, 0,0, 1 },
    { "COOKED MEAT",     I_MEAT_COOKED,  1, I_MEAT_RAW, 1, I_COAL, 1, 0,0, 1 },
    { "STEW",            I_STEW,         1, I_BOWL, 1, I_MEAT_RAW, 1, I_CROP, 1, 1 },
    { "STONE BRICKS",    B_STONE_BRICK,  4, B_COBBLE_GREY, 4, 0,0, 0,0, 0 },
    { "GLASS",           B_GLASS,        2, B_SAND, 2, I_COAL, 1, 0,0, 1 },
    { "CHARCOAL",        I_COAL,         2, B_LOG_PINE, 1, 0,0, 0,0, 1 },
    { "THATCH",          B_THATCH,       2, B_TALLGRASS, 4, B_TALLGRASS_DRY, 4, 0,0, 0 },
    { "CRATE",           B_CRATE,        1, B_PLANK_OAK, 6, 0,0, 0,0, 0 },
};
const int g_recipeCount = (int)(sizeof(g_recipes) / sizeof(g_recipes[0]));

// ------------------------------------------------------------------ helpers
static const float PLAYER_HW = 0.30f;
static const float PLAYER_H = 1.80f;

static bool aabbBlocked(World& w, float x, float y, float z) {
    int x0 = floori(x - PLAYER_HW), x1 = floori(x + PLAYER_HW);
    int y0 = floori(y), y1 = floori(y + PLAYER_H);
    int z0 = floori(z - PLAYER_HW), z1 = floori(z + PLAYER_HW);
    for (int yy = y0; yy <= y1; yy++)
        for (int zz = z0; zz <= z1; zz++)
            for (int xx = x0; xx <= x1; xx++) {
                u16 b = w.get(xx, yy, zz);
                if (bIsSolid(b) && !bIsLiquid(b)) return true;
            }
    return false;
}

static inline bool inWater(World& w, const v3& p) {
    return bIsLiquid(w.get(floori(p.x), floori(p.y + 0.3f), floori(p.z)));
}

void Game::notify(const char* msg) { toast = msg; toastTime = 3.2f; }

// ------------------------------------------------------------------ drops
static void dropOf(u16 block, u16& item, int& count) {
    item = block; count = 1;
    switch (block) {
    case B_GRASS: case B_GRASS_DRY: case B_GRASS_SNOW: item = B_DIRT; break;
    case B_ORE_COPPER: item = I_NUGGET_COPPER; count = 2; break;
    case B_ORE_TIN: item = I_NUGGET_COPPER; count = 1; break;
    case B_ORE_LEAD: item = I_NUGGET_COPPER; count = 1; break;
    case B_ORE_ZINC: item = I_NUGGET_COPPER; count = 1; break;
    case B_ORE_IRON: item = I_NUGGET_IRON; count = 3; break;
    case B_ORE_MAGNETITE: item = I_NUGGET_IRON; count = 3; break;
    case B_ORE_GOLD: item = I_NUGGET_GOLD; count = 2; break;
    case B_ORE_SILVER: item = I_NUGGET_GOLD; count = 1; break;
    case B_ORE_COAL: item = I_COAL; count = 3; break;
    case B_ORE_LIGNITE: item = I_COAL; count = 2; break;
    case B_ORE_QUARTZ: item = I_FLINT; count = 2; break;
    case B_CLAY: item = I_CLAYBALL; count = 2; break;
    case B_TALLGRASS: case B_TALLGRASS_DRY: case B_FERN: item = I_SEEDS; count = 1; break;
    case B_LEAF_PINE: case B_LEAF_OAK: case B_LEAF_BIRCH: case B_LEAF_MAPLE:
    case B_LEAF_ACACIA: case B_LEAF_KAPOK: item = I_STICK; count = 1; break;
    case B_CROP_5: item = I_CROP; count = 2; break;
    case B_CROP_4: item = I_SEEDS; count = 1; break;
    case B_BERRYBUSH: item = I_BERRIES; count = 2; break;
    case B_STONE_BRICK: item = B_STONE_BRICK; break;
    default:
        if (block >= B_CROP_0 && block < B_CROP_5) item = I_SEEDS;
        break;
    }
}

// ------------------------------------------------------------------ inventory
int Game::addItem(u16 item, int count) {
    if (!item) return count;
    ItemDef* def = itemDef(item);
    int maxStack = def ? (def->stack ? def->stack : 100) : 100;
    if (maxStack > 100) maxStack = 100;
    for (int i = 0; i < 30 && count > 0; i++) {
        if (inv[i].item == item && inv[i].count < maxStack) {
            int add = imin(count, maxStack - inv[i].count);
            inv[i].count += (u8)add; count -= add;
        }
    }
    for (int i = 0; i < 30 && count > 0; i++) {
        if (inv[i].item == 0) {
            int add = imin(count, maxStack);
            inv[i].item = item; inv[i].count = (u8)add; count -= add;
            if (def && def->durability) inv[i].dur = def->durability;
        }
    }
    return count;
}

int Game::countItem(u16 item) {
    int n = 0;
    for (int i = 0; i < 30; i++) if (inv[i].item == item) n += inv[i].count;
    return n;
}

bool Game::removeItem(u16 item, int count) {
    if (countItem(item) < count) return false;
    for (int i = 0; i < 30 && count > 0; i++) {
        if (inv[i].item == item) {
            int take = imin(count, (int)inv[i].count);
            inv[i].count -= (u8)take; count -= take;
            if (inv[i].count == 0) { inv[i].item = 0; inv[i].dur = 0; }
        }
    }
    return true;
}

void Game::damageTool(int idx, int amount) {
    if (idx < 0 || idx >= 30) return;
    if (inv[idx].dur == 0) return;
    ItemDef* d = itemDef(inv[idx].item);
    if (!d || !d->durability) return;
    inv[idx].dur = (u16)imax(0, (int)inv[idx].dur - amount);
    if (inv[idx].dur == 0) { inv[idx].item = 0; inv[idx].count = 0; notify("YOUR TOOL BROKE"); }
}

// ------------------------------------------------------------------ crafting
void Game::recipesAvailable(std::vector<int>& out) {
    out.clear();
    bool nearFire = false;
    for (int dy = -3; dy <= 3 && !nearFire; dy++)
        for (int dz = -3; dz <= 3 && !nearFire; dz++)
            for (int dx = -3; dx <= 3; dx++) {
                u16 b = world.get(floori(pos.x) + dx, floori(pos.y) + dy, floori(pos.z) + dz);
                if (b == B_FIRE || b == B_FIREPIT || b == B_LAVA) { nearFire = true; break; }
            }
    for (int i = 0; i < g_recipeCount; i++) {
        const Recipe& r = g_recipes[i];
        if (r.a && countItem(r.a) < r.an) continue;
        if (r.b && countItem(r.b) < r.bn) continue;
        if (r.c && countItem(r.c) < r.cn) continue;
        if (r.fire && !nearFire) continue;
        out.push_back(i);
    }
}

void Game::craft(int idx) {
    if (idx < 0 || idx >= g_recipeCount) return;
    const Recipe& r = g_recipes[idx];
    std::vector<int> avail;
    recipesAvailable(avail);
    bool ok = false;
    for (int a : avail) if (a == idx) ok = true;
    if (!ok) { notify(r.fire ? "NEEDS A FIREPIT" : "MISSING MATERIALS"); return; }
    if (r.a) removeItem(r.a, r.an);
    if (r.b) removeItem(r.b, r.bn);
    if (r.c) removeItem(r.c, r.cn);
    int left = addItem(r.out, r.outCount);
    if (left) notify("INVENTORY FULL");
    char buf[96];
    snprintf(buf, sizeof(buf), "CRAFTED %s", r.name);
    notify(buf);
}

// ------------------------------------------------------------------ raycast
BlockHit Game::raycast(float maxDist) {
    BlockHit h;
    v3 dir(cosf(yaw) * cosf(pitch), sinf(pitch), sinf(yaw) * cosf(pitch));
    v3 o = eyePos();
    float step = 0.04f;
    int lastX = floori(o.x), lastY = floori(o.y), lastZ = floori(o.z);
    for (float t = 0; t < maxDist; t += step) {
        v3 p = o + dir * t;
        int bx = floori(p.x), by = floori(p.y), bz = floori(p.z);
        u16 b = world.get(bx, by, bz);
        if (b != B_AIR && !bIsLiquid(b)) {
            h.hit = true; h.x = bx; h.y = by; h.z = bz; h.dist = t;
            h.nx = lastX - bx; h.ny = lastY - by; h.nz = lastZ - bz;
            return h;
        }
        lastX = bx; lastY = by; lastZ = bz;
    }
    return h;
}

// ------------------------------------------------------------------ world start
void Game::startWorld(int size, u64 seed, int gameMode) {
    mode = gameMode;
    world.init(size, seed);
    entities.clear();
    particles.clear();
    memset(inv, 0, sizeof(inv));
    health = maxHealth; satiety = maxSatiety; stability = 100; temperature = 18;
    dayTime = 0.30f;
    int cx = size / 2, cz = size / 2;
    int bestY = -1;
    for (int r = 0; r < size / 2 && bestY < 0; r += 16) {
        for (int a = 0; a < 24; a++) {
            float ang = a * TAU / 24.f;
            int x = cx + (int)(cosf(ang) * r), z = cz + (int)(sinf(ang) * r);
            if (!world.inside(x, z)) continue;
            int h = world.coarse.getHeight(x / world.coarse.res, z / world.coarse.res);
            if (h > SEA_LEVEL + 2) {
                int bh = h * 2;
                const CoarseMap& cm = world.coarse;
                int bi = cm.biome.empty() ? 3 : cm.biome[cm.at(x / cm.res, z / cm.res)];
                float t = -12.f + biomeTemp(bi) * 0.45f + 6.f - (bh - SEA_LEVEL) * 0.030f;
                if (t < 4.f && r < size / 3) continue;
                cx = x; cz = z; bestY = h; break;
            }
        }
    }
    if (bestY < 0) bestY = SEA_LEVEL + 4;
    for (int dz = -2; dz <= 2; dz++)
        for (int dx = -2; dx <= 2; dx++)
            world.getOrCreateChunk(cx / CH + dx, cz / CH + dz);
    int gy = world.heightAt(cx, cz);
    if (gy <= 1) gy = bestY;
    pos = v3((float)cx + 0.5f, (float)gy + 0.1f, (float)cz + 0.5f);
    vel = v3(0, 0, 0);
    yaw = 0.7f; pitch = -0.12f;
    if (mode == GM_CREATIVE) {
        inv[0].item = I_PICK_IRON; inv[0].count = 1; inv[0].dur = 600;
        inv[1].item = I_AXE_IRON; inv[1].count = 1; inv[1].dur = 600;
        inv[2].item = I_PROSPICK_COPPER; inv[2].count = 1; inv[2].dur = 200;
        inv[3].item = B_TORCH; inv[3].count = 64;
        inv[4].item = B_PLANK_OAK; inv[4].count = 64;
        inv[5].item = B_STONE_BRICK; inv[5].count = 64;
        inv[6].item = B_GLASS; inv[6].count = 64;
        inv[7].item = B_FIREPIT; inv[7].count = 4;
        inv[8].item = I_SEEDS; inv[8].count = 32;
    } else {
        inv[0].item = I_KNIFE_FLINT; inv[0].count = 1; inv[0].dur = 60;
        inv[1].item = I_AXE_FLINT; inv[1].count = 1; inv[1].dur = 60;
        inv[2].item = I_PICK_FLINT; inv[2].count = 1; inv[2].dur = 60;
        inv[3].item = B_TORCH; inv[3].count = 6;
        inv[4].item = I_BREAD; inv[4].count = 3;
        inv[5].item = I_SEEDS; inv[5].count = 8;
    }
    spawnEntities();
}

const char* Game::biomeNameAt(int x, int z) {
    const CoarseMap& m = world.coarse;
    int cx = clampi(x / m.res, 0, m.cells - 1), cz = clampi(z / m.res, 0, m.cells - 1);
    if (m.biome.empty()) return "???";
    return biomeName(m.biome[m.at(cx, cz)]);
}

// ------------------------------------------------------------------ environment
float Game::groundTemperature() {
    const CoarseMap& m = world.coarse;
    int cx = clampi(floori(pos.x) / m.res, 0, m.cells - 1);
    int cz = clampi(floori(pos.z) / m.res, 0, m.cells - 1);
    int bi = m.biome.empty() ? 3 : m.biome[m.at(cx, cz)];
    float seasonAdj[4] = { 0.f, 6.f, -1.f, -12.f };
    float base = -12.f + biomeTemp(bi) * 0.45f + seasonAdj[clampi(season, 0, 3)];
    base -= (pos.y - SEA_LEVEL) * 0.030f;                  // altitude
    float nightDip = 8.f * saturate(1.f - cosf(dayTime * TAU) * 0.5f - 0.5f);
    base -= nightDip;
    if (weather) base -= 4.f;
    // heat sources nearby
    for (int dy = -2; dy <= 3; dy++)
        for (int dz = -4; dz <= 4; dz++)
            for (int dx = -4; dx <= 4; dx++) {
                u16 b = world.get(floori(pos.x) + dx, floori(pos.y) + dy, floori(pos.z) + dz);
                if (b == B_FIRE || b == B_FIREPIT) { base += 14.f; dy = 9; dz = 9; dx = 9; }
                else if (b == B_TORCH) base += 1.2f;
            }
    return base;
}

void Game::updateEnvironment(float dt) {
    dayTime += dt / dayLength;
    if (dayTime > 1.f) {
        dayTime -= 1.f;
        dayCount++;
        season = (dayCount / 8) % 4;
        if (season == 0 && dayCount > 0) year = 1 + dayCount / 32;
    }
    weatherTimer -= dt;
    if (weatherTimer <= 0) {
        weatherTimer = 90.f + (float)(rand() % 180);
        int bi = 3;
        const CoarseMap& m = world.coarse;
        if (!m.biome.empty()) {
            int cx = clampi(floori(pos.x) / m.res, 0, m.cells - 1);
            int cz = clampi(floori(pos.z) / m.res, 0, m.cells - 1);
            bi = m.biome[m.at(cx, cz)];
        }
        int r = rand() % 100;
        if (r < 68) weather = 0;
        else weather = (biomeTemp(bi) < 25 || season == 3) ? 2 : 1;
    }
    temperature = groundTemperature();
    if (mode == GM_SURVIVAL) {
        satiety -= dt * 0.32f;
        if (satiety < 0) { satiety = 0; health -= dt * 0.8f; }
        if (temperature < 1.f) { health -= dt * (0.3f + saturate((1.f - temperature) / 40.f) * 1.3f); }
        if (temperature > 34.f) { health -= dt * 1.0f; }
        if (health < maxHealth) health += dt * 0.35f * (satiety > 40 ? 1.f : 0.f);
        if (health <= 0) {
            health = maxHealth * 0.5f; satiety = maxSatiety * 0.5f;
            pos.y = (float)imax(world.heightAt(floori(pos.x), floori(pos.z)), SEA_LEVEL) + 1.f;
            vel = v3(0, 0, 0);
            notify("YOU DIED - RESPAWNED AT THE SURFACE");
        }
        // temporal stability
        int lightHere = world.skyLight(floori(pos.x), floori(pos.y + 1), floori(pos.z));
        bool underground = lightHere < 5 && world.heightAt(floori(pos.x), floori(pos.z)) > pos.y + 2;
        if (underground || isNight()) stability -= dt * 0.55f;
        else stability += dt * 0.8f;
        stability = clampf(stability, 0.f, 100.f);
    } else {
        satiety = maxSatiety;
        health = maxHealth;
        stability = 100;
    }
    if (toastTime > 0) toastTime -= dt;
    if (prospectTime > 0) prospectTime -= dt;
}

// ------------------------------------------------------------------ entities
void Game::addParticles(v3 at, u32 color, int n, float spread, float life) {
    Rng r((u32)(at.x * 733.f + at.z * 977.f + world.tick));
    for (int i = 0; i < n; i++) {
        Particle p;
        p.pos = at + v3((r.f() - 0.5f) * spread, (r.f() - 0.5f) * spread, (r.f() - 0.5f) * spread);
        p.vel = v3((r.f() - 0.5f) * 3.f, r.f() * 3.f + 1.f, (r.f() - 0.5f) * 3.f);
        p.color = color;
        p.life = life * (0.6f + r.f() * 0.7f);
        p.size = 0.07f + r.f() * 0.08f;
        particles.push_back(p);
    }
}

void Game::spawnMob(int type, v3 at) {
    if (entities.size() > 40) return;
    Entity e;
    e.type = type;
    e.pos = at;
    e.vel = v3(0, 0, 0);
    e.yaw = (float)(rand() % 628) / 100.f;
    switch (type) {
    case ENT_HARE: e.health = 6; break;
    case ENT_CHICKEN: e.health = 5; break;
    case ENT_WOLF: e.health = 22; break;
    case ENT_DRIFTER: e.health = 35; break;
    }
    entities.push_back(e);
}

void Game::spawnEntities() {
    if (entities.size() > 26) return;
    Rng r((u32)(pos.x * 131.f) ^ (u32)(pos.z * 977.f) ^ (u32)(dayTime * 100000.f));
    float ang = r.range(0, TAU);
    float dist = r.range(22.f, 46.f);
    int x = floori(pos.x + cosf(ang) * dist), z = floori(pos.z + sinf(ang) * dist);
    if (!world.inside(x, z)) return;
    if (!world.chunkOf(x, z) || !world.chunkOf(x, z)->generated) return;
    int h = world.heightAt(x, z);
    if (h <= SEA_LEVEL) return;
    u16 on = world.get(x, h - 1, z);
    if (bIsLiquid(on)) return;
    v3 at((float)x + 0.5f, (float)h, (float)z + 0.5f);
    bool night = isNight();
    float hostileChance = night ? 0.55f : (stability < 45.f ? 0.35f : 0.06f);
    if (r.f() < hostileChance) spawnMob(r.chance(0.6f) ? ENT_DRIFTER : ENT_WOLF, at);
    else spawnMob(r.chance(0.5f) ? ENT_HARE : ENT_CHICKEN, at);
}

void Game::damagePlayer(float amount, const char* cause) {
    if (mode == GM_CREATIVE) return;
    health -= amount;
    if (toastTime <= 0) notify(cause);
}

void Game::updateEntities(float dt) {
    for (size_t i = 0; i < entities.size(); i++) {
        Entity& e = entities[i];
        e.timer -= dt;
        e.attackCd -= dt;
        if (e.hurtFlash > 0) e.hurtFlash -= dt;
        v3 toPlayer = pos - e.pos;
        float dist = toPlayer.len();
        bool hostile = (e.type == ENT_WOLF || e.type == ENT_DRIFTER);
        float speed = e.type == ENT_HARE ? 3.2f : (e.type == ENT_CHICKEN ? 1.6f : (e.type == ENT_WOLF ? 4.2f : 2.6f));

        if (hostile && dist < 22.f && !(e.type == ENT_DRIFTER && !isNight() && world.skyLight(floori(e.pos.x), floori(e.pos.y) + 1, floori(e.pos.z)) > 8)) {
            v3 dir = toPlayer.norm();
            e.vel.x = dir.x * speed;
            e.vel.z = dir.z * speed;
            e.yaw = atan2f(dir.z, dir.x);
            if (dist < 2.2f && e.attackCd <= 0) {
                e.attackCd = 1.3f;
                damagePlayer(e.type == ENT_DRIFTER ? 9.f : 6.f, e.type == ENT_DRIFTER ? "A DRIFTER HIT YOU" : "A WOLF BIT YOU");
                addParticles(e.pos + v3(0, 1.f, 0), 0xff3030, 6, 0.6f, 0.5f);
            }
        } else {
            if (e.timer <= 0) {
                e.timer = 2.5f + (float)(rand() % 40) / 10.f;
                e.wanderDir = rand() % 4;
                if (!hostile && dist < 8.f) e.wanderDir = (rand() % 4);
            }
            v3 dir(0, 0, 0);
            if (e.wanderDir == 0) dir.x = 1; else if (e.wanderDir == 1) dir.x = -1;
            else if (e.wanderDir == 2) dir.z = 1; else if (e.wanderDir == 3) dir.z = -1;
            if (!hostile && dist < 7.f) { dir = (e.pos - pos).norm(); speed *= 1.6f; }
            e.vel.x = dir.x * speed * 0.5f;
            e.vel.z = dir.z * speed * 0.5f;
            if (dir.len2() > 0.01f) e.yaw = atan2f(dir.z, dir.x);
        }
        // gravity + ground
        e.vel.y -= 26.f * dt;
        v3 np = e.pos + e.vel * dt;
        int bx = floori(np.x), bz = floori(np.z);
        int groundY = -1;
        for (int y = floori(np.y) + 2; y > floori(np.y) - 3; y--) {
            u16 b = world.get(bx, y, bz);
            if (bIsSolid(b)) { groundY = y; break; }
        }
        if (e.vel.y < 0 && groundY >= 0 && np.y < groundY + 1.f) {
            np.y = groundY + 1.f; e.vel.y = 0;
            if (e.type == ENT_HARE && rand() % 100 < 5) e.vel.y = 5.f * (float)(rand() % 100) / 100.f;
        }
        if (np.y < 1) np.y = 1;
        // keep inside the world border
        np.x = clampf(np.x, 1.5f, (float)world.size - 2.5f);
        np.z = clampf(np.z, 1.5f, (float)world.size - 2.5f);
        e.pos = np;
        e.animPhase += dt * (fabsf(e.vel.x) + fabsf(e.vel.z)) * 2.f;
        if (e.type == ENT_DRIFTER && !isNight() && e.health > 0) {
            if (world.skyLight(floori(e.pos.x), floori(e.pos.y) + 2, floori(e.pos.z)) > 10) {
                e.health -= dt * 12.f;
                if (e.health <= 0) addParticles(e.pos + v3(0, 1, 0), 0x303040, 12, 0.8f, 0.8f);
            }
        }
        if (e.health <= 0 || e.pos.y < -4) e.dead = true;
        if (dist > 90.f) e.dead = true;
    }
    for (int i = (int)entities.size() - 1; i >= 0; i--)
        if (entities[i].dead) entities.erase(entities.begin() + i);

    // particles
    for (int i = (int)particles.size() - 1; i >= 0; i--) {
        Particle& p = particles[i];
        p.vel.y -= 18.f * dt;
        p.pos += p.vel * dt;
        p.life -= dt;
        if (p.life <= 0) particles.erase(particles.begin() + i);
    }
    // spawning
    static float spawnTimer = 0;
    spawnTimer -= dt;
    if (spawnTimer <= 0) { spawnTimer = 2.5f; spawnEntities(); }
}

// ------------------------------------------------------------------ player
void Game::updatePlayer(float dt, InputState& in) {
    float speed = flying ? 14.f : (sprinting ? 6.2f : 4.3f);
    if (inWater(world, pos)) speed *= 0.6f;
    if (mode == GM_CREATIVE && in.key['V']) flying = true;

    v3 fwd(cosf(yaw), 0, sinf(yaw));
    v3 right(-sinf(yaw), 0, cosf(yaw));
    v3 wish(0, 0, 0);
    if (in.key['W']) wish += fwd;
    if (in.key['S']) wish -= fwd;
    if (in.key['D']) wish += right;
    if (in.key['A']) wish -= right;
    if (wish.len2() > 0.01f) wish = wish.norm();
    sprinting = in.key[0x10] || in.key['R'];   // shift or R

    float accel = onGround ? 14.f : 4.f;
    v3 target = wish * speed;
    vel.x += (target.x - vel.x) * clampf(accel * dt, 0.f, 1.f);
    vel.z += (target.z - vel.z) * clampf(accel * dt, 0.f, 1.f);

    if (flying) {
        vel.y = 0;
        if (in.key[' ']) vel.y = speed * 0.8f;
        if (in.key['C']) vel.y = -speed * 0.8f;
    } else {
        vel.y -= 28.f * dt;
        if (inWater(world, pos)) {
            vel.y = imax(vel.y, -3.f);
            if (in.key[' ']) vel.y = 4.5f;
        } else if (in.key[' '] && onGround) {
            vel.y = 8.6f;
            onGround = false;
        }
        if (vel.y < -60.f) vel.y = -60.f;
    }

    // axis separated movement with 1 block auto step
    float dx = vel.x * dt, dz = vel.z * dt, dy = vel.y * dt;
    bool stepped = false;
    if (dx != 0) {
        pos.x += dx;
        if (aabbBlocked(world, pos.x, pos.y, pos.z)) {
            if (!flying && onGround && !aabbBlocked(world, pos.x, pos.y + 1.02f, pos.z)) { pos.y += 1.02f; stepped = true; }
            else { pos.x -= dx; vel.x = 0; }
        }
    }
    if (dz != 0) {
        pos.z += dz;
        if (aabbBlocked(world, pos.x, pos.y, pos.z)) {
            if (!flying && onGround && !aabbBlocked(world, pos.x, pos.y + 1.02f, pos.z)) { pos.y += 1.02f; stepped = true; }
            else { pos.z -= dz; vel.z = 0; }
        }
    }
    onGround = false;
    if (dy != 0 || !flying) {
        pos.y += flying ? 0 : dy;
        if (!flying) {
            if (aabbBlocked(world, pos.x, pos.y, pos.z)) {
                pos.y -= dy;
                if (vel.y < 0) onGround = true;
                vel.y = 0;
            }
        }
    }
    if (flying && (in.key[' '] || in.key['C'])) {
        pos.y += vel.y * dt;
        if (aabbBlocked(world, pos.x, pos.y, pos.z)) pos.y -= vel.y * dt;
    }
    (void)stepped;
    if (pos.y < -8) { pos.y = (float)world.heightAt(floori(pos.x), floori(pos.z)) + 1.f; vel.y = 0; }
    // world border
    pos.x = clampf(pos.x, 0.6f, (float)world.size - 0.6f);
    pos.z = clampf(pos.z, 0.6f, (float)world.size - 0.6f);

    if (swing > 0) swing -= dt * 4.f;
}

// ------------------------------------------------------------------ interaction
void Game::tryBreak(float dt) {
    if (!hit.hit) { breakProgress = 0; return; }
    u16 b = world.get(hit.x, hit.y, hit.z);
    if (b == B_AIR || hit.y <= 2) return;
    if (hit.x != breakX || hit.y != breakY || hit.z != breakZ) {
        breakX = hit.x; breakY = hit.y; breakZ = hit.z; breakProgress = 0;
    }
    BlockDef& d = g_blocks[blockId(b)];
    Slot& held = selected();
    ItemDef* hd = itemDef(held.item);
    int tier = hd ? hd->tier : 0;
    bool correctTool = hd && d.tool != TOOL_NONE && hd->tool == d.tool;
    if (d.minTier > tier && mode != GM_CREATIVE) {
        if (toastTime <= 0) notify("YOU NEED A BETTER TOOL");
        breakProgress = 0;
        return;
    }
    float speed = 1.f;
    if (correctTool) speed = 1.8f + tier * 1.6f;
    else if (hd && hd->tool != TOOL_NONE) speed = 0.55f;
    float need = d.hardness * 0.055f / speed;
    if (mode == GM_CREATIVE) need = 0.05f;
    if (d.hardness >= 250) need = 999.f;
    breakProgress += dt;
    swing = imax(swing, 1.f);
    if (breakProgress >= need) {
        breakProgress = 0;
        u16 item; int cnt;
        dropOf(blockId(b), item, cnt);
        if (mode != GM_CREATIVE) {
            addItem(item, cnt);
            if (correctTool) damageTool(sel, 1);
        }
        addParticles(v3((float)hit.x + 0.5f, (float)hit.y + 0.5f, (float)hit.z + 0.5f),
                     textureAvgColor(g_blocks[blockId(b)].texSide), 8, 0.7f, 0.7f);
        world.set(hit.x, hit.y, hit.z, B_AIR);
        world.relightAround(hit.x, hit.y, hit.z);
        // plants above fall
        u16 above = world.get(hit.x, hit.y + 1, hit.z);
        if (bIsPlant(above)) { world.set(hit.x, hit.y + 1, hit.z, B_AIR); world.relightAround(hit.x, hit.y + 1, hit.z); }
        breakX = -9999;
    }
}

void Game::tryPlace() {
    if (!hit.hit) return;
    Slot& held = selected();
    if (!held.item) return;
    if (held.item >= 0x3000) return;   // not a block
    int px = hit.x + hit.nx, py = hit.y + hit.ny, pz = hit.z + hit.nz;
    u16 cur = world.get(px, py, pz);
    if (cur != B_AIR && !bIsReplaceable(cur)) return;
    // do not place inside the player
    float bx = px + 0.5f, bz = pz + 0.5f;
    if (fabsf(bx - pos.x) < 0.8f && fabsf(bz - pos.z) < 0.8f &&
        py >= floori(pos.y) - 0 && py <= floori(pos.y + PLAYER_H)) return;
    if (bIsPlant((u16)held.item)) {
        u16 below = world.get(px, py - 1, pz);
        if (!bIsSolid(below)) return;
    }
    world.set(px, py, pz, held.item);
    world.relightAround(px, py, pz);
    if (mode != GM_CREATIVE) {
        held.count--;
        if (held.count == 0) { held.item = 0; held.dur = 0; }
    }
    placeCooldown = 0.18f;
}

void Game::attack() {
    if (attackCooldown > 0) return;
    attackCooldown = 0.45f;
    swing = 1.f;
    v3 eye = eyePos();
    v3 dir(cosf(yaw) * cosf(pitch), sinf(pitch), sinf(yaw) * cosf(pitch));
    Entity* bestE = nullptr; float bestT = 4.0f;
    for (auto& e : entities) {
        v3 c = e.pos + v3(0, 0.5f, 0);
        v3 d = c - eye;
        float t = d.dot(dir);
        if (t < 0 || t > bestT) continue;
        float perp = (d - dir * t).len();
        if (perp < 0.9f) { bestT = t; bestE = &e; }
    }
    if (bestE) {
        Slot& held = selected();
        ItemDef* hd = itemDef(held.item);
        float dmg = 4.f;
        if (hd) {
            if (hd->tool == TOOL_AXE || hd->tool == TOOL_KNIFE) dmg = 6.f + hd->tier * 3.f;
            else if (hd->tool == TOOL_PICK || hd->tool == TOOL_SHOVEL) dmg = 5.f + hd->tier * 2.f;
            if (held.item == I_SWORD_COPPER || (held.item >= I_SWORD_COPPER && held.item <= I_SWORD_IRON)) dmg = 8.f + hd->tier * 4.f;
        }
        bestE->health -= dmg;
        bestE->hurtFlash = 0.35f;
        addParticles(bestE->pos + v3(0, 0.6f, 0), 0xcc3333, 8, 0.5f, 0.5f);
        if (bestE->health <= 0) {
            bestE->dead = true;
            if (mode != GM_CREATIVE) {
                if (bestE->type == ENT_HARE || bestE->type == ENT_CHICKEN) addItem(I_MEAT_RAW, 1);
                if (bestE->type == ENT_DRIFTER) addItem(I_GEAR, 1);
                if (bestE->type == ENT_WOLF) addItem(I_TALLOW, 1);
            }
        }
        if (hd && hd->durability) damageTool(sel, 1);
        return;
    }
    // no entity: dig
    if (hit.hit) tryBreak(0.35f);
}

void Game::useItem() {
    Slot& held = selected();
    if (!held.item) return;
    ItemDef* d = itemDef(held.item);
    if (!d) return;
    if (d->food > 0) {
        satiety = clampf(satiety + d->food, 0, maxSatiety);
        held.count--;
        if (!held.count) { held.item = 0; held.dur = 0; }
        notify("YUM");
        return;
    }
    // plant seeds
    if (held.item == I_SEEDS && hit.hit) {
        u16 on = world.get(hit.x, hit.y, hit.z);
        if (on == B_GRASS || on == B_GRASS_DRY || on == B_DIRT) {
            if (world.get(hit.x, hit.y + 1, hit.z) == B_AIR) {
                world.set(hit.x, hit.y + 1, hit.z, B_CROP_0);
                held.count--;
                if (!held.count) held.item = 0;
                notify("PLANTED");
            }
        }
        return;
    }
    // prospecting
    if (held.item == I_PROSPICK_COPPER || held.item == I_PROSPICK_BRONZE || held.item == I_PROSPICK_IRON) {
        if (hit.hit) {
            char buf[128];
            int counts[11]; memset(counts, 0, sizeof(counts));
            int total = 0;
            for (int dy = -14; dy <= 14; dy += 2)
                for (int dz = -10; dz <= 10; dz += 2)
                    for (int dx = -10; dx <= 10; dx += 2) {
                        u16 bb = world.get(hit.x + dx, hit.y + dy, hit.z + dz);
                        for (int i = 0; i < 11; i++)
                            if (bb == B_ORE_COPPER + i) { counts[i]++; total++; }
                    }
            if (!total) snprintf(buf, sizeof(buf), "NO ORE DETECTED NEARBY");
            else {
                int best = 0;
                for (int i = 1; i < 11; i++) if (counts[i] > counts[best]) best = i;
                snprintf(buf, sizeof(buf), "%s: %s TRACES (%d HITS)",
                         g_blocks[B_ORE_COPPER + best].name, counts[best] > 20 ? "STRONG" : (counts[best] > 8 ? "MODERATE" : "WEAK"), total);
            }
            prospectText = buf;
            prospectTime = 6.f;
        }
        return;
    }
    if (held.item < B_COUNT) tryPlace();
}

// ------------------------------------------------------------------ update
void Game::update(float dt, InputState& in) {
    if (invOpen || craftOpen || paused) {
        updateEnvironment(dt);
        updateEntities(dt);
        return;
    }
    updatePlayer(dt, in);
    updateEnvironment(dt);
    updateEntities(dt);
    hit = raycast(6.0f);
    placeCooldown -= dt;
    attackCooldown -= dt;

    if (in.mouse[0] && attackCooldown <= 0) {
        if (mode == GM_CREATIVE && in.key[0x11]) { /* ctrl = break instantly */ }
        attack();
        if (hit.hit) tryBreak(0.0001f);
    }
    if (in.mouse[0]) tryBreak(dt);
    if (in.mouse[1] && placeCooldown <= 0) useItem();

    for (int i = 0; i < 10; i++) {
        int key = (i == 9) ? '0' : ('1' + i);
        if (in.keyDown[key]) sel = i;
    }
    if (in.wheel != 0) {
        sel = (sel + (in.wheel > 0 ? 1 : -1) + 10) % 10;
    }
    // crop growth
    tickAccum += dt;
    if (tickAccum > 1.0f) {
        tickAccum = 0;
        world.tick++;
        for (int i = 0; i < 6; i++) {
            int x = floori(pos.x) + (rand() % 33) - 16;
            int z = floori(pos.z) + (rand() % 33) - 16;
            int y = world.heightAt(x, z);
            if (y <= 0) continue;
            u16 b = world.get(x, y - 1, z);
            if (b >= B_CROP_0 && b < B_CROP_5 && temperature > 4.f && season != 3) {
                if (rand() % 3 == 0) world.set(x, y - 1, z, (u16)(b + 1));
            }
        }
    }
}
