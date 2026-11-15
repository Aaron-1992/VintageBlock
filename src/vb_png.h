// vb_png.h : tiny dependency free PNG writer (stored deflate blocks)
#pragma once
#include "vb_common.h"
#include <cstdio>

static u32 crcTable[256];
static bool crcInit = false;
static void makeCrc() {
    if (crcInit) return;
    for (u32 n = 0; n < 256; n++) {
        u32 c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
    crcInit = true;
}
static u32 crc32buf(const u8* buf, size_t n, u32 crc = 0xFFFFFFFFu) {
    makeCrc();
    for (size_t i = 0; i < n; i++) crc = crcTable[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}
static void putU32(std::vector<u8>& v, u32 x) {
    v.push_back((u8)(x >> 24)); v.push_back((u8)(x >> 16)); v.push_back((u8)(x >> 8)); v.push_back((u8)x);
}
static void pngChunk(std::vector<u8>& out, const char* type, const std::vector<u8>& data) {
    putU32(out, (u32)data.size());
    size_t start = out.size();
    out.push_back((u8)type[0]); out.push_back((u8)type[1]); out.push_back((u8)type[2]); out.push_back((u8)type[3]);
    out.insert(out.end(), data.begin(), data.end());
    u32 c = crc32buf(&out[start], out.size() - start) ^ 0xFFFFFFFFu;
    putU32(out, c);
}
// rgba: 0xAARRGGBB packed
static bool writePNG(const char* path, int w, int h, const u32* rgba, bool flipY = true) {
    std::vector<u8> raw;
    raw.reserve((size_t)(w * 3 + 1) * h);
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        int sy = flipY ? (h - 1 - y) : y;
        for (int x = 0; x < w; x++) {
            u32 c = rgba[(size_t)sy * w + x];
            raw.push_back((u8)(c >> 16)); raw.push_back((u8)(c >> 8)); raw.push_back((u8)c);
        }
    }
    // zlib with stored blocks
    std::vector<u8> z;
    z.push_back(0x78); z.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = raw.size() - pos;
        if (n > 65535) n = 65535;
        bool last = (pos + n >= raw.size());
        z.push_back(last ? 1 : 0);
        z.push_back((u8)(n & 255)); z.push_back((u8)(n >> 8));
        z.push_back((u8)(~n & 255)); z.push_back((u8)((~n >> 8) & 255));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    u32 a = 1, b = 0;
    for (size_t i = 0; i < raw.size(); i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    putU32(z, (b << 16) | a);

    std::vector<u8> out;
    const u8 sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    out.insert(out.end(), sig, sig + 8);
    std::vector<u8> ihdr;
    putU32(ihdr, (u32)w); putU32(ihdr, (u32)h);
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    pngChunk(out, "IHDR", ihdr);
    pngChunk(out, "IDAT", z);
    pngChunk(out, "IEND", std::vector<u8>());
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    return true;
}
