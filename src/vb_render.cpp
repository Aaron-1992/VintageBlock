// vb_render.cpp : world rendering (Windows, OpenGL 1.1 + optional VBO fast path)
#include "vb_render.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

// ------------------------------------------------------------------ VBO loader
#include <cstddef>
typedef ptrdiff_t GLsizeiptrARB;
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif
typedef void (APIENTRY* PFNGENBUFFERS)(GLsizei, GLuint*);
typedef void (APIENTRY* PFNBINDBUFFER)(GLenum, GLuint);
typedef void (APIENTRY* PFNBUFFERDATA)(GLenum, GLsizeiptrARB, const void*, GLenum);
typedef void (APIENTRY* PFNDELETEBUFFERS)(GLsizei, const GLuint*);
typedef char* (APIENTRY* PFNGETPROCADDR)(const char*);

#ifndef GL_ARRAY_BUFFER_ARB
#define GL_ARRAY_BUFFER_ARB 0x8892
#define GL_STATIC_DRAW_ARB 0x88E4
#endif

static PFNGENBUFFERS pglGenBuffers = nullptr;
static PFNBINDBUFFER pglBindBuffer = nullptr;
static PFNBUFFERDATA pglBufferData = nullptr;
static PFNDELETEBUFFERS pglDeleteBuffers = nullptr;
static bool g_haveVBO = false;

#ifndef _WIN32
typedef long long GLsizeiptrARB_fallback;
#endif

// ------------------------------------------------------------------ types
struct ChunkMesh {
    GLuint vbo = 0;
    int verts = 0;
    int lod = -1;
    MeshData* cpu = nullptr;    // kept when VBOs are unavailable
    bool ready = false;
};

struct Renderer {
    GLuint atlas = 0;
    GLuint fontTex = 0;
    GLuint cloudTex = 0;
    int winW = 1280, winH = 720;
    GLuint farVbo = 0;
    int farVerts = 0;
    int statVerts = 0;
    int statChunks = 0;
    float farCenterX = -9999, farCenterZ = -9999;
    std::vector<ChunkMesh*> trash;
    int frames = 0;
};

// ------------------------------------------------------------------ helpers
static inline float imaxf(float a, float b) { return a > b ? a : b; }

struct SkyColors { u32 zenith, horizon, sun, ambient, fog; float night; };

static SkyColors skyFor(float dayTime) {
    SkyColors s;
    // 0 = midnight, 0.25 = sunrise, 0.5 = noon, 0.75 = sunset
    float t = dayTime;
    float dawn = saturate(1.f - fabsf(t - 0.25f) * 9.f);
    float dusk = saturate(1.f - fabsf(t - 0.75f) * 9.f);
    float day = saturate(sinf(t * TAU - PI * 0.5f) * 1.6f + 0.45f);
    (void)day;
    u32 nightZen = rgb(10, 12, 24), dayZen = rgb(86, 140, 202);
    u32 nightHor = rgb(22, 26, 40), dayHor = rgb(196, 214, 216);
    float l = saturate(day * 0.5f + 0.5f);
    s.zenith = lerpColor(nightZen, dayZen, l);
    s.horizon = lerpColor(nightHor, dayHor, l);
    u32 warm = rgb(238, 168, 96);
    float warmness = imaxf(dawn, dusk);
    s.horizon = lerpColor(s.horizon, warm, warmness * 0.85f);
    s.zenith = lerpColor(s.zenith, rgb(120, 120, 176), warmness * 0.35f);
    s.sun = warm;
    s.night = 1.f - l;
    float amb = 0.20f + 0.80f * l;
    s.ambient = rgb((int)(amb * 255 * lerpf(1.f, 1.04f, warmness)),
                    (int)(amb * 255 * lerpf(1.f, 0.86f, warmness)),
                    (int)(amb * 255 * lerpf(1.f, 0.74f, warmness)));
    s.fog = lerpColor(s.horizon, s.zenith, 0.25f);
    return s;
}

// ------------------------------------------------------------------ creation
Renderer* renderCreate() {
    Renderer* r = new Renderer();
    return r;
}

void renderSetWindow(Renderer* r, int w, int h) { r->winW = w; r->winH = h; }

void renderDestroy(Renderer* r) {
    delete r;
}

static void loadVboFuncs() {
    PFNGETPROCADDR getProc = (PFNGETPROCADDR)wglGetProcAddress;
    if (!getProc) return;
    pglGenBuffers = (PFNGENBUFFERS)getProc("glGenBuffers");
    pglBindBuffer = (PFNBINDBUFFER)getProc("glBindBuffer");
    pglBufferData = (PFNBUFFERDATA)getProc("glBufferData");
    pglDeleteBuffers = (PFNDELETEBUFFERS)getProc("glDeleteBuffers");
    if (!pglGenBuffers) {
        pglGenBuffers = (PFNGENBUFFERS)getProc("glGenBuffersARB");
        pglBindBuffer = (PFNBINDBUFFER)getProc("glBindBufferARB");
        pglBufferData = (PFNBUFFERDATA)getProc("glBufferDataARB");
        pglDeleteBuffers = (PFNDELETEBUFFERS)getProc("glDeleteBuffersARB");
    }
    g_haveVBO = pglGenBuffers && pglBindBuffer && pglBufferData && pglDeleteBuffers;
}

static void uploadAtlas(Renderer* r) {
    glGenTextures(1, &r->atlas);
    glBindTexture(GL_TEXTURE_2D, r->atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    int size = ATLAS_SIZE;
    std::vector<u32> buf;
    for (int level = 0; level <= 8 && size >= 1; level++) {
        buf.resize((size_t)size * size);
        if (level <= 3) {
            buildAtlasLevel(buf.data(), size, level);
        } else {
            // simple box filter down to 1x1
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
            break;
        }
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
        size /= 2;
    }
}

static void uploadFont(Renderer* r) {
    const int CELL = 8, COLS = 8, ROWS = 8;
    int W = CELL * COLS, H = CELL * ROWS;
    std::vector<u32> img((size_t)W * H, 0);
    for (int c = 32; c < 127; c++) {
        int idx = fontIndex((char)c);
        if (idx < 0 || idx >= 64) continue;
        const u8* cols = fontColumns((char)c);
        int cx = (idx % COLS) * CELL, cy = (idx / COLS) * CELL;
        for (int x = 0; x < 5; x++)
            for (int y = 0; y < 7; y++)
                if (cols[x] & (1 << y))
                    img[(size_t)(cy + y) * W + (cx + x)] = 0xFFFFFFFF;
    }
    for (int i = 0; i < W * H; i++) if (!img[i]) img[i] = 0x00FFFFFF;
    glGenTextures(1, &r->fontTex);
    glBindTexture(GL_TEXTURE_2D, r->fontTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
}

static void uploadClouds(Renderer* r) {
    const int N = 128;
    std::vector<u32> img((size_t)N * N);
    Noise n;
    n.init(9182);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float f = 0;
            float fx = x / (float)N, fy = y / (float)N;
            // tileable-ish fbm
            for (int oct = 0; oct < 4; oct++) {
                float s = (float)(1 << oct);
                float v = n.n2(fx * s * TAU, fy * s * TAU);
                f += v / s;
            }
            f = saturate(f * 1.6f + 0.5f);
            int a = (int)(saturate((f - 0.58f) * 3.2f) * 210);
            img[(size_t)y * N + x] = rgb(255, 255, 255) | ((u32)a << 24);
        }
    glGenTextures(1, &r->cloudTex);
    glBindTexture(GL_TEXTURE_2D, r->cloudTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
}

bool renderInitGPU(Renderer* r) {
    loadVboFuncs();
    uploadAtlas(r);
    uploadFont(r);
    uploadClouds(r);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glShadeModel(GL_SMOOTH);
    glAlphaFunc(GL_GREATER, 0.5f);
    glEnable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glHint(GL_FOG_HINT, GL_NICEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT);
    GLfloat zero[4] = { 0, 0, 0, 1 };
    glLightfv(GL_LIGHT0, GL_DIFFUSE, zero);
    glLightfv(GL_LIGHT0, GL_SPECULAR, zero);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, zero);
    GLfloat matSpec[4] = { 0, 0, 0, 1 };
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, matSpec);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, zero);
    return true;
}

// ------------------------------------------------------------------ meshes
static void freeGpu(Renderer* r, ChunkMesh* m) {
    if (!m) return;
    if (m->vbo && pglDeleteBuffers) { r->trash.push_back(m); return; }
    delete m->cpu;
    delete m;
}

void renderFreeChunkLod(Renderer* r, Chunk& c, int lod) {
    if (lod < 0 || lod > 3 || !c.meshes[lod]) return;
    ChunkMesh* m = (ChunkMesh*)c.meshes[lod];
    freeGpu(r, m);
    c.meshes[lod] = nullptr;
}

void renderFreeChunk(Renderer* r, Chunk& c) {
    for (int i = 0; i < 4; i++) {
        if (c.meshes[i]) {
            ChunkMesh* m = (ChunkMesh*)c.meshes[i];
            freeGpu(r, m);
            c.meshes[i] = nullptr;
        }
    }
}

void renderBuildChunk(Renderer* r, Game& g, Chunk& c, int lod) {
    ChunkMesh* m = (ChunkMesh*)c.meshes[lod];
    if (!m) {
        m = new ChunkMesh();
        c.meshes[lod] = m;
    }
    MeshData* md = m->cpu;
    if (!md) md = new MeshData();
    int cullY = (int)(g.pos.y) - 40;
    buildChunkMesh(g.world, c, lod, *md, cullY);
    m->lod = lod;
    m->verts = (int)md->v.size();
    if (g_haveVBO) {
        if (!m->vbo) pglGenBuffers(1, &m->vbo);
        pglBindBuffer(GL_ARRAY_BUFFER_ARB, m->vbo);
        pglBufferData(GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)(md->v.size() * sizeof(Vtx)),
                      md->v.empty() ? nullptr : md->v.data(), GL_STATIC_DRAW_ARB);
        pglBindBuffer(GL_ARRAY_BUFFER_ARB, 0);
        delete md;
        m->cpu = nullptr;
    } else {
        m->cpu = md;
    }
    m->ready = true;
}

void renderRebuildFar(Renderer* r, Game& g) {
    if (!g_haveVBO) return;   // rebuilt on the fly at draw time
    MeshData farMesh;
    float skip = g.viewDistance * CH * 0.78f;
    float maxR = (float)g.world.size * 1.4f;
    int step = g.world.size >= 1024 ? 16 : (g.world.size >= 512 ? 12 : 8);
    buildCoarseMesh(g.world, g.pos.x, g.pos.z, skip, maxR, step, farMesh);
    r->farCenterX = g.pos.x;
    r->farCenterZ = g.pos.z;
    r->farVerts = (int)farMesh.v.size();
    if (!r->farVbo) pglGenBuffers(1, &r->farVbo);
    pglBindBuffer(GL_ARRAY_BUFFER_ARB, r->farVbo);
    pglBufferData(GL_ARRAY_BUFFER_ARB, (GLsizeiptrARB)(farMesh.v.size() * sizeof(Vtx)),
                  farMesh.v.empty() ? nullptr : farMesh.v.data(), GL_STATIC_DRAW_ARB);
    pglBindBuffer(GL_ARRAY_BUFFER_ARB, 0);
}

// ------------------------------------------------------------------ drawing
static v3 g_farFallback;   // unused placeholder

static void setupClientArrays(const MeshData& md) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    const char* p = (const char*)md.v.data();
    glVertexPointer(3, GL_FLOAT, sizeof(Vtx), p);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vtx), p + 12);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vtx), p + 20);
}
static void setupVboArrays() {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(Vtx), (void*)0);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vtx), (void*)12);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vtx), (void*)20);
}

static void drawSky(Renderer* r, Game& g, const SkyColors& sky) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    float zr = (sky.zenith >> 16 & 255) / 255.f, zg = (sky.zenith >> 8 & 255) / 255.f, zb = (sky.zenith & 255) / 255.f;
    float hr = (sky.horizon >> 16 & 255) / 255.f, hg = (sky.horizon >> 8 & 255) / 255.f, hb = (sky.horizon & 255) / 255.f;
    glBegin(GL_QUADS);
    glColor3f(zr, zg, zb); glVertex2f(0, 1); glVertex2f(1, 1);
    glColor3f(hr, hg, hb); glVertex2f(1, 0.42f); glVertex2f(0, 0.42f);
    glColor3f(hr, hg, hb); glVertex2f(0, 0.42f); glVertex2f(1, 0.42f);
    glVertex2f(1, 0); glVertex2f(0, 0);
    glEnd();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 1);
}

static void drawCelestial(Renderer* r, Game& g, const SkyColors& sky) {
    // sun / moon as billboards far away
    float ang = g.dayTime * TAU - PI * 0.5f;
    v3 sunDir(cosf(ang) * 0.55f, sinf(ang), 0.83f);
    sunDir = sunDir.norm();
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    // project direction to a rough screen position using the camera
    float yaw = g.yaw, pitch = g.pitch;
    v3 fwd(cosf(yaw) * cosf(pitch), sinf(pitch), sinf(yaw) * cosf(pitch));
    v3 right = fwd.cross(v3(0, 1, 0)).norm() * -1.f;
    v3 up = right.cross(fwd);
    float sx = sunDir.dot(right), sy = sunDir.dot(up), sz = sunDir.dot(fwd);
    if (sz > 0.05f) {
        float px = sx / sz * 0.75f, py = sy / sz * 0.75f;
        bool isSun = g.dayTime > 0.22f && g.dayTime < 0.78f;
        float size = 0.12f;
        u32 c = isSun ? rgb(255, 236, 180) : rgb(210, 220, 240);
        float a = isSun ? 1.f : 0.85f;
        glColor4f((c >> 16 & 255) / 255.f, (c >> 8 & 255) / 255.f, (c & 255) / 255.f, a * 0.35f);
        glBegin(GL_QUADS);
        glVertex2f(px - size * 3, py - size * 3); glVertex2f(px + size * 3, py - size * 3);
        glVertex2f(px + size * 3, py + size * 3); glVertex2f(px - size * 3, py + size * 3);
        glEnd();
        glColor4f((c >> 16 & 255) / 255.f, (c >> 8 & 255) / 255.f, (c & 255) / 255.f, a);
        glBegin(GL_QUADS);
        glVertex2f(px - size, py - size); glVertex2f(px + size, py - size);
        glVertex2f(px + size, py + size); glVertex2f(px - size, py + size);
        glEnd();
    }
    // stars
    if (sky.night > 0.15f) {
        glColor4f(1, 1, 1, sky.night * 0.9f);
        glBegin(GL_QUADS);
        for (int i = 0; i < 220; i++) {
            u32 h = hash32((u32)i * 2654435761u);
            float az = (h & 1023) / 1023.f * TAU;
            float el = ((h >> 10) & 1023) / 1023.f * 1.3f + 0.05f;
            v3 d(cosf(az) * cosf(el), sinf(el), sinf(az) * cosf(el));
            float z = d.dot(fwd);
            if (z < 0.1f) continue;
            float px = d.dot(right) / z * 0.75f, py = d.dot(up) / z * 0.75f;
            if (fabsf(px) > 1 || fabsf(py) > 1) continue;
            float s = 0.004f + ((h >> 20) & 7) * 0.0012f;
            glVertex2f(px - s, py - s); glVertex2f(px + s, py - s);
            glVertex2f(px + s, py + s); glVertex2f(px - s, py + s);
        }
        glEnd();
    }
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_FOG);
}

static void boxTex(float x0, float y0, float z0, float x1, float y1, float z1, int tile, u32 tint) {
    float u0, v0, u1, v1;
    tileUV(tile, u0, v0, u1, v1);
    float r = ((tint >> 16 & 255) / 255.f), gg = ((tint >> 8 & 255) / 255.f), b = ((tint & 255) / 255.f);
    glColor4f(r, gg, b, 1);
    glBegin(GL_QUADS);
    // +X
    glTexCoord2f(u0, v0); glVertex3f(x1, y1, z0);
    glTexCoord2f(u0, v1); glVertex3f(x1, y0, z0);
    glTexCoord2f(u1, v1); glVertex3f(x1, y0, z1);
    glTexCoord2f(u1, v0); glVertex3f(x1, y1, z1);
    // -X
    glTexCoord2f(u0, v0); glVertex3f(x0, y1, z1);
    glTexCoord2f(u0, v1); glVertex3f(x0, y0, z1);
    glTexCoord2f(u1, v1); glVertex3f(x0, y0, z0);
    glTexCoord2f(u1, v0); glVertex3f(x0, y1, z0);
    // +Y
    glTexCoord2f(u0, v0); glVertex3f(x0, y1, z0);
    glTexCoord2f(u1, v0); glVertex3f(x1, y1, z0);
    glTexCoord2f(u1, v1); glVertex3f(x1, y1, z1);
    glTexCoord2f(u0, v1); glVertex3f(x0, y1, z1);
    // -Y
    glTexCoord2f(u0, v0); glVertex3f(x0, y0, z1);
    glTexCoord2f(u1, v0); glVertex3f(x1, y0, z1);
    glTexCoord2f(u1, v1); glVertex3f(x1, y0, z0);
    glTexCoord2f(u0, v1); glVertex3f(x0, y0, z0);
    // +Z
    glTexCoord2f(u0, v0); glVertex3f(x1, y1, z1);
    glTexCoord2f(u0, v1); glVertex3f(x1, y0, z1);
    glTexCoord2f(u1, v1); glVertex3f(x0, y0, z1);
    glTexCoord2f(u1, v0); glVertex3f(x0, y1, z1);
    // -Z
    glTexCoord2f(u0, v0); glVertex3f(x0, y1, z0);
    glTexCoord2f(u0, v1); glVertex3f(x0, y0, z0);
    glTexCoord2f(u1, v1); glVertex3f(x1, y0, z0);
    glTexCoord2f(u1, v0); glVertex3f(x1, y1, z0);
    glEnd();
}

static void drawEntity(Renderer* r, Game& g, Entity& e, const SkyColors& sky) {
    (void)r;
    float amb = 0.25f + 0.75f * (1.f - sky.night);
    u32 tint = rgb((int)(255 * amb), (int)(255 * amb), (int)(255 * amb));
    if (e.hurtFlash > 0) tint = rgb(255, 120, 120);
    glPushMatrix();
    glTranslatef(e.pos.x, e.pos.y, e.pos.z);
    glRotatef(-e.yaw * 180.f / PI, 0, 1, 0);
    float bob = sinf(e.animPhase) * 0.05f;
    switch (e.type) {
    case ENT_HARE: {
        glTranslatef(0, bob + 0.18f, 0);
        boxTex(-0.22f, -0.18f, -0.38f, 0.22f, 0.18f, 0.38f, T_E_FUR_BROWN, tint);
        boxTex(-0.16f, -0.02f, 0.34f, 0.16f, 0.28f, 0.56f, T_E_FUR_BROWN, tint);
        boxTex(0.13f, 0.14f, 0.40f, 0.17f, 0.46f, 0.48f, T_E_FUR_BROWN, tint);
        boxTex(-0.17f, 0.14f, 0.40f, -0.13f, 0.46f, 0.48f, T_E_FUR_BROWN, tint);
    } break;
    case ENT_CHICKEN: {
        glTranslatef(0, bob + 0.22f, 0);
        boxTex(-0.18f, -0.16f, -0.22f, 0.18f, 0.20f, 0.22f, T_E_FUR_WHITE, tint);
        boxTex(-0.10f, 0.16f, 0.14f, 0.10f, 0.40f, 0.34f, T_E_FUR_WHITE, tint);
        boxTex(-0.05f, 0.22f, 0.34f, 0.05f, 0.30f, 0.42f, T_E_BEAK, tint);
        boxTex(0.02f, 0.32f, 0.20f, 0.06f, 0.38f, 0.26f, T_E_EYE, tint);
        boxTex(-0.06f, 0.32f, 0.20f, -0.02f, 0.38f, 0.26f, T_E_EYE, tint);
    } break;
    case ENT_WOLF: {
        glTranslatef(0, bob + 0.42f, 0);
        boxTex(-0.28f, -0.20f, -0.60f, 0.28f, 0.26f, 0.55f, T_E_FUR_GREY, tint);
        boxTex(-0.22f, 0.16f, 0.50f, 0.22f, 0.60f, 0.92f, T_E_FUR_GREY, tint);
        boxTex(-0.16f, 0.30f, 0.86f, -0.06f, 0.42f, 0.96f, T_E_EYE, tint);
        boxTex(0.06f, 0.30f, 0.86f, 0.16f, 0.42f, 0.96f, T_E_EYE, tint);
        boxTex(-0.26f, -0.55f, -0.50f, -0.10f, -0.16f, -0.34f, T_E_FUR_GREY, tint);
        boxTex(0.10f, -0.55f, -0.50f, 0.26f, -0.16f, -0.34f, T_E_FUR_GREY, tint);
        boxTex(-0.26f, -0.55f, 0.28f, -0.10f, -0.16f, 0.44f, T_E_FUR_GREY, tint);
        boxTex(0.10f, -0.55f, 0.28f, 0.26f, -0.16f, 0.44f, T_E_FUR_GREY, tint);
    } break;
    default: {
        // drifter : tall, dark, glowing eyes
        glTranslatef(0, bob * 0.5f, 0);
        boxTex(-0.30f, 0.55f, -0.20f, 0.30f, 1.55f, 0.24f, T_E_DRIFTER, tint);
        boxTex(-0.24f, 1.52f, -0.16f, 0.24f, 1.95f, 0.20f, T_E_DRIFTER, tint);
        boxTex(-0.18f, 1.68f, 0.20f, -0.05f, 1.82f, 0.23f, T_E_DRIFTER_EYE, tint);
        boxTex(0.05f, 1.68f, 0.20f, 0.18f, 1.82f, 0.23f, T_E_DRIFTER_EYE, tint);
        float arm = sinf(e.animPhase) * 0.28f;
        boxTex(-0.46f, 0.45f, -0.14f, -0.30f, 1.45f + arm * 0.4f, 0.14f, T_E_DRIFTER, tint);
        boxTex(0.30f, 0.45f, -0.14f, 0.46f, 1.45f - arm * 0.4f, 0.14f, T_E_DRIFTER, tint);
        boxTex(-0.26f, 0.0f, -0.16f, -0.04f, 0.58f, 0.12f, T_E_DRIFTER, tint);
        boxTex(0.04f, 0.0f, -0.16f, 0.26f, 0.58f, 0.12f, T_E_DRIFTER, tint);
    } break;
    }
    glPopMatrix();
    glColor4f(1, 1, 1, 1);
}

static void drawClouds(Renderer* r, Game& g, float time, const SkyColors& sky) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, r->cloudTex);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glColor4f(1, 1, 1, 0.72f * (1.f - sky.night * 0.7f));
    float y = 214.f;
    float size = 1400.f;
    float scroll = time * 1.6f;
    float cx = g.pos.x, cz = g.pos.z;
    glBegin(GL_QUADS);
    glTexCoord2f(0 + scroll * 0.004f, 0); glVertex3f(cx - size, y, cz - size);
    glTexCoord2f(0 + scroll * 0.004f, 6); glVertex3f(cx - size, y, cz + size);
    glTexCoord2f(6 + scroll * 0.004f, 6); glVertex3f(cx + size, y, cz + size);
    glTexCoord2f(6 + scroll * 0.004f, 0); glVertex3f(cx + size, y, cz - size);
    glEnd();
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
    glEnable(GL_FOG);
}

void renderScene(Renderer* r, Game& g, float timeSec) {
    SkyColors sky = skyFor(g.dayTime);
    glViewport(0, 0, r->winW, r->winH);
    glClearColor((sky.horizon >> 16 & 255) / 255.f, (sky.horizon >> 8 & 255) / 255.f, (sky.horizon & 255) / 255.f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawSky(r, g, sky);
    drawCelestial(r, g, sky);

    // fog
    GLfloat fogCol[4] = { (sky.fog >> 16 & 255) / 255.f, (sky.fog >> 8 & 255) / 255.f, (sky.fog & 255) / 255.f, 1 };
    glFogfv(GL_FOG_COLOR, fogCol);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, g.viewDistance * CH * 0.55f);
    glFogf(GL_FOG_END, g.viewDistance * CH * 1.05f + 40.f);
    glEnable(GL_FOG);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float fov = 72.f;
    {
        Mat4 p = Mat4::perspective(fov, (float)r->winW / (float)r->winH, 0.08f, 2600.f);
        glLoadMatrixf(p.m);
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    v3 fwd(cosf(g.yaw) * cosf(g.pitch), sinf(g.pitch), sinf(g.yaw) * cosf(g.pitch));
    v3 eye = g.eyePos();
    Mat4 view = Mat4::mul(Mat4::lookRot(fwd, v3(0, 1, 0)), Mat4::translate(v3(-eye.x, -eye.y, -eye.z)));
    glLoadMatrixf(view.m);

    // global day light through the ambient/colour-material trick
    GLfloat amb[4] = { (sky.ambient >> 16 & 255) / 255.f, (sky.ambient >> 8 & 255) / 255.f, (sky.ambient & 255) / 255.f, 1 };
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);

    drawClouds(r, g, timeSec, sky);

    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, r->atlas);
    glEnable(GL_ALPHA_TEST);

    // ---- detailed chunks
    Frustum fr;
    Mat4 vp = Mat4::mul(Mat4::perspective(fov, (float)r->winW / (float)r->winH, 0.08f, 2600.f), view);
    fr.fromMatrix(vp);
    r->statVerts = 0;
    r->statChunks = 0;
    int nc = g.world.chunksX;
    for (int cz = 0; cz < nc; cz++)
        for (int cx = 0; cx < nc; cx++) {
            Chunk* c = g.world.chunkAt(cx, cz);
            if (!c) continue;
            float x0 = (float)(cx * CH), z0 = (float)(cz * CH);
            if (!fr.boxVisible(x0 - 2, 0, z0 - 2, x0 + CH + 2, (float)WH, z0 + CH + 2)) continue;
            float dx = (x0 + CH * 0.5f) - g.pos.x, dz = (z0 + CH * 0.5f) - g.pos.z;
            float dist = sqrtf(dx * dx + dz * dz);
            int lod = 0;
            if (dist > g.viewDistance * CH * 0.62f) lod = 1;
            if (dist > g.viewDistance * CH * 0.82f) lod = 2;
            if (dist > g.viewDistance * CH) lod = 3;
            ChunkMesh* m = (ChunkMesh*)c->meshes[lod];
            if (!m || !m->ready) continue;
            if (m->verts == 0) continue;
            if (true) {
            if (g_haveVBO && m->vbo) {
                pglBindBuffer(GL_ARRAY_BUFFER_ARB, m->vbo);
                setupVboArrays();
                glDrawArrays(GL_QUADS, 0, m->verts);
                glDisableClientState(GL_VERTEX_ARRAY);
                glDisableClientState(GL_COLOR_ARRAY);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                pglBindBuffer(GL_ARRAY_BUFFER_ARB, 0);
            } else if (m->cpu && !m->cpu->v.empty()) {
                // cpu fallback meshes are stored in chunk-local space as well
                setupClientArrays(*m->cpu);
                glDrawArrays(GL_QUADS, 0, (GLsizei)m->cpu->v.size());
                glDisableClientState(GL_VERTEX_ARRAY);
                glDisableClientState(GL_COLOR_ARRAY);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            }
            }
            r->statVerts += m->verts;
            r->statChunks++;
        }

    // ---- far coarse terrain
    if (!g_haveVBO) {
        // no VBO: rebuild on the fly (cheap, low vertex count)
        static MeshData farCpuMesh;
        buildCoarseMesh(g.world, g.pos.x, g.pos.z, g.viewDistance * CH * 0.78f,
                        (float)g.world.size * 1.4f,
                        g.world.size >= 1024 ? 16 : (g.world.size >= 512 ? 12 : 8), farCpuMesh);
        if (!farCpuMesh.v.empty()) {
            setupClientArrays(farCpuMesh);
            glDrawArrays(GL_QUADS, 0, (GLsizei)farCpuMesh.v.size());
            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        r->farVerts = (int)farCpuMesh.v.size();
    } else if (r->farVbo && r->farVerts > 0) {
        pglBindBuffer(GL_ARRAY_BUFFER_ARB, r->farVbo);
        setupVboArrays();
        glDrawArrays(GL_QUADS, 0, r->farVerts);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        pglBindBuffer(GL_ARRAY_BUFFER_ARB, 0);
    }

    // ---- entities
    glDisable(GL_FOG);
    for (auto& e : g.entities) drawEntity(r, g, e, sky);
    glEnable(GL_FOG);

    // ---- particles
    if (!g.particles.empty()) {
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glBegin(GL_QUADS);
        for (auto& p : g.particles) {
            float a = saturate(p.life * 2.f);
            if (a <= 0.02f) continue;
            glColor4ub((u8)(p.color >> 16), (u8)(p.color >> 8), (u8)p.color, (u8)(a * 255));
            float s = p.size;
            glVertex3f(p.pos.x - s, p.pos.y - s, p.pos.z - s);
            glVertex3f(p.pos.x + s, p.pos.y - s, p.pos.z - s);
            glVertex3f(p.pos.x + s, p.pos.y + s, p.pos.z - s);
            glVertex3f(p.pos.x - s, p.pos.y + s, p.pos.z - s);
        }
        glEnd();
        glEnable(GL_TEXTURE_2D);
        glColor4f(1, 1, 1, 1);
    }

    // ---- weather
    if (g.weather != 0) {
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_LIGHTING);
        glDisable(GL_FOG);
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
        static float fall = 0.f;
        fall += 0.02f;
        glLineWidth(1.5f);
        bool snow = (g.weather == 2);
        glColor4ub(snow ? 240 : 170, snow ? 245 : 200, snow ? 255 : 230, snow ? 190 : 150);
        glBegin(GL_LINES);
        for (int i = 0; i < 420; i++) {
            u32 h = hash32((u32)i * 2246822519u);
            float rx = g.pos.x + ((h & 1023) / 1023.f - 0.5f) * 34.f;
            float rz = g.pos.z + (((h >> 10) & 1023) / 1023.f - 0.5f) * 34.f;
            float ph = ((h >> 20) & 1023) / 1023.f;
            float ry = g.pos.y + 16.f - fmodf(ph * 24.f + fall * (snow ? 3.f : 14.f), 24.f);
            if (snow) {
                float s2 = 0.07f;
                glVertex3f(rx - s2, ry, rz);
                glVertex3f(rx + s2, ry, rz);
            } else {
                glVertex3f(rx, ry, rz);
                glVertex3f(rx + 0.06f, ry - 0.8f, rz + 0.02f);
            }
        }
        glEnd();
        glDepthMask(GL_TRUE);
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_LIGHTING);
        glEnable(GL_FOG);
        glColor4f(1, 1, 1, 1);
    }

    // ---- block highlight
    if (g.hit.hit) {
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glColor4f(0, 0, 0, 0.55f);
        glLineWidth(2.f);
        float x = (float)g.hit.x, y = (float)g.hit.y, z = (float)g.hit.z;
        float e = 0.003f;
        glBegin(GL_LINES);
        for (int i = 0; i < 4; i++) {
            float ax = (i & 1) ? x + 1 + e : x - e;
            float az = (i & 2) ? z + 1 + e : z - e;
            glVertex3f(ax, y - e, az); glVertex3f(ax, y + 1 + e, az);
        }
        // top + bottom rectangles
        glVertex3f(x - e, y + 1 + e, z - e); glVertex3f(x + 1 + e, y + 1 + e, z - e);
        glVertex3f(x + 1 + e, y + 1 + e, z - e); glVertex3f(x + 1 + e, y + 1 + e, z + 1 + e);
        glVertex3f(x + 1 + e, y + 1 + e, z + 1 + e); glVertex3f(x - e, y + 1 + e, z + 1 + e);
        glVertex3f(x - e, y + 1 + e, z + 1 + e); glVertex3f(x - e, y + 1 + e, z - e);
        glVertex3f(x - e, y - e, z - e); glVertex3f(x + 1 + e, y - e, z - e);
        glVertex3f(x + 1 + e, y - e, z - e); glVertex3f(x + 1 + e, y - e, z + 1 + e);
        glVertex3f(x + 1 + e, y - e, z + 1 + e); glVertex3f(x - e, y - e, z + 1 + e);
        glVertex3f(x - e, y - e, z + 1 + e); glVertex3f(x - e, y - e, z - e);
        glEnd();
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_LIGHTING);
        glColor4f(1, 1, 1, 1);
    }

    glDisable(GL_FOG);
}

unsigned int renderAtlas(Renderer* r) { return r->atlas; }
unsigned int renderFontTex(Renderer* r) { return r->fontTex; }
int renderStatsVerts(Renderer* r) { return r->statVerts; }
int renderStatsChunks(Renderer* r) { return r->statChunks; }
int renderStatsFarVerts(Renderer* r) { return r->farVerts; }

void renderFlushTrash(Renderer* r) {
    if (r->trash.empty()) return;
    for (auto* m : r->trash) {
        if (m->vbo && pglDeleteBuffers) { GLuint v = m->vbo; pglDeleteBuffers(1, &v); }
        delete m->cpu;
        delete m;
    }
    r->trash.clear();
}
