// tools/preview.cpp : offline renderer used to check worldgen / meshing / LODs
// (software rasteriser, no GPU required)
#include "../src/vb_mesh.h"
#include "../src/vb_png.h"
#include <cstdio>
#include <cstring>

struct FB {
    int w = 0, h = 0;
    std::vector<u32> col;
    std::vector<float> depth;
    std::vector<float> dist;
    void init(int ww, int hh) {
        w = ww; h = hh;
        col.assign((size_t)w * h, 0);
        depth.assign((size_t)w * h, 1e30f);
        dist.assign((size_t)w * h, 1e30f);
    }
};

static const u32* g_atlas = nullptr;
static v3 g_eye;

static inline u32 sampleAtlas(float u, float v) {
    int x = (int)(u * ATLAS_SIZE), y = (int)(v * ATLAS_SIZE);
    x = clampi(x, 0, ATLAS_SIZE - 1); y = clampi(y, 0, ATLAS_SIZE - 1);
    return g_atlas[(size_t)y * ATLAS_SIZE + x];
}

struct Cam { v3 pos, fwd; float fov = 70.f; };

static void rasterMesh(FB& fb, const MeshData& mesh, const Mat4& view, const Mat4& proj,
                       u32 fogColor, float fogStart, float fogEnd) {
    Mat4 vp = Mat4::mul(proj, view);
    const float* M = vp.m;
    float skyR = (fogColor >> 16 & 255) / 255.f, skyG = (fogColor >> 8 & 255) / 255.f, skyB = (fogColor & 255) / 255.f;
    for (size_t q = 0; q + 3 < mesh.v.size(); q += 4) {
        struct SV { float x, y, z, w, u, v, d; u32 c; };
        SV sv[4];
        bool behind = false;
        for (int k = 0; k < 4; k++) {
            const Vtx& vt = mesh.v[q + k];
            float cx = M[0] * vt.x + M[4] * vt.y + M[8] * vt.z + M[12];
            float cy = M[1] * vt.x + M[5] * vt.y + M[9] * vt.z + M[13];
            float cz = M[2] * vt.x + M[6] * vt.y + M[10] * vt.z + M[14];
            float cw = M[3] * vt.x + M[7] * vt.y + M[11] * vt.z + M[15];
            if (cw <= 0.05f) { behind = true; break; }
            sv[k].x = (cx / cw * 0.5f + 0.5f) * fb.w;
            sv[k].y = (1.f - (cy / cw * 0.5f + 0.5f)) * fb.h;
            sv[k].z = cz / cw;
            sv[k].w = cw;
            sv[k].u = vt.u; sv[k].v = vt.v; sv[k].c = vt.c;
            sv[k].d = sqrtf((vt.x - g_eye.x) * (vt.x - g_eye.x) + (vt.y - g_eye.y) * (vt.y - g_eye.y) + (vt.z - g_eye.z) * (vt.z - g_eye.z));
        }
        if (behind) continue;
        // distance for fog
        float d = sqrtf(mesh.v[q].x * mesh.v[q].x + mesh.v[q].y * mesh.v[q].y + mesh.v[q].z * mesh.v[q].z);
        (void)d;
        int tris[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
        for (int t = 0; t < 2; t++) {
            SV* a = &sv[tris[t][0]]; SV* b = &sv[tris[t][1]]; SV* c = &sv[tris[t][2]];
            float minx = fminf(a->x, fminf(b->x, c->x)), maxx = fmaxf(a->x, fmaxf(b->x, c->x));
            float miny = fminf(a->y, fminf(b->y, c->y)), maxy = fmaxf(a->y, fmaxf(b->y, c->y));
            int x0 = imax(0, (int)minx), x1 = imin(fb.w - 1, (int)maxx + 1);
            int y0 = imax(0, (int)miny), y1 = imin(fb.h - 1, (int)maxy + 1);
            if (x1 < x0 || y1 < y0) continue;
            float area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
            if (fabsf(area) < 1e-6f) continue;
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) {
                    float px = x + 0.5f, py = y + 0.5f;
                    float w0 = ((b->x - px) * (c->y - py) - (c->x - px) * (b->y - py)) / area;
                    float w1 = ((c->x - px) * (a->y - py) - (a->x - px) * (c->y - py)) / area;
                    float w2 = 1.f - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    float z = w0 * a->z + w1 * b->z + w2 * c->z;
                    size_t di = (size_t)y * fb.w + x;
                    if (z >= fb.depth[di]) continue;
                    float u = w0 * a->u + w1 * b->u + w2 * c->u;
                    float v = w0 * a->v + w1 * b->v + w2 * c->v;
                    u32 tex = sampleAtlas(u, v);
                    if ((tex >> 24) < 128) continue;
                    float r = ((a->c >> 16 & 255) * w0 + (b->c >> 16 & 255) * w1 + (c->c >> 16 & 255) * w2) / 255.f;
                    float g = ((a->c >> 8 & 255) * w0 + (b->c >> 8 & 255) * w1 + (c->c >> 8 & 255) * w2) / 255.f;
                    float bl = ((a->c & 255) * w0 + (b->c & 255) * w1 + (c->c & 255) * w2) / 255.f;
                    float tr = (tex >> 16 & 255) / 255.f * r;
                    float tg = (tex >> 8 & 255) / 255.f * g;
                    float tb = (tex & 255) / 255.f * bl;
                    float fog = saturate((z * 0 + 1.f) * 0.f);
                    (void)fog;
                    fb.depth[di] = z;
                    fb.dist[di] = w0 * a->d + w1 * b->d + w2 * c->d;
                    fb.col[di] = rgb((int)(tr * 255), (int)(tg * 255), (int)(tb * 255));
                }
        }
    }
    (void)skyR; (void)skyG; (void)skyB; (void)fogStart; (void)fogEnd;
}

// distance fog applied afterwards using the depth buffer + inverse projection is
// overkill for a preview, so we simply blend by screen depth
static void applyFog(FB& fb, u32 color, float fogStart, float fogEnd) {
    for (size_t i = 0; i < fb.col.size(); i++) {
        if (fb.dist[i] > 1e29f) continue;
        float t = saturate((fb.dist[i] - fogStart) / (fogEnd - fogStart));
        fb.col[i] = lerpColor(fb.col[i], color, t);
    }
}

static void drawSky(FB& fb, u32 top, u32 bottom) {
    for (int y = 0; y < fb.h; y++) {
        float t = (float)y / fb.h;
        u32 c = lerpColor(top, bottom, t);
        for (int x = 0; x < fb.w; x++) fb.col[(size_t)y * fb.w + x] = c;
    }
}

int main(int argc, char** argv) {
    u64 seed = 12345;
    int size = 512;
    int imgW = 1280, imgH = 720;
    float camX = 0, camY = 0, camZ = 0;
    bool autoCam = true;
    float yaw = 0, pitch = -12;
    const char* outPath = "out/preview.png";
    const char* mapPath = nullptr;
    int radius = 6;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (u64)atoll(argv[++i]);
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) size = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outPath = argv[++i];
        else if (!strcmp(argv[i], "--map") && i + 1 < argc) mapPath = argv[++i];
        else if (!strcmp(argv[i], "--cam") && i + 3 < argc) { camX = (float)atof(argv[++i]); camY = (float)atof(argv[++i]); camZ = (float)atof(argv[++i]); autoCam = false; }
        else if (!strcmp(argv[i], "--yaw") && i + 1 < argc) yaw = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--pitch") && i + 1 < argc) pitch = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--radius") && i + 1 < argc) radius = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--w") && i + 1 < argc) imgW = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--h") && i + 1 < argc) imgH = atoi(argv[++i]);
    }
    initBlocks();
    initItems();
    g_atlas = getAtlasRGBA();

    World w;
    w.init(size, seed);

    {
        int mn = 999, mx = -1; long sum = 0; int n = 0; int waterCells = 0;
        for (int cz = 0; cz < w.coarse.cells; cz++)
            for (int cx = 0; cx < w.coarse.cells; cx++) {
                int h = w.coarse.getHeight(cx, cz);
                mn = imin(mn, h); mx = imax(mx, h); sum += h; n++;
                if (h <= SEA_LEVEL) waterCells++;
            }
        printf("terrain height min=%d max=%d avg=%ld water=%.1f%%\n", mn, mx, sum / n, 100.0 * waterCells / n);
    }
    // top down map
    if (mapPath) {
        int cells = w.coarse.cells;
        int scale = 2048 / cells > 0 ? 2048 / cells : 1;
        int outN = cells * scale;
        if (outN > 2048) outN = 2048;
        std::vector<u32> img((size_t)outN * outN);
        for (int y = 0; y < outN; y++)
            for (int x = 0; x < outN; x++) {
                int cx = x / scale, cz = y / scale;
                u32 c = w.coarse.color[w.coarse.at(cx, cz)];
                int h = w.coarse.getHeight(cx, cz);
                // hillshade
                int hx = w.coarse.getHeight(imin(cx + 1, cells - 1), cz);
                int hz = w.coarse.getHeight(cx, imin(cz + 1, cells - 1));
                float shade = 1.0f + (h - hx) * 0.035f + (h - hz) * 0.025f;
                img[(size_t)y * outN + x] = shadeColor(c, clampf(shade, 0.55f, 1.5f));
            }
        writePNG(mapPath, outN, outN, img.data(), false);
        printf("wrote map %s\n", mapPath);
    }

    // find a nice camera spot: a high, open (tree free) column
    if (autoCam) {
        int bestH = -1;
        for (int cz = size / 8; cz < size * 7 / 8; cz += 6)
            for (int cx = size / 8; cx < size * 7 / 8; cx += 6) {
                int h = w.coarse.getHeight(cx / 4, cz / 4);
                if (h > bestH) { bestH = h; camX = (float)cx; camZ = (float)cz; }
            }
    }
    int ccx = (int)camX / CH, ccz = (int)camZ / CH;
    for (int dz = -radius; dz <= radius; dz++)
        for (int dx = -radius; dx <= radius; dx++)
            w.getOrCreateChunk(ccx + dx, ccz + dz);

    int ground = w.heightAt((int)camX, (int)camZ);
    // make sure the camera is not buried inside a canopy: find a clear column nearby
    for (int attempt = 0; attempt < 400; attempt++) {
        int gx = (int)camX, gz = (int)camZ;
        bool clear = true;
        int gh = w.heightAt(gx, gz);
        for (int y = gh + 1; y < gh + 12; y++) {
            u16 b = w.get(gx, y, gz);
            if (b != B_AIR) { clear = false; break; }
        }
        if (clear) { ground = gh; break; }
        camX += (float)((attempt % 9) - 4) * 3.f;
        camZ += (float)((attempt / 9) % 9 - 4) * 3.f;
        camX = clampf(camX, 12, (float)size - 12);
        camZ = clampf(camZ, 12, (float)size - 12);
    }
    ccx = (int)camX / CH; ccz = (int)camZ / CH;
    for (int dz = -radius; dz <= radius; dz++)
        for (int dx = -radius; dx <= radius; dx++)
            w.getOrCreateChunk(ccx + dx, ccz + dz);
    ground = w.heightAt((int)camX, (int)camZ);
    if (autoCam) camY = (float)ground + 2.f;

    MeshData all;
    for (int dz = -radius; dz <= radius; dz++)
        for (int dx = -radius; dx <= radius; dx++) {
            Chunk* c = w.chunkAt(ccx + dx, ccz + dz);
            if (!c) continue;
            float d = sqrtf((float)(dx * dx + dz * dz));
            int lod = 0;
            if (d > radius * 0.75f) lod = 1;
            if (d > radius * 0.9f) lod = 2;
            MeshData md;
            buildChunkMesh(w, *c, lod, md);
            all.v.insert(all.v.end(), md.v.begin(), md.v.end());
        }
    // far coarse terrain
    {
        MeshData far;
        buildCoarseMesh(w, camX, camZ, (float)(radius * CH), (float)(size * 2), 8, far);
        all.v.insert(all.v.end(), far.v.begin(), far.v.end());
        printf("far quads: %d\n", far.quads);
    }
    printf("quads: %d (%zu verts)\n", all.quads, all.v.size());

    FB fb;
    fb.init(imgW, imgH);
    u32 skyTop = rgb(96, 150, 206), skyBot = rgb(214, 224, 210);
    drawSky(fb, skyTop, skyBot);

    v3 fwd(cosf(yaw * PI / 180) * cosf(pitch * PI / 180), sinf(pitch * PI / 180),
           sinf(yaw * PI / 180) * cosf(pitch * PI / 180));
    v3 eye(camX, camY, camZ);
    g_eye = eye;
    Mat4 view = Mat4::mul(Mat4::lookRot(fwd, v3(0, 1, 0)), Mat4::translate(v3(-eye.x, -eye.y, -eye.z)));
    Mat4 proj = Mat4::perspective(70.f, (float)imgW / imgH, 0.08f, 2000.f);
    rasterMesh(fb, all, view, proj, skyBot, 200, 700);
    applyFog(fb, skyBot, 110.f, 460.f);
    writePNG(outPath, imgW, imgH, fb.col.data(), false);
    {
        std::string ppm = std::string(outPath);
        size_t dot = ppm.rfind('.');
        if (dot != std::string::npos) ppm = ppm.substr(0, dot) + ".ppm";
        FILE* pf = fopen(ppm.c_str(), "wb");
        if (pf) {
            fprintf(pf, "P6\n%d %d\n255\n", imgW, imgH);
            for (int i = 0; i < imgW * imgH; i++) {
                u32 c = fb.col[i];
                fputc((c >> 16) & 255, pf); fputc((c >> 8) & 255, pf); fputc(c & 255, pf);
            }
            fclose(pf);
            printf("wrote %s\n", ppm.c_str());
        }
    }
    printf("wrote %s  (cam %.0f %.0f %.0f)\n", outPath, eye.x, eye.y, eye.z);
    return 0;
}
