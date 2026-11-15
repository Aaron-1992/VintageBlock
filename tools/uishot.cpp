// tools/uishot.cpp : rasterises the immediate-mode UI on the CPU through a fake
// GL, so the HUD / inventory / menu can be inspected without a GPU.
#include "shim/GL/gl.h"
#include "../src/vb_render.h"
#include "../src/vb_png.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

// ------------------------------------------------------------------ software GL
struct Tex { int w = 0, h = 0; std::vector<u32> px; };
static std::map<GLuint, Tex> g_tex;
static GLuint g_bound = 0;
static GLuint g_nextId = 100;

static int g_fbW = 1280, g_fbH = 720;
static std::vector<u32> g_fb;
static std::vector<int> g_stamp;
static int g_stampId = 1;

static float g_col[4] = { 1, 1, 1, 1 };
static bool g_texEnabled = true;

struct Vtx2 { float x, y, u, v; };
static GLenum g_mode = 0;
static std::vector<Vtx2> g_verts;

static void putPixel(int x, int y, u32 c) {
    if (x < 0 || y < 0 || x >= g_fbW || y >= g_fbH) return;
    u32* d = &g_fb[(size_t)y * g_fbW + x];
    float a = ((c >> 24) & 255) / 255.f * g_col[3];
    if (a <= 0.001f) return;
    int sr = (int)((c >> 16) & 255), sg = (int)((c >> 8) & 255), sb = (int)(c & 255);
    sr = (int)(sr * g_col[0]); sg = (int)(sg * g_col[1]); sb = (int)(sb * g_col[2]);
    int dr = (int)((*d >> 16) & 255), dg = (int)((*d >> 8) & 255), db = (int)(*d & 255);
    int r = (int)(sr * a + dr * (1 - a));
    int g = (int)(sg * a + dg * (1 - a));
    int b = (int)(sb * a + db * (1 - a));
    *d = 0xFF000000u | (u32)(clampi(r, 0, 255) << 16) | (u32)(clampi(g, 0, 255) << 8) | (u32)clampi(b, 0, 255);
}

static u32 sampleTex(const Tex& t, float u, float v) {
    int x = (int)(u * t.w), y = (int)(v * t.h);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= t.w) x = t.w - 1;
    if (y >= t.h) y = t.h - 1;
    return t.px[(size_t)y * t.w + x];
}

static void rasterTri(const Vtx2& a, const Vtx2& b, const Vtx2& c) {
    const Tex* tx = nullptr;
    if (g_texEnabled) {
        auto it = g_tex.find(g_bound);
        if (it != g_tex.end() && it->second.w > 0) tx = &it->second;
    }
    float minX = fminf(a.x, fminf(b.x, c.x)), maxX = fmaxf(a.x, fmaxf(b.x, c.x));
    float minY = fminf(a.y, fminf(b.y, c.y)), maxY = fmaxf(a.y, fmaxf(b.y, c.y));
    if (maxX < 0 || maxY < 0 || minX > g_fbW || minY > g_fbH) return;
    int x0 = imax(0, (int)floorf(minX)), x1 = imin(g_fbW - 1, (int)ceilf(maxX));
    int y0 = imax(0, (int)floorf(minY)), y1 = imin(g_fbH - 1, (int)ceilf(maxY));
    float den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (fabsf(den) < 1e-9f) return;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int si = y * g_fbW + x;
            if (g_stamp[si] == g_stampId) continue;
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / den;
            float w1 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / den;
            float w2 = 1 - w0 - w1;
            if (w0 < -0.0005f || w1 < -0.0005f || w2 < -0.0005f) continue;
            float u = a.u * w0 + b.u * w1 + c.u * w2;
            float v = a.v * w0 + b.v * w1 + c.v * w2;
            u32 src = tx ? sampleTex(*tx, u, v) : 0xFFFFFFFFu;
            float ta = ((src >> 24) & 255) / 255.f;
            u32 out = (u32)((int)(ta * 255) << 24) | (src & 0x00FFFFFF);
            g_stamp[si] = g_stampId;
            putPixel(x, y, out);
        }
}

void glBegin(GLenum m) { g_mode = m; g_verts.clear(); }
void glEnd() {
    if (g_mode == GL_QUADS) {
        for (size_t i = 0; i + 3 < g_verts.size(); i += 4) {
            g_stampId++;
            rasterTri(g_verts[i], g_verts[i + 1], g_verts[i + 2]);
            rasterTri(g_verts[i], g_verts[i + 2], g_verts[i + 3]);
        }
    } else if (g_mode == GL_TRIANGLES) {
        for (size_t i = 0; i + 2 < g_verts.size(); i += 3)
            rasterTri(g_verts[i], g_verts[i + 1], g_verts[i + 2]);
    } else if (g_mode == GL_LINES) {
        for (size_t i = 0; i + 1 < g_verts.size(); i += 2) {
            float x0 = g_verts[i].x, y0 = g_verts[i].y, x1 = g_verts[i + 1].x, y1 = g_verts[i + 1].y;
            int steps = (int)fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)) + 1;
            for (int s = 0; s <= steps; s++) {
                float t = s / (float)steps;
                putPixel((int)(x0 + (x1 - x0) * t), (int)(y0 + (y1 - y0) * t), 0xFFFFFFFFu);
            }
        }
    }
    g_verts.clear();
}
static float g_lu = 0, g_lv = 0;
void glVertex2f(GLfloat x, GLfloat y) { g_verts.push_back({ x, y, g_lu, g_lv }); }
void glVertex2i(GLint x, GLint y) { glVertex2f((float)x, (float)y); }
void glVertex3f(GLfloat x, GLfloat y, GLfloat) { glVertex2f(x, y); }
void glTexCoord2f(GLfloat u, GLfloat v) { g_lu = u; g_lv = v; }
void glColor3f(GLfloat r, GLfloat g, GLfloat b) { g_col[0] = r; g_col[1] = g; g_col[2] = b; g_col[3] = 1; }
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { g_col[0] = r; g_col[1] = g; g_col[2] = b; g_col[3] = a; }
void glColor3ub(GLubyte r, GLubyte g, GLubyte b) { glColor3f(r / 255.f, g / 255.f, b / 255.f); }
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { glColor4f(r / 255.f, g / 255.f, b / 255.f, a / 255.f); }
void glEnable(GLenum c) { if (c == GL_TEXTURE_2D) g_texEnabled = true; }
void glDisable(GLenum c) { if (c == GL_TEXTURE_2D) g_texEnabled = false; }
void glBlendFunc(GLenum, GLenum) {}
void glMatrixMode(GLenum) {}
void glLoadIdentity() {}
void glPushMatrix() {}
void glPopMatrix() {}
void glOrtho(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble) {}
void glViewport(GLint, GLint, GLsizei w, GLsizei h) { g_fbW = w; g_fbH = h; }
void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat) {}
void glClear(GLbitfield) { for (auto& p : g_fb) p = 0xFF101510u; }
void glLineWidth(GLfloat) {}
void glGenTextures(GLsizei n, GLuint* ids) { for (int i = 0; i < n; i++) ids[i] = g_nextId++; }
void glDeleteTextures(GLsizei, const GLuint*) {}
void glBindTexture(GLenum, GLuint id) { g_bound = id; }
void glTexParameteri(GLenum, GLenum, GLint) {}
void glDepthMask(GLboolean) {}
void glScissor(GLint, GLint, GLsizei, GLsizei) {}
void glPixelStorei(GLenum, GLint) {}
void glTexImage2D(GLenum, GLint, GLint, GLsizei w, GLsizei h, GLint, GLenum, GLenum, const void* data) {
    Tex& t = g_tex[g_bound];
    t.w = w; t.h = h;
    t.px.assign((size_t)w * h, 0);
    if (data) memcpy(t.px.data(), data, (size_t)w * h * 4);
}

// ------------------------------------------------------------------ fake renderer
static u32 g_atlasPx[1024 * 1024];
static int g_atlasDim = 256;
struct FakeRenderer { int dummy; };
static FakeRenderer g_fake;

Renderer* renderCreate() { return (Renderer*)&g_fake; }
void renderDestroy(Renderer*) {}
bool renderInitGPU(Renderer*) { return true; }
void renderSetWindow(Renderer*, int, int) {}
void renderBuildChunk(Renderer*, Game&, Chunk&, int) {}
void renderFreeChunk(Renderer*, Chunk&) {}
void renderFreeChunkLod(Renderer*, Chunk&, int) {}
void renderRebuildFar(Renderer*, Game&) {}
void renderScene(Renderer*, Game&, float) {}
int renderStatsVerts(Renderer*) { return 100000; }
int renderStatsChunks(Renderer*) { return 149; }
int renderStatsFarVerts(Renderer*) { return 12000; }
void renderFlushTrash(Renderer*) {}
unsigned int renderAtlas(Renderer*) { return 1; }
unsigned int renderFontTex(Renderer*) { return 2; }

int main(int argc, char** argv) {
    const char* bgPath = nullptr;
    const char* outPath = "out/ui.png";
    int mode = 0;    // 0 hud, 1 inventory, 2 menu
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bg") && i + 1 < argc) bgPath = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outPath = argv[++i];
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) mode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--w") && i + 1 < argc) g_fbW = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--h") && i + 1 < argc) g_fbH = atoi(argv[++i]);
    }
    initBlocks();
    initItems();

    g_fb.assign((size_t)g_fbW * g_fbH, 0xFF6E9BC8u);
    g_stamp.assign((size_t)g_fbW * g_fbH, 0);
    // vertical sky gradient background
    for (int y = 0; y < g_fbH; y++) {
        float t = y / (float)g_fbH;
        int r = (int)(110 + 90 * t), g2 = (int)(150 + 40 * t), b = (int)(200 - 20 * t);
        u32 c = 0xFF000000u | (u32)(r << 16) | (u32)(g2 << 8) | (u32)b;
        for (int x = 0; x < g_fbW; x++) g_fb[(size_t)y * g_fbW + x] = c;
    }
    if (bgPath) {
        FILE* f = fopen(bgPath, "rb");
        if (f) {
            char magic[3] = { 0, 0, 0 };
            int w = 0, h = 0, mx = 0;
            if (fscanf(f, "%2s", magic) == 1 && !strcmp(magic, "P6")) {
                fscanf(f, "%d %d %d", &w, &h, &mx);
                fgetc(f);
                if (w == g_fbW && h == g_fbH) {
                    std::vector<u8> tmp((size_t)w * h * 3);
                    if (fread(tmp.data(), 1, tmp.size(), f) == tmp.size())
                        for (size_t i = 0; i < (size_t)w * h; i++) {
                            u32 c = 0xFF000000u | ((u32)tmp[i * 3] << 16) | ((u32)tmp[i * 3 + 1] << 8) | tmp[i * 3 + 2];
                            g_fb[i] = c;
                        }
                    printf("background: %s (%dx%d)\n", bgPath, w, h);
                }
            }
            fclose(f);
        }
    }

    // atlas + font textures
    {
        const u32* a = getAtlasRGBA();
        Tex& t = g_tex[1];
        t.w = ATLAS_SIZE; t.h = ATLAS_SIZE;
        t.px.assign(a, a + (size_t)ATLAS_SIZE * ATLAS_SIZE);
    }
    {
        const int CELL = 8, COLS = 8, ROWS = 8;
        int W = CELL * COLS, H = CELL * ROWS;
        Tex& t = g_tex[2];
        t.w = W; t.h = H;
        t.px.assign((size_t)W * H, 0x00FFFFFFu);
        for (int c = 32; c < 127; c++) {
            int idx = fontIndex((char)c);
            if (idx < 0 || idx >= 64) continue;
            const u8* cols = fontColumns((char)c);
            int cx = (idx % COLS) * CELL, cy = (idx / COLS) * CELL;
            for (int x = 0; x < 5; x++)
                for (int y = 0; y < 7; y++)
                    if (cols[x] & (1 << y)) t.px[(size_t)(cy + y) * W + (cx + x)] = 0xFFFFFFFFu;
        }
    }

    Game g;
    g.startWorld(512, 12345, GM_SURVIVAL);
    g.health = 78; g.satiety = 61; g.temperature = 12.5f; g.stability = 88;
    g.dayTime = 0.34f; g.dayCount = 12; g.season = 1;
    g.world.getOrCreateChunk(8, 8);
    g.hit = g.raycast(6.f);
    // a believable inventory
    struct { int item, count, dur; } it[] = {
        { I_KNIFE_FLINT, 1, 44 }, { I_AXE_FLINT, 1, 31 }, { I_PICK_FLINT, 1, 22 },
        { B_TORCH, 6, 0 }, { I_BREAD, 3, 0 }, { I_SEEDS, 8, 0 },
        { B_LOG_OAK, 12, 0 }, { B_COBBLE_GREY, 9, 0 }, { B_ORE_COPPER, 5, 0 }
    };
    for (int i = 0; i < 9; i++) { g.inv[i].item = it[i].item; g.inv[i].count = it[i].count; g.inv[i].dur = it[i].dur; }
    g.inv[9].item = I_FLINT; g.inv[9].count = 14;
    g.inv[10].item = I_STICK; g.inv[10].count = 21;
    g.inv[11].item = I_BOWL; g.inv[11].count = 2;
    g.inv[12].item = I_INGOT_COPPER; g.inv[12].count = 3;
    g.inv[13].item = I_ROPE; g.inv[13].count = 1;
    g.inv[14].item = I_STEW; g.inv[14].count = 1;
    g.notify("WELCOME TO VINTAGEBLOCK");

    g.screen = 1;
    if (mode == 1) { g.invOpen = true; }
    if (mode == 2) { g.screen = 0; strcpy(g.uiSeed, "1234567"); g.uiSize = 1; }
    if (mode == 3) { g.craftOpen = true; }
    if (mode == 4) { g.paused = true; }

    renderUI((Renderer*)&g_fake, g, (float)(g_fbW * (mode == 1 ? 0.42f : 0.5f)), (float)(g_fbH * 0.52f), g_fbW, g_fbH);
    writePNG(outPath, g_fbW, g_fbH, g_fb.data(), false);
    printf("wrote %s\n", outPath);
    return 0;
}
