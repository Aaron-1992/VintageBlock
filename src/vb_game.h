// vb_game.h : player, inventory, crafting, entities and survival systems
#pragma once
#include "vb_world.h"
#include "vb_mesh.h"

enum GameMode { GM_SURVIVAL = 0, GM_CREATIVE = 1 };

struct Slot {
    u16 item = 0;
    u8 count = 0;
    u16 dur = 0;
};

struct InputState {
    bool key[256] = {};
    bool keyDown[256] = {};      // pressed this frame
    bool mouse[3] = {};
    bool mouseDown[3] = {};
    float mdX = 0, mdY = 0;
    float wheel = 0;
    float dt = 0.016f;
    void endFrame() {
        memset(keyDown, 0, sizeof(keyDown));
        memset(mouseDown, 0, sizeof(mouseDown));
        mdX = mdY = 0; wheel = 0;
    }
};

enum EntityType { ENT_HARE = 0, ENT_WOLF, ENT_DRIFTER, ENT_CHICKEN, ENT_COUNT };

struct Entity {
    v3 pos, vel;
    float yaw = 0;
    int type = ENT_HARE;
    float health = 10;
    float timer = 0;
    float attackCd = 0;
    float hurtFlash = 0;
    bool dead = false;
    int wanderDir = 0;
    float animPhase = 0;
};

struct Recipe {
    const char* name;
    u16 out;
    u8 outCount;
    u16 a; u8 an;
    u16 b; u8 bn;
    u16 c; u8 cn;
    u8 fire;         // requires a firepit / heat source nearby
};

struct Particle {
    v3 pos, vel;
    u32 color;
    float life;
    float size;
};

struct BlockHit { bool hit = false; int x = 0, y = 0, z = 0; int nx = 0, ny = 0, nz = 0; float dist = 0; };

struct Game {
    World world;
    int mode = GM_SURVIVAL;
    v3 pos, vel;
    float yaw = 0, pitch = 0;
    bool onGround = false, flying = false, sprinting = false;
    float eyeHeight = 1.62f;

    // survival stats
    float health = 100, maxHealth = 100;
    float satiety = 100, maxSatiety = 100;
    float temperature = 20;        // degrees C
    float stability = 100;         // temporal stability
    float bodyHeat = 20;

    // inventory : 10 hotbar + 20 backpack
    Slot inv[30];
    int sel = 0;
    bool invOpen = false;
    bool craftOpen = false;
    bool paused = false;
    int craftScroll = 0;
    std::string toast;
    float toastTime = 0;

    std::vector<Entity> entities;
    std::vector<Particle> particles;

    // world interaction
    BlockHit hit;
    float breakProgress = 0;
    int breakX = -9999, breakY = -9999, breakZ = -9999;
    float placeCooldown = 0;
    float attackCooldown = 0;
    float swing = 0;

    // voxel streaming
    float viewDistance = 7;        // chunks of full detail
    u32 streamTick = 0;

    // environment
    float dayTime = 0.30f;         // 0 = midnight, 0.5 = noon
    float dayLength = 900.f;       // seconds per full cycle
    int weather = 0;               // 0 clear, 1 rain, 2 snow
    float weatherTimer = 120;
    int dayCount = 0;
    int season = 1;                // 0 spring 1 summer 2 autumn 3 winter
    int year = 1;
    float tickAccum = 0;
    float fps = 0;

    // prospecting result
    std::string prospectText;
    float prospectTime = 0;

    // ui state (main menu / inventory)
    int screen = 0;                 // 0 menu, 1 game
    int uiSize = 1;                 // 0=256 1=512 2=1024
    int uiMode = 0;
    char uiSeed[24] = "1207";
    bool uiSeedEditing = false;
    bool showDebug = false;
    float menuFade = 0;
    float damageFlash = 0.f;
    float lastHealth = 100.f;

    void startWorld(int size, u64 seed, int gameMode);
    bool saveGame();
    bool loadGame();
    void update(float dt, InputState& in);
    void updatePlayer(float dt, InputState& in);
    void updateEntities(float dt);
    void updateEnvironment(float dt);
    void updateStreaming(float dt);
    float groundTemperature();

    BlockHit raycast(float maxDist);
    void tryBreak(float dt);
    void tryPlace();
    void attack();
    void useItem();

    // inventory helpers
    int addItem(u16 item, int count);
    bool removeItem(u16 item, int count);
    int countItem(u16 item);
    void damageTool(int slotIndex, int amount);
    Slot& selected() { return inv[sel]; }

    // crafting
    void craft(int recipeIndex);
    void recipesAvailable(std::vector<int>& out);

    // entities
    void spawnEntities();
    void spawnMob(int type, v3 at);
    void damagePlayer(float amount, const char* cause);
    void addParticles(v3 at, u32 color, int n, float spread, float life);

    // helpers
    v3 eyePos() const { return pos + v3(0, eyeHeight, 0); }
    float sunAngle() const { return dayTime * TAU - PI * 0.5f; }
    bool isNight() const { return dayTime < 0.23f || dayTime > 0.77f; }
    void notify(const char* msg);
    const char* biomeNameAt(int x, int z);
};

extern const Recipe g_recipes[];
extern const int g_recipeCount;

// simple 5x7 bitmap font used by the HUD
const u8* fontColumns(char c);   // 5 column bytes, bit0 = top row
bool saveExists();
void deleteSave();
int fontIndex(char c);           // glyph cell index or -1
