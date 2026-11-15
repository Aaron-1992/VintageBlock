#include "vb_common.h"

Mat4 Mat4::lookRot(const v3& fwd, const v3& upv) {
    v3 f = fwd.norm();
    v3 s = f.cross(upv).norm();
    v3 u = s.cross(f);
    Mat4 r;
    r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
    r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    return r;
}

void Frustum::fromMatrix(const Mat4& m) {
    // rows of the combined matrix
    auto set = [&](int i, float a, float b, float c, float d) {
        float len = sqrtf(a * a + b * b + c * c);
        if (len < 1e-8f) len = 1.f;
        p[i][0] = a / len; p[i][1] = b / len; p[i][2] = c / len; p[i][3] = d / len;
    };
    const float* M = m.m;
    // column major: M[col*4+row]
    set(0, M[0] + M[3], M[4] + M[7], M[8]  + M[11], M[12] + M[15]);   // left
    set(1, M[3] - M[0], M[7] - M[4], M[11] - M[8],  M[15] - M[12]);   // right
    set(2, M[3] + M[1], M[7] + M[5], M[11] + M[9],  M[15] + M[13]);   // bottom
    set(3, M[3] - M[1], M[7] - M[5], M[11] - M[9],  M[15] - M[13]);   // top
    set(4, M[3] + M[2], M[7] + M[6], M[11] + M[10], M[15] + M[14]);   // near
    set(5, M[3] - M[2], M[7] - M[6], M[11] - M[10], M[15] - M[14]);   // far
}

bool Frustum::boxVisible(float x0, float y0, float z0, float x1, float y1, float z1) const {
    for (int i = 0; i < 6; i++) {
        float* pl = (float*)p[i];
        float px = pl[0] > 0 ? x1 : x0;
        float py = pl[1] > 0 ? y1 : y0;
        float pz = pl[2] > 0 ? z1 : z0;
        if (pl[0] * px + pl[1] * py + pl[2] * pz + pl[3] < 0) return false;
    }
    return true;
}
