// vb_ui.cpp : HUD, inventory, crafting and menus (immediate mode quads + 5x7 font)
#include "vb_render.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#define ACT_START     100
#define ACT_SIZE      101
#define ACT_MODE      102
#define ACT_SEED      103
#define ACT_CONTINUE  104
#define ACT_RESUME    110
#define ACT_SAVEQUIT  111
#define ACT_SLOT      120
#define ACT_CRAFT     130
#define ACT_SCROLLUP  140
#define ACT_SCROLLDN  141
#define ACT_CLOSE     150
#define ACT_QUITGAME  160

static int g_scrW = 1280, g_scrH = 720;
static GLuint g_atlasTex = 0, g_fontTex = 0;
static GLuint g_mapTex = 0;
static u64 g_mapSeed = 0;
static bool g_hasSave = false;
static int g_saveTick = 30;
static int g_mapSize = 0;

// ------------------------------------------------------------------ primitives
static void setOrtho() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, g_scrW, g_scrH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void preUI() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    setOrtho();
}

static void postUI() {
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);
    glColor4f(1, 1, 1, 1);
}

static void quad(float x, float y, float w, float h, u32 c, u8 a = 255) {
    glDisable(GL_TEXTURE_2D);
    glColor4ub((u8)(c >> 16), (u8)(c >> 8), (u8)c, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

static void outline(float x, float y, float w, float h, u32 c, u8 a = 255) {
    quad(x, y, w, 2, c, a);
    quad(x, y + h - 2, w, 2, c, a);
    quad(x, y, 2, h, c, a);
    quad(x + w - 2, y, 2, h, c, a);
}

// tile a block texture across an arbitrary rectangle (tight uvs, no bleeding)
static void texTiled(float x, float y, float wPx, float hPx, int tile, u32 tint, u8 a, float cell) {
    float u0, v0, u1, v1;
    tileUV(tile, u0, v0, u1, v1);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_atlasTex);
    glColor4ub((u8)(tint >> 16), (u8)(tint >> 8), (u8)tint, a);
    glBegin(GL_QUADS);
    for (float yy = 0; yy < hPx - 0.01f; yy += cell) {
        float ch = fminf(cell, hPx - yy);
        float fv = ch / cell;
        for (float xx = 0; xx < wPx - 0.01f; xx += cell) {
            float cw = fminf(cell, wPx - xx);
            float fu = cw / cell;
            float ax = x + xx, ay = y + yy;
            float bu = u0 + (u1 - u0) * fu, bv = v0 + (v1 - v0) * fv;
            glTexCoord2f(u0, v0); glVertex2f(ax, ay);
            glTexCoord2f(bu, v0); glVertex2f(ax + cw, ay);
            glTexCoord2f(bu, bv); glVertex2f(ax + cw, ay + ch);
            glTexCoord2f(u0, bv); glVertex2f(ax, ay + ch);
        }
    }
    glEnd();
}

// a wooden/stone panel with a bevel : the container for every window
static void panel(float x, float y, float w, float h, int tile, u32 tint, float cell = 26.f) {
    quad(x - 3, y - 3, w + 6, h + 6, 0x0d0a07, 210);
    texTiled(x, y, w, h, tile, tint, 255, cell);
    quad(x, y, w, 2, 0xf0e0c0, 45);
    quad(x, y, 2, h, 0xf0e0c0, 35);
    quad(x, y + h - 2, w, 2, 0x000000, 90);
    quad(x + w - 2, y, 2, h, 0x000000, 80);
    outline(x - 3, y - 3, w + 6, h + 6, 0x0d0a07, 235);
    outline(x, y, w, h, 0x6a5a42, 150);
}

// a button : same idea, brighter when selected
static void button(float x, float y, float w, float h, int tile, bool hot) {
    u32 tint = hot ? 0x8a6a3a : 0x5a4630;
    quad(x - 2, y - 2, w + 4, h + 4, 0x0d0a07, 200);
    texTiled(x, y, w, h, tile, tint, 255, 22.f);
    quad(x, y, w, 2, 0xf0e0c0, hot ? 90 : 45);
    quad(x, y + h - 2, w, 2, 0x000000, 90);
    outline(x, y, w, h, hot ? 0xffe0a0 : 0x2a2018, hot ? 220 : 190);
}

static void texQuad(float x, float y, float w, float h, int tile, u32 tint, u8 alpha = 255) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_atlasTex);
    float u0, v0, u1, v1;
    tileUV(tile, u0, v0, u1, v1);
    glColor4ub((u8)(tint >> 16), (u8)(tint >> 8), (u8)tint, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x, y);
    glTexCoord2f(u1, v0); glVertex2f(x + w, y);
    glTexCoord2f(u1, v1); glVertex2f(x + w, y + h);
    glTexCoord2f(u0, v1); glVertex2f(x, y + h);
    glEnd();
}

static void text(const char* s, float x, float y, float sc, u32 c, u8 a = 255) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_fontTex);
    glColor4ub((u8)(c >> 16), (u8)(c >> 8), (u8)c, a);
    float cx = x;
    for (const char* p = s; *p; p++) {
        if (*p == ' ') { cx += 4 * sc; continue; }
        int idx = fontIndex(*p);
        if (idx < 0) { cx += 4 * sc; continue; }
        float u0 = (idx % 8) / 8.f, v0 = (idx / 8) / 8.f;
        float u1 = u0 + 5.f / 64.f, v1 = v0 + 7.f / 64.f;
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex2f(cx, y);
        glTexCoord2f(u1, v0); glVertex2f(cx + 5 * sc, y);
        glTexCoord2f(u1, v1); glVertex2f(cx + 5 * sc, y + 7 * sc);
        glTexCoord2f(u0, v1); glVertex2f(cx, y + 7 * sc);
        glEnd();
        cx += 6 * sc;
    }
}

static float textW(const char* s, float sc) {
    float w = 0;
    for (const char* p = s; *p; p++) w += (*p == ' ') ? 4 * sc : 6 * sc;
    return w;
}

static void textCentered(const char* s, float cx, float y, float sc, u32 c, u8 a = 255) {
    text(s, cx - textW(s, sc) * 0.5f, y, sc, c, a);
}

// ------------------------------------------------------------------ item icons
static u32 itemTint(u16 id) {
    if (id == I_INGOT_COPPER || id == I_NUGGET_COPPER) return rgb(214, 128, 78);
    if (id == I_INGOT_TIN) return rgb(178, 190, 198);
    if (id == I_INGOT_BRONZE) return rgb(198, 158, 86);
    if (id == I_INGOT_IRON || id == I_NUGGET_IRON) return rgb(206, 210, 218);
    if (id == I_NUGGET_GOLD) return rgb(238, 196, 82);
    return rgb(255, 255, 255);
}

static void drawItemIcon(u16 item, float x, float y, float size) {
    if (!item) return;
    int tile = itemTexture(item);
    texQuad(x, y, size, size, tile, itemTint(item));
}

static void drawSlot(float x, float y, float size, Slot& s, bool selected) {
    quad(x - 2, y - 2, size + 4, size + 4, 0x0d0a07, 200);
    texTiled(x, y, size, size, T_PLANK_OAK, selected ? 0xd8b070 : 0x7a6446, 255, size * 0.5f);
    quad(x, y, size, 2, 0xffffff, selected ? 70 : 25);
    quad(x, y + size - 2, size, 2, 0x000000, 80);
    outline(x, y, size, size, selected ? 0xffe8b0 : 0x2a2018, 230);
    if (s.item) {
        drawItemIcon(s.item, x + size * 0.12f, y + size * 0.12f, size * 0.76f);
        if (s.count > 1) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", (int)s.count);
            text(buf, x + size - textW(buf, 1.4f) - 3, y + size - 11, 1.4f, 0xffffff);
        }
        if (s.dur > 0) {
            float f = (float)s.dur / (float)imax(1, itemDef(s.item) ? itemDef(s.item)->durability : 1);
            quad(x + 3, y + size - 5, size - 6, 3, 0x201810, 200);
            quad(x + 3, y + size - 5, (size - 6) * saturate(f), 3,
                 f > 0.4f ? 0x6ab04a : 0xc04a3a, 230);
        }
    }
}

// ------------------------------------------------------------------ minimap
static void buildMapTexture(Game& g) {
    const CoarseMap& m = g.world.coarse;
    if (m.cells == 0) return;
    std::vector<u32> img((size_t)m.cells * m.cells);
    for (int z = 0; z < m.cells; z++)
        for (int x = 0; x < m.cells; x++) {
            u32 c = m.color[m.at(x, z)];
            int h = m.getHeight(x, z);
            int hx = m.getHeight(imin(x + 1, m.cells - 1), z);
            int hz = m.getHeight(x, imin(z + 1, m.cells - 1));
            float sh = 1.f + (h - hx) * 0.05f + (h - hz) * 0.035f;
            if (h <= SEA_LEVEL) c = 0x2f6a8e;
            img[(size_t)z * m.cells + x] = shadeColor(c, clampf(sh, 0.6f, 1.45f));
        }
    if (!g_mapTex) glGenTextures(1, &g_mapTex);
    glBindTexture(GL_TEXTURE_2D, g_mapTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m.cells, m.cells, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
    g_mapSeed = g.world.seed;
    g_mapSize = g.world.size;
}

// ------------------------------------------------------------------ HUD
static void drawBar(float x, float y, float w, float h, float frac, u32 c, const char* label) {
    quad(x - 2, y - 2, w + 4, h + 4, 0x14100c, 200);
    quad(x, y, w, h, 0x2a2420, 220);
    quad(x, y, w * saturate(frac), h, c, 240);
    if (label) {
        text(label, x - 58, y + 1, 1.0f, 0xf0e6d0, 225);
    }
}

static void drawHUD(Game& g, float mx, float my) {
    float cx = g_scrW * 0.5f;
    // crosshair
    quad(cx - 7, g_scrH * 0.5f - 1, 14, 2, 0xf0f0f0, 170);
    quad(cx - 1, g_scrH * 0.5f - 7, 2, 14, 0xf0f0f0, 170);

    // hotbar
    float slot = 40, gap = 3;
    float totalW = 10 * slot + 9 * gap;
    float bx = cx - totalW * 0.5f;
    float by = g_scrH - slot - 14;
    quad(bx - 6, by - 6, totalW + 12, slot + 12, 0x1a1510, 170);
    for (int i = 0; i < 10; i++)
        drawSlot(bx + i * (slot + gap), by, slot, g.inv[i], i == g.sel);

    // stats
    float sx = 100, sy = g_scrH - 118;
    quad(sx - 8, sy - 10, 190, 108, 0x1a1510, 150);
    panel(sx - 70, sy - 8, 254, 86, T_COBBLE_DARK, 0x4a453c, 20.f);
    drawBar(sx, sy, 170, 10, g.health / g.maxHealth, 0xc0392b, "HP");
    drawBar(sx, sy + 20, 170, 10, g.satiety / g.maxSatiety, 0xd08b3a, "SATIETY");
    float tempFrac = saturate((g.temperature + 15.f) / 60.f);
    drawBar(sx, sy + 40, 170, 10, tempFrac, g.temperature < 5 ? 0x4a90d0 : (g.temperature > 30 ? 0xd05a2a : 0x60a060), "TEMP");
    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%dC", (int)g.temperature);
    text(tbuf, sx + 142, sy + 41, 1.0f, 0xf0e8d8, 235);
    drawBar(sx, sy + 60, 170, 10, g.stability / 100.f, 0x8a5ac0, "STABILITY");

    // clock / biome
    float dayFrac = g.dayTime;
    int hour = (int)((dayFrac * 24.f + 6.f));
    hour %= 24;
    int minute = (int)((dayFrac * 24.f * 60.f)) % 60;
    char tbuf2[64];
    const char* seasonNames[4] = { "SPRING", "SUMMER", "AUTUMN", "WINTER" };
    snprintf(tbuf2, sizeof(tbuf2), "%s  D%d  %02d:%02d  %s", seasonNames[clampi(g.season, 0, 3)], g.dayCount + 1,
             hour, minute, g.weather == 1 ? "RAIN" : (g.weather == 2 ? "SNOW" : "CLEAR"));
    quad(cx - 90, 12, 180, 22, 0x1a1510, 150);
    textCentered(tbuf2, cx, 17, 1.6f, 0xf0e6d0);
    snprintf(tbuf2, sizeof(tbuf2), "%s", g.biomeNameAt((int)g.pos.x, (int)g.pos.z));
    textCentered(tbuf2, cx, 38, 1.4f, 0xd8ccb0, 220);

    // minimap
    float ms = 148;
    float mX = g_scrW - ms - 18, mY = 18;
    if (g_mapSeed != g.world.seed || g_mapSize != g.world.size) buildMapTexture(g);
    quad(mX - 4, mY - 4, ms + 8, ms + 8, 0x1a1510, 200);
    outline(mX - 4, mY - 4, ms + 8, ms + 8, 0xc8b083, 200);
    glBindTexture(GL_TEXTURE_2D, g_mapTex);
    glColor4ub(255, 255, 255, 255);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(mX, mY);
    glTexCoord2f(1, 0); glVertex2f(mX + ms, mY);
    glTexCoord2f(1, 1); glVertex2f(mX + ms, mY + ms);
    glTexCoord2f(0, 1); glVertex2f(mX, mY + ms);
    glEnd();
    // player marker
    float px = mX + (g.pos.x / (float)g.world.size) * ms;
    float pz = mY + (g.pos.z / (float)g.world.size) * ms;
    float fx = cosf(g.yaw), fz = sinf(g.yaw);
    quad(px - 2, pz - 2, 5, 5, 0xffe070, 255);
    glDisable(GL_TEXTURE_2D);
    glColor4ub(255, 224, 112, 210);
    glBegin(GL_TRIANGLES);
    glVertex2f(px + fx * 12, pz + fz * 12);
    glVertex2f(px - fz * 6 - fx * 2, pz + fx * 6 - fz * 2);
    glVertex2f(px + fz * 6 - fx * 2, pz - fx * 6 - fz * 2);
    glEnd();
    glEnable(GL_TEXTURE_2D);

    // toast
    if (g.toastTime > 0 && !g.toast.empty()) {
        float a = saturate(g.toastTime * 2.f);
        float w = textW(g.toast.c_str(), 1.6f) + 24;
        quad(cx - w * 0.5f, g_scrH - 190, w, 26, 0x1a1510, (u8)(160 * a));
        textCentered(g.toast.c_str(), cx, g_scrH - 183, 1.6f, 0xf0e6d0, (u8)(255 * a));
    }
    if (g.prospectTime > 0 && !g.prospectText.empty()) {
        float a = saturate(g.prospectTime);
        float w = textW(g.prospectText.c_str(), 1.5f) + 22;
        quad(cx - w * 0.5f, g_scrH - 232, w, 24, 0x101820, (u8)(170 * a));
        textCentered(g.prospectText.c_str(), cx, g_scrH - 226, 1.5f, 0x9ad0f0, (u8)(255 * a));
    }

    // damage flash
    if (g.damageFlash > 0.01f) {
        float a = fminf(1.f, g.damageFlash) * 0.55f;
        quad(0, 0, g_scrW, g_scrH, 0x8a1410, (u8)(a * 255));
    }

    // world still streaming in
    {
        int pcx = floori(g.pos.x) / CH, pcz = floori(g.pos.z) / CH;
        int have = 0, need = 0;
        for (int dz = -3; dz <= 3; dz++)
            for (int dx = -3; dx <= 3; dx++) {
                int ccx = pcx + dx, ccz = pcz + dz;
                if (ccx < 0 || ccz < 0 || ccx >= g.world.chunksX || ccz >= g.world.chunksX) continue;
                need++;
                if (g.world.chunkAt(ccx, ccz)) have++;
            }
        if (need > 0 && have < need) {
            float w = 260, p = have / (float)need;
            float by = g_scrH - 78;
            quad(cx - w * 0.5f, by, w, 10, 0x14100c, 190);
            quad(cx - w * 0.5f, by, w * p, 10, 0x9ec86a, 220);
            textCentered("GENERATING WORLD", cx, by - 20, 1.5f, 0xe8e0c8, 220);
        }
    }

    // held item (bottom right, simple hand view)
    if (g.inv[g.sel].item) {
        float s = 76;
        float swing = g.swing * 26.f;
        float hx = g_scrW - s - 34 + swing, hy = g_scrH - s - 60 + swing * 0.5f;
        quad(hx - 6, hy - 6, s + 12, s + 12, 0x14100c, 110);
        texQuad(hx, hy, s, s, itemTexture(g.inv[g.sel].item), itemTint(g.inv[g.sel].item), 240);
        const ItemDef* id = itemDef(g.inv[g.sel].item);
        if (id) textCentered(id->name, hx + s * 0.5f, hy - 20, 1.1f, 0xe8dcc0, 210);
    }

    // debug
    if (g.showDebug) {
        static int frames = 0;
        char buf[256];
        snprintf(buf, sizeof(buf), "FPS %.0f", g.fps);
        text(buf, 18, 18, 1.5f, 0xa0ffa0);
        snprintf(buf, sizeof(buf), "XYZ %.1f %.1f %.1f", g.pos.x, g.pos.y, g.pos.z);
        text(buf, 18, 36, 1.5f, 0xd0e0d0);
        snprintf(buf, sizeof(buf), "CHUNK %d %d  LOD0..3", (int)g.pos.x / CH, (int)g.pos.z / CH);
        text(buf, 18, 54, 1.5f, 0xd0e0d0);
        snprintf(buf, sizeof(buf), "WORLD %dX%d  SEED %llu", g.world.size, g.world.size, (unsigned long long)g.world.seed);
        text(buf, 18, 72, 1.5f, 0xd0e0d0);
        snprintf(buf, sizeof(buf), "ENTS %d  PARTS %d", (int)g.entities.size(), (int)g.particles.size());
        text(buf, 18, 90, 1.5f, 0xd0e0d0);
        snprintf(buf, sizeof(buf), "MODE %s", g.mode == GM_CREATIVE ? "CREATIVE" : "SURVIVAL");
        text(buf, 18, 108, 1.5f, 0xd0e0d0);
        snprintf(buf, sizeof(buf), "BREAK %.0f%%", g.breakProgress * 100.f);
        (void)frames;
        text(buf, 18, 126, 1.5f, 0xd0e0d0);
    }
    (void)mx; (void)my;
}

// ------------------------------------------------------------------ inventory
static void drawInventory(Game& g) {
    float cx = g_scrW * 0.5f;
    float slot = 46, gap = 4;
    float gridW = 10 * slot + 9 * gap;
    float gx = cx - gridW * 0.5f;
    float gy = g_scrH * 0.5f - 130;
    panel(cx - gridW * 0.5f - 22, gy - 46, gridW + 44, 320, T_PLANK_KAPOK, 0x6e5a42, 30.f);
    textCentered("INVENTORY", cx, gy - 36, 2.0f, 0xf0e6d0);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 10; c++) {
            int i = r * 10 + c;
            drawSlot(gx + c * (slot + gap), gy + r * (slot + gap), slot, g.inv[i], false);
        }
    textCentered("CLICK AN ITEM TO SWAP IT WITH THE SELECTED HOTBAR SLOT", cx, gy + 160, 1.2f, 0xb8ac94);
    textCentered("E / ESC TO CLOSE", cx, gy + 180, 1.2f, 0xb8ac94);
    // hotbar row
    float hy = gy + 202;
    for (int i = 0; i < 10; i++)
        drawSlot(gx + i * (slot + gap), hy, slot, g.inv[i], i == g.sel);
}

// ------------------------------------------------------------------ crafting
static void drawCrafting(Game& g, std::vector<int>& avail) {
    float cx = g_scrW * 0.5f;
    float w = 560, h = 420;
    float x = cx - w * 0.5f, y = g_scrH * 0.5f - h * 0.5f;
    quad(x, y, w, h, 0x241c14, 240);
    outline(x, y, w, h, 0xc8b083, 240);
    textCentered("CRAFTING", cx, y + 12, 2.0f, 0xf0e6d0);
    textCentered("FIREPIT REQUIRED FOR SMELTING AND COOKING", cx, y + 34, 1.2f, 0xb8ac94);
    int rows = 9;
    int first = clampi(g.craftScroll, 0, imax(0, (int)avail.size() - rows));
    for (int r = 0; r < rows; r++) {
        int idx = first + r;
        if (idx >= (int)avail.size()) break;
        const Recipe& rec = g_recipes[avail[idx]];
        float ry = y + 58 + r * 38.f;
        bool hov = false;
        texTiled(x + 14, ry, w - 28, 34, T_PLANK_OAK, hov ? 0x9a7c54 : 0x7d6548, 240, 34.f);
        quad(x + 14, ry, w - 28, 2, 0xffffff, 26);
        quad(x + 14, ry + 32, w - 28, 2, 0x000000, 60);
        drawItemIcon(rec.out, x + 20, ry + 3, 28);
        char buf[128];
        snprintf(buf, sizeof(buf), "%s X%d", rec.name, (int)rec.outCount);
        text(buf, x + 56, ry + 6, 1.5f, 0xf0e6d0);
        char ing[128] = "";
        ItemDef* da = itemDef(rec.a);
        if (rec.a) snprintf(ing + strlen(ing), sizeof(ing) - strlen(ing), "%dx%s ", (int)rec.an, da ? da->name : "?");
        if (rec.b) { ItemDef* db = itemDef(rec.b); snprintf(ing + strlen(ing), sizeof(ing) - strlen(ing), "%dx%s ", (int)rec.bn, db ? db->name : "?"); }
        if (rec.c) { ItemDef* dc = itemDef(rec.c); snprintf(ing + strlen(ing), sizeof(ing) - strlen(ing), "%dx%s", (int)rec.cn, dc ? dc->name : "?"); }
        text(ing, x + 56, ry + 20, 1.0f, 0xa89c84);
        button(x + w - 96, ry + 4, 78, 26, T_STONE_BRICK, false);
        text("CRAFT", x + w - 77, ry + 9, 1.3f, 0xd8f0b0);
    }
    if (avail.empty()) textCentered("NO RECIPES AVAILABLE - GATHER MATERIALS", cx, y + 200, 1.5f, 0xa89c84);
    textCentered("SCROLL TO SEE MORE   -   C / ESC TO CLOSE", cx, y + h - 24, 1.2f, 0xb8ac94);
}

// ------------------------------------------------------------------ menus
static void drawMenu(Game& g, float mx, float my) {
    (void)mx; (void)my;
    // background : tiled stone with a warm wash and a dark crown
    texTiled(0, 0, g_scrW, g_scrH, T_COBBLE_DARK, 0x5a6a4e, 255, 64.f);
    for (int i = 0; i < 64; i++) {
        float t = i / 63.f;
        // dark at the top and bottom, open in the middle where the controls sit
        float k = 1.f - fabsf(t - 0.55f) * 1.35f;
        u32 c = lerpColor(rgb(6, 10, 7), rgb(40, 56, 30), saturate(k));
        quad(0, g_scrH * t, g_scrW, g_scrH / 63.f + 1, c, (u8)(70 + 120 * saturate(1.f - t * 0.85f)));
    }
    float cx = g_scrW * 0.5f;
    textCentered("VINTAGEBLOCK", cx, g_scrH * 0.16f, 5.0f, 0xf0e0b8);
    textCentered("A VINTAGE STORY INSPIRED VOXEL SANDBOX", cx, g_scrH * 0.16f + 52, 1.8f, 0xc8b894);

    float bw = 300, bh = 40;
    float bx = cx - bw * 0.5f;
    float y = g_scrH * 0.42f;
    // size selector
    textCentered("WORLD SIZE", cx, y - 19, 1.8f, 0xe8dcc0);
    const char* sizeNames[3] = { "256 X 256", "512 X 512", "1024 X 1024" };
    for (int i = 0; i < 3; i++) {
        float bxi = cx - 240 + i * 165;
        bool selSize = g.uiSize == i;
        button(bxi, y, 155, bh, T_STONE_BRICK, selSize);
        textCentered(sizeNames[i], bxi + 77, y + 13, 1.7f, selSize ? 0xfff4d8 : 0xcfc2a4);
    }
    y += bh + 22;
    textCentered("GAME MODE", cx, y - 19, 1.8f, 0xe8dcc0);
    const char* modeNames[2] = { "SURVIVAL", "CREATIVE" };
    for (int i = 0; i < 2; i++) {
        float bxi = cx - 160 + i * 165;
        bool selM = g.uiMode == i;
        button(bxi, y, 155, bh, T_STONE_BRICK, selM);
        textCentered(modeNames[i], bxi + 77, y + 13, 1.7f, selM ? 0xfff4d8 : 0xcfc2a4);
    }
    y += bh + 22;
    textCentered("WORLD SEED", cx, y - 19, 1.8f, 0xe8dcc0);
    button(bx, y, bw, bh, T_PLANK_OAK, g.uiSeedEditing);
    textCentered(g.uiSeed, cx, y + 13, 1.9f, 0xf0e6d0);
    y += bh + 34;
    if (g_hasSave) {
        quad(bx - 2, y - 2, bw + 4, 56, 0x0d0a07, 200);
        texTiled(bx, y, bw, 52, T_PLANK_OAK, 0x5e7a3a, 255, 26.f);
        quad(bx, y, bw, 2, 0xd8ffa8, 70);
        outline(bx, y, bw, 52, 0xb8e080, 235);
        textCentered("CONTINUE WORLD", cx, y + 16, 2.4f, 0xf4ffe0);
        y += 62;
    }
    quad(bx - 2, y - 2, bw + 4, 56, 0x0d0a07, 200);
    texTiled(bx, y, bw, 52, T_PLANK_OAK, 0x9a6a2a, 255, 26.f);
    quad(bx, y, bw, 2, 0xffe0a0, 90);
    outline(bx, y, bw, 52, 0xffc860, 235);
    textCentered("CREATE WORLD", cx, y + 16, 2.4f, 0xfff4d8);

    textCentered("WASD MOVE   SHIFT RUN   SPACE JUMP   MOUSE LOOK", cx, g_scrH - 96, 1.4f, 0xc0b498);
    textCentered("LMB MINE / ATTACK   RMB USE / PLACE   E INVENTORY   C CRAFTING", cx, g_scrH - 76, 1.4f, 0xc0b498);
    textCentered("1-0 HOTBAR   F FLY (CREATIVE)   F3 DEBUG   ESC PAUSE", cx, g_scrH - 56, 1.4f, 0xc0b498);
    char vbuf[64];
    snprintf(vbuf, sizeof(vbuf), "VERSION %s %s   -   WORLDS ARE FINITE BY DESIGN", VB_VERSION, VB_VERSION_TAG);
    textCentered(vbuf, cx, g_scrH - 30, 1.2f, 0x8a9478, 200);
}

static void drawPause(Game& g) {
    quad(0, 0, g_scrW, g_scrH, 0x060604, 175);
    float cx = g_scrW * 0.5f, cy = g_scrH * 0.5f;
    panel(cx - 210, cy - 160, 420, 300, T_PLANK_OAK, 0x6e5a42, 30.f);
    textCentered("PAUSED", cx, cy - 130, 4.0f, 0xf0e0b8);
    float bw = 300, bh = 44;
    button(cx - bw * 0.5f, cy - 40, bw, bh, T_STONE_BRICK, false);
    textCentered("CONTINUE", cx, cy - 27, 1.9f, 0xfff4d8);
    button(cx - bw * 0.5f, cy + 20, bw, bh, T_STONE_BRICK, false);
    textCentered("SAVE AND QUIT", cx, cy + 33, 1.9f, 0xfff4d8);
    textCentered("ESC TO RESUME", cx, cy + 96, 1.3f, 0xa89c80);
}

// ------------------------------------------------------------------ entry
void renderUI(Renderer* r, Game& g, float mx, float my, int W, int H) {
    if (g.screen == 0 && ++g_saveTick > 30) { g_saveTick = 0; g_hasSave = saveExists(); }
    g_scrW = W; g_scrH = H;
    g_atlasTex = renderAtlas(r);
    g_fontTex = renderFontTex(r);
    preUI();
    if (g.screen == 0) {
        drawMenu(g, mx, my);
    } else {
        drawHUD(g, mx, my);
        if (g.invOpen) drawInventory(g);
        if (g.craftOpen) {
            std::vector<int> avail;
            g.recipesAvailable(avail);
            drawCrafting(g, avail);
        }
        if (g.paused) drawPause(g);
    }
    postUI();
}

int uiHitTest(Game& g, int W, int H, float mx, float my) {
    g_scrW = W; g_scrH = H;
    float cx = W * 0.5f;
    auto inRect = [&](float x, float y, float w, float h) {
        return mx >= x && my >= y && mx < x + w && my < y + h;
    };
    if (g.screen == 0) {
        float bw = 300, bh = 40;
        float bx = cx - bw * 0.5f;
        float y = H * 0.42f;
        for (int i = 0; i < 3; i++) {
            float bxi = cx - 240 + i * 165;
            if (inRect(bxi, y, 155, bh)) return ACT_SIZE * 1000 + i;
        }
        y += bh + 22;
        for (int i = 0; i < 2; i++) {
            float bxi = cx - 160 + i * 165;
            if (inRect(bxi, y, 155, bh)) return ACT_MODE * 1000 + i;
        }
        y += bh + 22;
        if (inRect(bx, y, bw, bh)) return ACT_SEED * 1000;
        y += bh + 34;
        if (g_hasSave) {
            if (inRect(bx, y, bw, 52)) return ACT_CONTINUE * 1000;
            y += 62;
        }
        if (inRect(bx, y, bw, 52)) return ACT_START * 1000;
        return -1;
    }
    if (g.paused) {
        float bh = 44;
        float cy = H * 0.5f;
        if (inRect(cx - 150, cy - 40, 300, bh)) return ACT_RESUME * 1000;
        if (inRect(cx - 150, cy + 20, 300, bh)) return ACT_SAVEQUIT * 1000;
        return -1;
    }
    if (g.invOpen) {
        float slot = 46, gap = 4;
        float gridW = 10 * slot + 9 * gap;
        float gx = cx - gridW * 0.5f;
        float gy = H * 0.5f - 130;
        for (int rr = 0; rr < 3; rr++)
            for (int c = 0; c < 10; c++) {
                int i = rr * 10 + c;
                if (inRect(gx + c * (slot + gap), gy + rr * (slot + gap), slot, slot))
                    return ACT_SLOT * 1000 + i;
            }
        float hy = gy + 202;
        for (int i = 0; i < 10; i++)
            if (inRect(gx + i * (slot + gap), hy, slot, slot)) return ACT_SLOT * 1000 + (20 + i);
        return -1;
    }
    if (g.craftOpen) {
        std::vector<int> avail;
        g.recipesAvailable(avail);
        float w = 560, h = 420;
        float x = cx - w * 0.5f, y = H * 0.5f - h * 0.5f;
        int first = clampi(g.craftScroll, 0, imax(0, (int)avail.size() - 9));
        for (int rr = 0; rr < 9; rr++) {
            int idx = first + rr;
            if (idx >= (int)avail.size()) break;
            float ry = y + 58 + rr * 38.f;
            if (inRect(x + 14, ry, w - 28, 34)) return ACT_CRAFT * 1000 + avail[idx];
        }
        return ACT_CLOSE * 1000;
    }
    // hotbar clicks
    float slot = 40, gap = 3;
    float totalW = 10 * slot + 9 * gap;
    float bx = cx - totalW * 0.5f;
    float by = H - slot - 14;
    for (int i = 0; i < 10; i++)
        if (inRect(bx + i * (slot + gap), by, slot, slot)) return ACT_SLOT * 1000 + i;
    return -1;
}
