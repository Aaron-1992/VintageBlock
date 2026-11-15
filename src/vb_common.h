// VintageBlock - a Vintage Story inspired voxel sandbox
// vb_common.h : math, hashing, noise, small utilities (platform independent)
#pragma once

#define VB_VERSION "0.1.0"
#define VB_VERSION_TAG "indev"
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>
#include <memory>
#include <functional>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;

static const float PI = 3.14159265358979f;
static const float TAU = 6.28318530717958f;

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float saturate(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float smoothstepf(float e0, float e1, float x) {
    float t = saturate((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t);
}
inline int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }
inline float signf(float v) { return v < 0 ? -1.f : 1.f; }
inline int floori(float v) { return (int)floorf(v); }
inline int ifloordiv(int a, int b) { int q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }
inline int imod(int a, int b) { int r = a % b; if (r < 0) r += b; return r; }

// ------------------------------------------------------------------ vectors
struct v3 {
    float x, y, z;
    v3() : x(0), y(0), z(0) {}
    v3(float a, float b, float c) : x(a), y(b), z(c) {}
    explicit v3(float a) : x(a), y(a), z(a) {}
    v3 operator+(const v3& o) const { return v3(x + o.x, y + o.y, z + o.z); }
    v3 operator-(const v3& o) const { return v3(x - o.x, y - o.y, z - o.z); }
    v3 operator*(float s) const { return v3(x * s, y * s, z * s); }
    v3 operator*(const v3& o) const { return v3(x * o.x, y * o.y, z * o.z); }
    v3 operator/(float s) const { return v3(x / s, y / s, z / s); }
    v3& operator+=(const v3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    v3& operator-=(const v3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    v3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float dot(const v3& o) const { return x * o.x + y * o.y + z * o.z; }
    v3 cross(const v3& o) const { return v3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x); }
    float len() const { return sqrtf(x * x + y * y + z * z); }
    float len2() const { return x * x + y * y + z * z; }
    v3 norm() const { float l = len(); return l > 1e-6f ? v3(x / l, y / l, z / l) : v3(0, 0, 0); }
    float operator[](int i) const { return (&x)[i]; }
    float& operator[](int i) { return (&x)[i]; }
};
struct v2 { float x, y; v2() :x(0), y(0) {} v2(float a, float b) :x(a), y(b) {} };

// 4x4 column-major matrix (OpenGL layout)
struct Mat4 {
    float m[16];
    Mat4() { identity(); }
    void identity() { memset(m, 0, sizeof(m)); m[0] = m[5] = m[10] = m[15] = 1.f; }
    static Mat4 mul(const Mat4& a, const Mat4& b) {
        Mat4 r; memset(r.m, 0, sizeof(r.m));
        for (int c = 0; c < 4; c++)
            for (int rr = 0; rr < 4; rr++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
                r.m[c * 4 + rr] = s;
            }
        return r;
    }
    static Mat4 perspective(float fovyDeg, float aspect, float zn, float zf) {
        Mat4 r; memset(r.m, 0, sizeof(r.m));
        float f = 1.f / tanf(fovyDeg * PI / 360.f);
        r.m[0] = f / aspect; r.m[5] = f;
        r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1.f;
        r.m[14] = (2 * zf * zn) / (zn - zf);
        return r;
    }
    static Mat4 ortho(float l, float r_, float b, float t, float n, float f) {
        Mat4 r;
        r.m[0] = 2 / (r_ - l); r.m[5] = 2 / (t - b); r.m[10] = -2 / (f - n);
        r.m[12] = -(r_ + l) / (r_ - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -(f + n) / (f - n);
        return r;
    }
    static Mat4 lookRot(const v3& fwd, const v3& up);
    static Mat4 translate(const v3& t) { Mat4 r; r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
};

struct Frustum {
    float p[6][4];
    void fromMatrix(const Mat4& m);      // m = proj * view
    bool boxVisible(float x0, float y0, float z0, float x1, float y1, float z1) const;
};

// ------------------------------------------------------------------ random
struct Rng {
    u64 s;
    explicit Rng(u64 seed = 0x243F6A8885A308D3ull) : s(seed ? seed : 1) {}
    inline u32 next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (u32)(s >> 32); }
    inline float f() { return (next() >> 8) * (1.f / 16777216.f); }
    inline float range(float a, float b) { return a + (b - a) * f(); }
    inline int irange(int a, int b) { return a + (int)(next() % (u32)(b - a + 1)); }
    inline bool chance(float p) { return f() < p; }
};

inline u32 hash32(u32 x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
inline u32 hash2i(int x, int y, u32 seed) {
    u32 h = (u32)x * 0x8da6b343u ^ (u32)y * 0xd8163841u ^ seed * 0xcb1ab31fu;
    return hash32(h);
}
inline u32 hash3i(int x, int y, int z, u32 seed) {
    u32 h = (u32)x * 0x8da6b343u ^ (u32)y * 0xd8163841u ^ (u32)z * 0x2f5c1b7du ^ seed * 0xcb1ab31fu;
    return hash32(h);
}
inline float hash2f(int x, int y, u32 seed) { return (hash2i(x, y, seed) >> 8) * (1.f / 16777216.f); }

// ------------------------------------------------------------------ noise
// Gradient (Perlin style) noise, plus fbm / ridged / worley helpers.
struct Noise {
    u8 perm[512];
    u32 seed;
    void init(u32 s) {
        seed = s;
        u8 base[256];
        for (int i = 0; i < 256; i++) base[i] = (u8)i;
        Rng r(s * 2654435761u + 12345u);
        for (int i = 255; i > 0; i--) { int j = r.irange(0, i); u8 t = base[i]; base[i] = base[j]; base[j] = t; }
        for (int i = 0; i < 512; i++) perm[i] = base[i & 255];
    }
    static inline float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static inline float grad2(u32 h, float x, float y) {
        switch (h & 7) {
            case 0: return  x + y; case 1: return  x - y; case 2: return -x + y; case 3: return -x - y;
            case 4: return  x;     case 5: return -x;     case 6: return  y;     default: return -y;
        }
    }
    static inline float grad3(u32 h, float x, float y, float z) {
        switch (h & 15) {
            case 0: return  x + y; case 1: return -x + y; case 2: return x - y;  case 3: return -x - y;
            case 4: return  x + z; case 5: return -x + z; case 6: return x - z;  case 7: return -x - z;
            case 8: return  y + z; case 9: return -y + z; case 10: return y - z; case 11: return -y - z;
            case 12: return x + y; case 13: return -y + z; case 14: return -x + y; default: return -y - z;
        }
    }
    float n2(float x, float y) const {
        int X = floori(x), Y = floori(y);
        float fx = x - X, fy = y - Y;
        int xi = X & 255, yi = Y & 255;
        float u = fade(fx), v = fade(fy);
        u32 a = perm[xi] + yi, b = perm[xi + 1] + yi;
        float n00 = grad2(perm[a], fx, fy);
        float n10 = grad2(perm[b], fx - 1, fy);
        float n01 = grad2(perm[a + 1], fx, fy - 1);
        float n11 = grad2(perm[b + 1], fx - 1, fy - 1);
        return lerpf(lerpf(n00, n10, u), lerpf(n01, n11, u), v) * 0.7f;
    }
    float n3(float x, float y, float z) const {
        int X = floori(x), Y = floori(y), Z = floori(z);
        float fx = x - X, fy = y - Y, fz = z - Z;
        int xi = X & 255, yi = Y & 255, zi = Z & 255;
        float u = fade(fx), v = fade(fy), w = fade(fz);
        u32 a = perm[xi] + yi, aa = perm[a] + zi, ab = perm[a + 1] + zi;
        u32 b = perm[xi + 1] + yi, ba = perm[b] + zi, bb = perm[b + 1] + zi;
        float n000 = grad3(perm[aa], fx, fy, fz);
        float n100 = grad3(perm[ba], fx - 1, fy, fz);
        float n010 = grad3(perm[ab], fx, fy - 1, fz);
        float n110 = grad3(perm[bb], fx - 1, fy - 1, fz);
        float n001 = grad3(perm[aa + 1], fx, fy, fz - 1);
        float n101 = grad3(perm[ba + 1], fx - 1, fy, fz - 1);
        float n011 = grad3(perm[ab + 1], fx, fy - 1, fz - 1);
        float n111 = grad3(perm[bb + 1], fx - 1, fy - 1, fz - 1);
        return lerpf(lerpf(lerpf(n000, n100, u), lerpf(n010, n110, u), v),
                     lerpf(lerpf(n001, n101, u), lerpf(n011, n111, u), v), w) * 0.75f;
    }
    float fbm2(float x, float y, int oct, float lac = 2.f, float gain = 0.5f) const {
        float a = 1.f, s = 0.f, norm = 0.f;
        for (int i = 0; i < oct; i++) {
            s += a * n2(x, y); norm += a;
            x *= lac; y *= lac; a *= gain;
        }
        return s / (norm > 0 ? norm : 1);
    }
    float fbm3(float x, float y, float z, int oct, float lac = 2.f, float gain = 0.5f) const {
        float a = 1.f, s = 0.f, norm = 0.f;
        for (int i = 0; i < oct; i++) {
            s += a * n3(x, y, z); norm += a;
            x *= lac; y *= lac; z *= lac; a *= gain;
        }
        return s / (norm > 0 ? norm : 1);
    }
    float ridged2(float x, float y, int oct) const {
        float a = 1.f, s = 0.f, norm = 0.f, w = 1.f;
        for (int i = 0; i < oct; i++) {
            float n = 1.f - fabsf(n2(x, y));
            n *= n; n *= w; w = clampf(n * 2.f, 0.f, 1.f);
            s += n * a; norm += a;
            x *= 2.03f; y *= 2.03f; a *= 0.5f;
        }
        return s * 2.f - 1.f;
    }
    // cellular / worley : returns f1 distance (0..~1) and cell id
    float worley2(float x, float y, u32 sd, u32* cellOut = nullptr) const {
        int X = floori(x), Y = floori(y);
        float best = 1e9f; u32 bestId = 0;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int cx = X + dx, cy = Y + dy;
                u32 h = hash2i(cx, cy, sd);
                float px = cx + (h & 1023) * (1.f / 1023.f);
                float py = cy + ((h >> 10) & 1023) * (1.f / 1023.f);
                float d = (px - x) * (px - x) + (py - y) * (py - y);
                if (d < best) { best = d; bestId = h; }
            }
        if (cellOut) *cellOut = bestId;
        return sqrtf(best);
    }
    float worley3(float x, float y, float z, u32 sd) const {
        int X = floori(x), Y = floori(y), Z = floori(z);
        float best = 1e9f;
        for (int dz = -1; dz <= 1; dz++)
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    u32 h = hash3i(X + dx, Y + dy, Z + dz, sd);
                    float px = X + dx + (h & 255) * (1.f / 255.f);
                    float py = Y + dy + ((h >> 8) & 255) * (1.f / 255.f);
                    float pz = Z + dz + ((h >> 16) & 255) * (1.f / 255.f);
                    float d = (px - x) * (px - x) + (py - y) * (py - y) + (pz - z) * (pz - z);
                    if (d < best) best = d;
                }
        return sqrtf(best);
    }
};

// small helper: pack/unpack colors
inline u32 rgb(int r, int g, int b) { return 0xFF000000u | ((u32)clampi(r, 0, 255) << 16) | ((u32)clampi(g, 0, 255) << 8) | (u32)clampi(b, 0, 255); }
inline u32 shadeColor(u32 c, float f) {
    int r = (int)((c >> 16 & 255) * f), g = (int)((c >> 8 & 255) * f), b = (int)((c & 255) * f);
    return rgb(r, g, b);
}
inline u32 lerpColor(u32 a, u32 b, float t) {
    int ar = a >> 16 & 255, ag = a >> 8 & 255, ab = a & 255;
    int br = b >> 16 & 255, bg = b >> 8 & 255, bb = b & 255;
    return rgb((int)lerpf((float)ar, (float)br, t), (int)lerpf((float)ag, (float)bg, t), (int)lerpf((float)ab, (float)bb, t));
}
