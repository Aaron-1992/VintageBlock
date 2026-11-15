// vb_main.cpp : Win32 window, input and the main loop
#include <windows.h>
#include <GL/gl.h>
#include <cstdio>
#include "vb_game.h"
#include "vb_render.h"

#define IDI_VB_ICON 101
static void setMouseCapture(bool on);

static Game* g_game = nullptr;
static Renderer* g_render = nullptr;
static int g_w = 1280, g_h = 720;
static bool g_running = true;
static bool g_mouseCaptured = false;
static HWND g_hwnd = nullptr;
static InputState g_input;
static float g_time = 0;
static int g_lastCullY = -999;
static double g_dtScale = 1.0;

// ------------------------------------------------------------------ window
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (LOWORD(lp) > 0 && HIWORD(lp) > 0) {
            g_w = LOWORD(lp);
            g_h = HIWORD(lp);
            if (g_render) renderSetWindow(g_render, g_w, g_h);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp < 256) {
            if (!g_input.key[wp]) g_input.keyDown[wp] = true;
            g_input.key[wp] = true;
        }
        return 0;
    case WM_KEYUP:
        if (wp < 256) g_input.key[wp] = false;
        return 0;
    case WM_CHAR:
        if (g_game && g_game->uiSeedEditing) {
            char c = (char)wp;
            size_t len = strlen(g_game->uiSeed);
            if (c >= '0' && c <= '9' && len < 18) {
                g_game->uiSeed[len] = c;
                g_game->uiSeed[len + 1] = 0;
            } else if (c == 8 && len > 0) {
                g_game->uiSeed[len - 1] = 0;
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        g_input.wheel += (float)GET_WHEEL_DELTA_WPARAM(wp) / 120.f;
        return 0;
    case WM_LBUTTONDOWN: g_input.mouse[0] = true; g_input.mouseDown[0] = true; return 0;
    case WM_LBUTTONUP: g_input.mouse[0] = false; return 0;
    case WM_RBUTTONDOWN: g_input.mouse[1] = true; g_input.mouseDown[1] = true; return 0;
    case WM_RBUTTONUP: g_input.mouse[1] = false; return 0;
    case WM_MBUTTONDOWN: g_input.mouse[2] = true; g_input.mouseDown[2] = true; return 0;
    case WM_MBUTTONUP: g_input.mouse[2] = false; return 0;
    case WM_SETFOCUS: return 0;
    case WM_KILLFOCUS:
        memset(g_input.key, 0, sizeof(g_input.key));
        memset(g_input.mouse, 0, sizeof(g_input.mouse));
        if (g_game && g_game->screen == 1 && !g_game->paused) {
            g_game->paused = true;
            setMouseCapture(false);
        }
        return 0;
    }
    if (msg == WM_SETCURSOR) {
        if (LOWORD(lp) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static void setMouseCapture(bool on) {
    if (on == g_mouseCaptured) return;
    g_mouseCaptured = on;
    ShowCursor(on ? FALSE : TRUE);
    if (on) {
        RECT r;
        GetClientRect(g_hwnd, &r);
        POINT c = { (r.right - r.left) / 2, (r.bottom - r.top) / 2 };
        ClientToScreen(g_hwnd, &c);
        SetCursorPos(c.x, c.y);
        SetCapture(g_hwnd);
    } else {
        ReleaseCapture();
    }
}

// ------------------------------------------------------------------ streaming
struct Cand { float d; int cx, cz; };

static void invalidateChunk(Game& g, Renderer* r, int cx, int cz) {
    Chunk* c = g.world.chunkAt(cx, cz);
    if (!c) return;
    renderFreeChunk(r, *c);
}

static void streamChunks(Game& g, Renderer* r) {
    World& w = g.world;
    int pcx = ifloordiv(floori(g.pos.x), CH);
    int pcz = ifloordiv(floori(g.pos.z), CH);
    int radius = (int)g.viewDistance + 1;
    static std::vector<Cand> cands;
    cands.clear();
    for (int dz = -radius; dz <= radius; dz++)
        for (int dx = -radius; dx <= radius; dx++) {
            int cx = pcx + dx, cz = pcz + dz;
            if (cx < 0 || cz < 0 || cx >= w.chunksX || cz >= w.chunksX) continue;
            float d = sqrtf((float)(dx * dx + dz * dz));
            if (d > radius) continue;
            cands.push_back({ d, cx, cz });
        }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });

    // unload far chunks
    float unloadR = g.viewDistance + 2.5f;
    for (int cz = 0; cz < w.chunksX; cz++)
        for (int cx = 0; cx < w.chunksX; cx++) {
            Chunk* c = w.chunkAt(cx, cz);
            if (!c) continue;
            float dcx = (float)(cx - pcx), dcz = (float)(cz - pcz);
            if (sqrtf(dcx * dcx + dcz * dcz) > unloadR) {
                renderFreeChunk(r, *c);
                w.destroyChunk(c);
            }
        }

    // regenerate meshes for edited chunks
    for (auto& cd : cands) {
        Chunk* c = w.chunkAt(cd.cx, cd.cz);
        if (c && c->modified) {
            c->modified = false;
            renderFreeChunk(r, *c);
            for (int i = 0; i < 4; i++) {
                int nx = cd.cx + (i == 0 ? 1 : (i == 1 ? -1 : 0));
                int nz = cd.cz + (i == 2 ? 1 : (i == 3 ? -1 : 0));
                Chunk* n = w.chunkAt(nx, nz);
                if (n) renderFreeChunk(r, *n);
            }
        }
    }

    // generate + mesh within a budget
    ULONGLONG start = GetTickCount64();
    const ULONGLONG budget = 9;
    int built = 0;
    for (auto& cd : cands) {
        if (GetTickCount64() - start > budget) break;
        Chunk* c = w.chunkAt(cd.cx, cd.cz);
        if (!c) {
            w.getOrCreateChunk(cd.cx, cd.cz);
            continue;
        }
        if (!c->generated) continue;
        float distBlocks = cd.d * CH;
        int lod = 0;
        if (distBlocks > g.viewDistance * CH * 0.62f) lod = 1;
        if (distBlocks > g.viewDistance * CH * 0.82f) lod = 2;
        if (distBlocks > g.viewDistance * CH) lod = 3;
        if (!c->meshes[lod]) {
            renderBuildChunk(r, g, *c, lod);
            built++;
            if (built >= 3) break;
        }
    }
    // drop meshes for LODs we no longer need (keeps GPU memory bounded)
    for (auto& cd : cands) {
        Chunk* c = w.chunkAt(cd.cx, cd.cz);
        if (!c) continue;
        float distBlocks = cd.d * CH;
        int lod = 0;
        if (distBlocks > g.viewDistance * CH * 0.62f) lod = 1;
        if (distBlocks > g.viewDistance * CH * 0.82f) lod = 2;
        if (distBlocks > g.viewDistance * CH) lod = 3;
        for (int i = 0; i < 4; i++)
            if (i != lod && c->meshes[i]) { renderFreeChunkLod(r, *c, i); break; }
    }
    renderFlushTrash(r);
}

// ------------------------------------------------------------------ input
static void updateLook(Game& g) {
    if (!g_mouseCaptured) return;
    POINT p;
    GetCursorPos(&p);
    RECT r;
    GetClientRect(g_hwnd, &r);
    POINT c = { (r.right - r.left) / 2, (r.bottom - r.top) / 2 };
    ClientToScreen(g_hwnd, &c);
    float dx = (float)(p.x - c.x), dy = (float)(p.y - c.y);
    if (dx != 0 || dy != 0) {
        float sens = 0.0022f;
        g.yaw += dx * sens;
        g.pitch -= dy * sens;
        g.pitch = clampf(g.pitch, -1.55f, 1.55f);
        SetCursorPos(c.x, c.y);
    }
}

static void handleActions(Game& g) {
    if (g.screen == 0) {
        // menu : handled by clicks
        if (g.uiSeedEditing) {
            if (g_input.keyDown[0x0D] || g_input.keyDown[0x1B]) g.uiSeedEditing = false;
        }
        return;
    }
    if (g_input.keyDown[0x1B]) {   // ESC
        if (g.invOpen) g.invOpen = false;
        else if (g.craftOpen) g.craftOpen = false;
        else g.paused = !g.paused;
        setMouseCapture(!g.paused && !g.invOpen && !g.craftOpen);
    }
    if (g_input.keyDown['E']) {
        g.invOpen = !g.invOpen;
        g.craftOpen = false;
        setMouseCapture(!g.invOpen && !g.paused);
    }
    if (g_input.keyDown['C'] && !g_input.key[0x11]) {
        g.craftOpen = !g.craftOpen;
        g.invOpen = false;
        g.craftScroll = 0;
        setMouseCapture(!g.craftOpen && !g.paused);
    }
    if (g_input.keyDown[VK_F3]) g.showDebug = !g.showDebug;
    if (g_input.keyDown[VK_F1]) g.viewDistance = (g.viewDistance > 10.f) ? 6.f : g.viewDistance + 2.f;
    if (g_input.keyDown['F'] && g.mode == GM_CREATIVE) g.flying = !g.flying;
    if (g_input.keyDown[VK_F5]) {
        if (g.saveGame()) g.notify("WORLD SAVED");
        else g.notify("SAVE FAILED");
    }
    if (g.craftOpen && g_input.wheel != 0) {
        g.craftScroll += (g_input.wheel > 0 ? -1 : 1);
        if (g.craftScroll < 0) g.craftScroll = 0;
    }
}

// ------------------------------------------------------------------ main
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = nullptr;
    wc.hIcon = LoadIconA(hInst, MAKEINTRESOURCEA(IDI_VB_ICON));
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "VintageBlockWindow";
    wc.style = CS_OWNDC;
    if (!RegisterClassA(&wc)) return 1;

    DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    RECT rect = { 0, 0, g_w, g_h };
    AdjustWindowRect(&rect, style, FALSE);
    char title[128];
    snprintf(title, sizeof(title), "VintageBlock %s %s  -  a Vintage Story inspired sandbox", VB_VERSION, VB_VERSION_TAG);
    HWND hwnd = CreateWindowA("VintageBlockWindow", title,
                              style, CW_USEDEFAULT, CW_USEDEFAULT,
                              rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    g_hwnd = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);

    HDC hdc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf) {
        MessageBoxA(hwnd, "No OpenGL pixel format available.", "VintageBlock", MB_ICONERROR);
        return 1;
    }
    SetPixelFormat(hdc, pf, &pfd);
    HGLRC glrc = wglCreateContext(hdc);
    if (!glrc) {
        MessageBoxA(hwnd, "Could not create an OpenGL context.", "VintageBlock", MB_ICONERROR);
        return 1;
    }
    wglMakeCurrent(hdc, glrc);

    initBlocks();
    initItems();

    g_game = new Game();
    memset(g_game->inv, 0, sizeof(g_game->inv));
    g_game->screen = 0;
    g_game->viewDistance = 7.f;

    g_render = renderCreate();
    renderSetWindow(g_render, g_w, g_h);
    renderInitGPU(g_render);

    LARGE_INTEGER freq, prev;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    MSG msg;
    while (g_running) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!g_running) break;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - prev.QuadPart) / (double)freq.QuadPart);
        prev = now;
        if (dt > 0.1f) dt = 0.1f;
        if (dt < 0.0005f) dt = 0.0005f;
        g_time += dt;
        g_input.dt = dt;
        g_game->fps = 1.f / dt;

        updateLook(*g_game);

        // ---- UI clicks
        POINT mp;
        GetCursorPos(&mp);
        ScreenToClient(hwnd, &mp);
        float mxf = (float)mp.x, myf = (float)mp.y;
        if (!g_mouseCaptured && (g_input.mouseDown[0] || g_input.mouseDown[1])) {
            int act = uiHitTest(*g_game, g_w, g_h, mxf, myf);
            if (act >= 0) {
                int action = act / 1000, arg = act % 1000;
                switch (action) {
                case 101: g_game->uiSize = arg; break;
                case 102: g_game->uiMode = arg; break;
                case 103: g_game->uiSeedEditing = !g_game->uiSeedEditing; break;
                case 100: {
                    u64 seed = (u64)atoll(g_game->uiSeed);
                    if (seed == 0) seed = (u64)GetTickCount64();
                    g_game->startWorld(WORLD_SIZES[g_game->uiSize], seed, g_game->uiMode);
                    g_game->screen = 1;
                    g_game->paused = false;
                    g_game->invOpen = false;
                    g_game->craftOpen = false;
                    g_game->uiSeedEditing = false;
                    g_lastCullY = (int)g_game->pos.y;
                    setMouseCapture(true);
                    break;
                }
                case 110: g_game->paused = false; setMouseCapture(true); break;
                case 111:
                    g_game->saveGame();
                    g_running = false;
                    break;
                case 104: {
                    if (g_game->loadGame()) {
                        g_game->screen = 1;
                        g_game->paused = false;
                        g_game->invOpen = false;
                        g_game->craftOpen = false;
                        g_game->uiSeedEditing = false;
                        g_lastCullY = (int)g_game->pos.y;
                        setMouseCapture(true);
                    } else {
                        g_game->notify("SAVE COULD NOT BE LOADED");
                    }
                    break;
                }
                case 120: {
                    int i = arg;
                    if (i < 10) g_game->sel = i;
                    else if (i < 30) {
                        // swap inventory slot with the selected hotbar slot
                        Slot tmp = g_game->inv[i];
                        g_game->inv[i] = g_game->inv[g_game->sel];
                        g_game->inv[g_game->sel] = tmp;
                    }
                    break;
                }
                case 130: g_game->craft(arg); break;
                case 150: g_game->craftOpen = false; g_game->invOpen = false; setMouseCapture(true); break;
                }
            }
        }

        handleActions(*g_game);

        if (g_game->screen == 1) {
            if (!g_game->paused && !g_game->invOpen && !g_game->craftOpen) {
                streamChunks(*g_game, g_render);
                if (abs((int)g_game->pos.y - g_lastCullY) > 22) {
                    g_lastCullY = (int)g_game->pos.y;
                    for (int i = 0; i < g_game->world.chunksX; i++)
                        for (int j = 0; j < g_game->world.chunksX; j++) {
                            Chunk* c = g_game->world.chunkAt(i, j);
                            if (c) renderFreeChunk(g_render, *c);
                        }
                }
                static float lastFarX = -9999, lastFarZ = -9999;
                if (fabsf(g_game->pos.x - lastFarX) > 24.f || fabsf(g_game->pos.z - lastFarZ) > 24.f) {
                    lastFarX = g_game->pos.x;
                    lastFarZ = g_game->pos.z;
                    renderRebuildFar(g_render, *g_game);
                }
                InputState gs = g_input;
                g_game->update(dt, gs);
            } else {
                g_game->updateEnvironment(dt);
                g_game->updateEntities(dt);
                if (g_game->screen == 1) g_game->hit = g_game->raycast(6.f);
            }
            renderScene(g_render, *g_game, g_time);
        } else {
            glViewport(0, 0, g_w, g_h);
            glClearColor(0.05f, 0.07f, 0.05f, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        renderUI(g_render, *g_game, mxf, myf, g_w, g_h);
        SwapBuffers(hdc);
        g_input.endFrame();
    }

    if (g_game && g_game->screen == 1) g_game->saveGame();
    if (g_game) g_game->world.shutdown();
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(glrc);
    ReleaseDC(hwnd, hdc);
    return 0;
}
