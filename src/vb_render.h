// vb_render.h : OpenGL rendering interface (Windows / WGL)
#pragma once
#include "vb_game.h"

struct GpuChunk;
struct Renderer;

Renderer* renderCreate();
void renderDestroy(Renderer* r);
bool renderInitGPU(Renderer* r);
void renderSetWindow(Renderer* r, int w, int h);

// cpu side mesh lifetime
void renderBuildChunk(Renderer* r, Game& g, Chunk& c, int lod);
void renderFreeChunk(Renderer* r, Chunk& c);
void renderFreeChunkLod(Renderer* r, Chunk& c, int lod);
void renderRebuildFar(Renderer* r, Game& g);

void renderScene(Renderer* r, Game& g, float timeSec);
void renderUI(Renderer* r, Game& g, float mx, float my, int screenW, int screenH);

// returns packed action or -1
int uiHitTest(Game& g, int screenW, int screenH, float mx, float my);
int renderStatsVerts(Renderer* r);
int renderStatsChunks(Renderer* r);
int renderStatsFarVerts(Renderer* r);
void renderFlushTrash(Renderer* r);
unsigned int renderAtlas(Renderer* r);
unsigned int renderFontTex(Renderer* r);
