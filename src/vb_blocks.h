// vb_blocks.h : block ids, properties and the 8x8 texture atlas
#pragma once
#include "vb_common.h"

// ------------------------------------------------------------------ textures
// every texture is exactly 8x8 pixels.  Tiles are packed into a 16x16 grid of
// 16x16 pixel cells (8x8 art + 4px replicated border) so that nearest-filtered
// mipmapping never bleeds between tiles.
enum TexId : int {
    T_NONE = 0,
    // soil / surface
    T_GRASS_TOP, T_GRASS_SIDE, T_GRASS_DRY_TOP, T_GRASS_DRY_SIDE, T_GRASS_SNOW_TOP, T_GRASS_SNOW_SIDE,
    T_DIRT, T_SAND, T_RED_SAND, T_GRAVEL, T_CLAY, T_PEAT, T_SNOW, T_PERMAFROST,
    // rock
    T_GRANITE, T_ANDESITE, T_BASALT, T_LIMESTONE, T_CHALK, T_SLATE, T_SANDSTONE,
    T_PERIDOTITE, T_KIMBERLITE, T_SHALE,
    T_COBBLE_GREY, T_COBBLE_WARM, T_COBBLE_DARK, T_BRICKS, T_STONE_BRICK, T_POLISHED,
    // ores
    T_ORE_COPPER, T_ORE_TIN, T_ORE_LEAD, T_ORE_ZINC, T_ORE_IRON, T_ORE_MAGNETITE,
    T_ORE_GOLD, T_ORE_SILVER, T_ORE_COAL, T_ORE_LIGNITE, T_ORE_QUARTZ,
    // wood
    T_LOG_PINE_S, T_LOG_PINE_T, T_LOG_OAK_S, T_LOG_OAK_T, T_LOG_BIRCH_S, T_LOG_BIRCH_T,
    T_LOG_MAPLE_S, T_LOG_MAPLE_T, T_LOG_ACACIA_S, T_LOG_ACACIA_T, T_LOG_KAPOK_S, T_LOG_KAPOK_T,
    T_PLANK_PINE, T_PLANK_OAK, T_PLANK_BIRCH, T_PLANK_MAPLE, T_PLANK_ACACIA, T_PLANK_KAPOK,
    T_LEAF_PINE, T_LEAF_OAK, T_LEAF_BIRCH, T_LEAF_MAPLE, T_LEAF_ACACIA, T_LEAF_KAPOK,
    // plants
    T_TALLGRASS, T_TALLGRASS_DRY, T_FERN, T_FLOWER_RED, T_FLOWER_YELLOW, T_FLOWER_BLUE,
    T_FLOWER_PURPLE, T_HEATHER, T_CATTAIL, T_REED, T_MUSHROOM_RED, T_MUSHROOM_BROWN,
    T_CACTUS_S, T_CACTUS_T, T_BERRYBUSH, T_DEADBUSH, T_SAPLING,
    T_CROP_0, T_CROP_1, T_CROP_2, T_CROP_3, T_CROP_4, T_CROP_5,
    // misc blocks
    T_TORCH, T_FIRE, T_GLASS, T_THATCH, T_CHARCOAL, T_ICE, T_WATER, T_LAVA, T_CRATE,
    T_FIREPIT, T_BEDROCK, T_SLAG, T_BLOOM, T_SCAFFOLD,
    // item icons : 8 tool shapes x 3 metal tiers, tier major
    T_I_AXE_C, T_I_PICK_C, T_I_SHOVEL_C, T_I_KNIFE_C, T_I_SWORD_C, T_I_HAMMER_C, T_I_PROSPICK_C, T_I_CHISEL_C,
    T_I_AXE_B, T_I_PICK_B, T_I_SHOVEL_B, T_I_KNIFE_B, T_I_SWORD_B, T_I_HAMMER_B, T_I_PROSPICK_B, T_I_CHISEL_B,
    T_I_AXE_I, T_I_PICK_I, T_I_SHOVEL_I, T_I_KNIFE_I, T_I_SWORD_I, T_I_HAMMER_I, T_I_PROSPICK_I, T_I_CHISEL_I,
    T_I_KNIFE_FLINT, T_I_AXE_FLINT, T_I_SHOVEL_FLINT, T_I_PICK_FLINT,
    T_I_INGOT, T_I_NUGGET, T_I_STICK, T_I_FLINT, T_I_SEEDS, T_I_BERRIES, T_I_MEAT, T_I_CLAYBALL,
    T_I_BOWL, T_I_POT, T_I_ROPE, T_I_TORCH, T_I_GEAR, T_I_CROPS, T_I_BREAD, T_I_STEW, T_I_TALLOW,
    T_I_COAL, T_I_ARROWHEAD, T_I_LOOM,
    // entity skins
    T_E_FUR_BROWN, T_E_FUR_GREY, T_E_FUR_WHITE, T_E_DRIFTER, T_E_DRIFTER_EYE, T_E_BEAK, T_E_EYE,
    T_COUNT
};

static const int ATLAS_GRID = 16;        // 16x16 tiles
static const int ATLAS_CELL = 16;        // pixels per cell (8 art + border)
static const int ATLAS_SIZE = ATLAS_GRID * ATLAS_CELL;   // 256

// Builds tile level `lod` (0:8x8 art, 1:4x4, 2:2x2) and blits into `out`
// which is `size*size` RGBA (0xAARRGGBB order in a u32, uploaded as BGRA).
void buildAtlasLevel(u32* out, int size, int level);
const u32* getAtlasRGBA();

// ------------------------------------------------------------------ blocks
enum BlockId : u16 {
    B_AIR = 0,
    // --- rock (with cobble + brick variants generated per type)
    B_GRANITE, B_ANDESITE, B_BASALT, B_LIMESTONE, B_CHALK, B_SLATE, B_SANDSTONE,
    B_PERIDOTITE, B_KIMBERLITE, B_SHALE,
    B_BEDROCK,
    // --- soil
    B_GRASS, B_GRASS_DRY, B_GRASS_SNOW, B_DIRT, B_DIRT_DRY, B_SAND, B_RED_SAND, B_GRAVEL,
    B_CLAY, B_PEAT, B_SNOW, B_PERMAFROST,
    // --- ores
    B_ORE_COPPER, B_ORE_TIN, B_ORE_LEAD, B_ORE_ZINC, B_ORE_IRON, B_ORE_MAGNETITE,
    B_ORE_GOLD, B_ORE_SILVER, B_ORE_COAL, B_ORE_LIGNITE, B_ORE_QUARTZ,
    // --- wood: logs, planks, leaves for 6 species
    B_LOG_PINE, B_LOG_OAK, B_LOG_BIRCH, B_LOG_MAPLE, B_LOG_ACACIA, B_LOG_KAPOK,
    B_PLANK_PINE, B_PLANK_OAK, B_PLANK_BIRCH, B_PLANK_MAPLE, B_PLANK_ACACIA, B_PLANK_KAPOK,
    B_LEAF_PINE, B_LEAF_OAK, B_LEAF_BIRCH, B_LEAF_MAPLE, B_LEAF_ACACIA, B_LEAF_KAPOK,
    // --- plants (cross shaped)
    B_TALLGRASS, B_TALLGRASS_DRY, B_FERN, B_FLOWER_RED, B_FLOWER_YELLOW, B_FLOWER_BLUE,
    B_FLOWER_PURPLE, B_HEATHER, B_CATTAIL, B_REED, B_MUSHROOM_RED, B_MUSHROOM_BROWN,
    B_BERRYBUSH, B_DEADBUSH, B_SAPLING, B_CACTUS,
    B_CROP_0, B_CROP_1, B_CROP_2, B_CROP_3, B_CROP_4, B_CROP_5,
    // --- constructed / misc
    B_COBBLE_GREY, B_COBBLE_WARM, B_COBBLE_DARK, B_BRICKS, B_STONE_BRICK, B_POLISHED,
    B_TORCH, B_FIRE, B_GLASS, B_THATCH, B_CHARCOAL, B_ICE, B_WATER, B_LAVA, B_CRATE,
    B_FIREPIT, B_SLAG, B_BLOOM, B_SCAFFOLD,
    B_COUNT
};

enum BlockFlags : u16 {
    BF_SOLID       = 0x0001,   // has collision
    BF_OPAQUE      = 0x0002,   // hides neighbouring faces and blocks light
    BF_LIQUID      = 0x0004,
    BF_CROSS       = 0x0008,   // rendered as two crossed quads
    BF_LEAF        = 0x0010,   // partially see-through, attenuates light
    BF_REPLACEABLE = 0x0020,   // worldgen / placement can overwrite
    BF_TRANSLUCENT = 0x0040,   // alpha blended (water, ice, glass)
    BF_PLANT       = 0x0080,   // needs solid ground, breaks when unsupported
    BF_NOCOLLIDE   = 0x0100,   // no collision despite being visible
};

enum ToolKind : u8 { TOOL_NONE = 0, TOOL_AXE, TOOL_PICK, TOOL_SHOVEL, TOOL_KNIFE, TOOL_HAMMER };

enum TintKind : u8 { TINT_NONE = 0, TINT_GRASS, TINT_FOLIAGE, TINT_LEAF_PINE };

struct BlockDef {
    const char* name;
    u16 texSide, texTop, texBottom;
    u16 flags;
    u8  hardness;    // 0..100, how long it takes to break
    u8  tool;
    u8  light;       // emitted light level 0..15
    u8  tint;
    u16 drop;        // item/block id dropped, 0 => itself
    u8  minTier;     // 0 hand, 1 copper, 2 bronze, 3 iron
    float top;       // terrain "top" offset for cross plants (0 = full block)
    u32 avgColor;    // filled at init from the atlas
};

extern BlockDef g_blocks[B_COUNT];

void initBlocks();

inline BlockDef& bdef(u16 id) { return g_blocks[id]; }
inline u16 blockId(u16 v) { return v & 0x0FFF; }
inline u16 blockMeta(u16 v) { return v >> 12; }
inline u16 makeBlock(u16 id, u16 meta = 0) { return (u16)((id & 0x0FFF) | ((meta & 0xF) << 12)); }

inline bool bIsOpaque(u16 v) { return (g_blocks[blockId(v)].flags & BF_OPAQUE) != 0; }
inline bool bIsSolid(u16 v)  { return (g_blocks[blockId(v)].flags & BF_SOLID) != 0; }
inline bool bIsAir(u16 v)    { return blockId(v) == B_AIR; }
inline bool bIsPlant(u16 v)  { return (g_blocks[blockId(v)].flags & BF_PLANT) != 0; }
inline bool bIsCross(u16 v)  { return (g_blocks[blockId(v)].flags & BF_CROSS) != 0; }
inline bool bIsLiquid(u16 v) { return (g_blocks[blockId(v)].flags & BF_LIQUID) != 0; }
inline bool bIsReplaceable(u16 v) { u16 id = blockId(v); return id == B_AIR || (g_blocks[id].flags & BF_REPLACEABLE) != 0; }

// items that are not blocks live above the block namespace
enum ItemId : u16 {
    I_NONE = 0x3000,
    I_STICK = 0x3001, I_FLINT = 0x3002, I_KNIFE_FLINT = 0x3100, I_AXE_FLINT = 0x3101,
    I_SHOVEL_FLINT = 0x3102, I_PICK_FLINT = 0x3103, I_INGOT_COPPER = 0x3200,
    I_INGOT_BRONZE = 0x3201, I_INGOT_IRON = 0x3202, I_INGOT_TIN = 0x3203,
    I_NUGGET_COPPER = 0x3210, I_NUGGET_GOLD = 0x3211, I_NUGGET_IRON = 0x3212,
    I_AXE_COPPER = 0x3300, I_PICK_COPPER = 0x3301, I_SHOVEL_COPPER = 0x3302, I_KNIFE_COPPER = 0x3303,
    I_HAMMER_COPPER = 0x3304, I_CHISEL_COPPER = 0x3305, I_PROSPICK_COPPER = 0x3306, I_SWORD_COPPER = 0x3307,
    I_AXE_BRONZE = 0x3310, I_PICK_BRONZE = 0x3311, I_SHOVEL_BRONZE = 0x3312, I_KNIFE_BRONZE = 0x3313,
    I_HAMMER_BRONZE = 0x3314, I_PROSPICK_BRONZE = 0x3315, I_SWORD_BRONZE = 0x3316,
    I_AXE_IRON = 0x3320, I_PICK_IRON = 0x3321, I_SHOVEL_IRON = 0x3322, I_KNIFE_IRON = 0x3323,
    I_HAMMER_IRON = 0x3324, I_PROSPICK_IRON = 0x3325, I_SWORD_IRON = 0x3326,
    I_SEEDS = 0x3400, I_BERRIES = 0x3401, I_MEAT_RAW = 0x3402, I_MEAT_COOKED = 0x3403,
    I_CLAYBALL = 0x3404, I_BOWL = 0x3405, I_POT = 0x3406, I_ROPE = 0x3407, I_CROP = 0x3408,
    I_BREAD = 0x3409, I_STEW = 0x340A, I_TALLOW = 0x340B, I_COAL = 0x340C, I_GEAR = 0x340D,
    I_ARROWHEAD = 0x340E,
};

struct ItemDef {
    const char* name;
    int tex;
    u16 tool;        // ToolKind
    u8  tier;
    u16 damage;
    u16 durability;
    u8  stack;
    u8  food;        // satiety restored
    u8  placeBlock;  // 0 = none (block id to place)
    bool isBlock;
};
ItemDef* itemDef(u16 id);          // resolves blocks to a generated def
int itemTexture(u16 id);           // atlas tile for the item (block side texture or icon)
bool itemIsBlock(u16 id);
u16  blockToItem(u16 blockId);
void initItems();
int textureAvgColor(int tex);
