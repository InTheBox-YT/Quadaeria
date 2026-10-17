#include "world/Mesher.h"
#include "core/Noise.h"
#include "world/Orient.h"
#include "world/PlantGeometry.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <mutex>

namespace qc {
namespace {
uint8_t gOcc[256], gCube[256];
void buildTabs() {
    static std::once_flag once;
    std::call_once(once, [] {
        for (int i = 0; i < 256; ++i) {
            Render r = def(uint8_t(i)).render;
            gOcc[i] = r == Render::Opaque;
            gCube[i] = r == Render::Opaque || r == Render::Leaves || r == Render::Glass || r == Render::Cutout;
        }
    });
}
inline bool occludes(uint8_t b) { return gOcc[b]; }
inline bool isCube(uint8_t b) { return gCube[b]; }
inline bool translucentCube(uint8_t) { return false; }

bool faceVisible(uint8_t a, uint8_t nb, bool fancyLeaves) {
    // ice counts as solid here
    if (occludes(nb) || nb == B_ICE) return false;
    const BlockDef& da = def(a);
    if (da.render == Render::Opaque) return true;
    if (da.render == Render::Leaves) return nb != a || fancyLeaves;
    return nb != a;
}

struct Box {
    const Region& r;
    int n[3];
    const uint8_t* owned = nullptr;
    std::vector<uint32_t> climate;
    void cacheClimate() {
        climate.resize(size_t(n[0] + 1) * (n[2] + 1));
        for (int z = 0; z <= n[2]; ++z)
            for (int x = 0; x <= n[0]; ++x) {
                uint32_t t = 128, h = 128;
                if (!r.climT.empty()) {
                    int tt = 0, hh = 0;
                    for (int dz = 0; dz < 2; ++dz)
                        for (int dx = 0; dx < 2; ++dx) {
                            int i = std::clamp(z + dz, 0, r.sz - 1) * r.sx + std::clamp(x + dx, 0, r.sx - 1);
                            tt += r.climT[size_t(i)];
                            hh += r.climH[size_t(i)];
                        }
                    t = uint32_t(tt / 4);
                    h = uint32_t(hh / 4);
                }
                climate[size_t(z) * (n[0] + 1) + x] = ((t >> 2) << 2) | ((h >> 2) << 8);
            }
    }
    int at(int x, int y, int z) const { return ((y + 1) * r.sz + (z + 1)) * r.sx + (x + 1); }
    uint8_t b(int x, int y, int z) const { return r.blocks[size_t(at(x, y, z))]; }
    uint8_t l(int x, int y, int z) const { return r.light[size_t(at(x, y, z))]; }
    uint8_t m(int x, int y, int z) const { return r.meta.empty() ? 0 : r.meta[size_t(at(x, y, z))]; }
    bool owns(int x, int y, int z) const {
        return !owned || (x >= 0 && y >= 0 && z >= 0 && x < n[0] && y < n[1] && z < n[2] && owned[size_t((y * n[2] + z) * n[0] + x)] != 0);
    }
    uint16_t color(int x, int y, int z, int face) const {
        if (r.radiance.empty()) return 0;
        int sum[3] = {}, count = 0;
        for (int dz = -1; dz <= 0; ++dz)
            for (int dy = -1; dy <= 0; ++dy)
                for (int dx = -1; dx <= 0; ++dx) {
                    if (face < 6) {
                        int off = face < 2 ? dx : face < 4 ? dy : dz;
                        if (off != ((face & 1) ? -1 : 0)) continue;
                    }
                    int px = std::clamp(x + dx + 1, 0, r.sx - 1), py = std::clamp(y + dy + 1, 0, r.sy - 1),
                        pz = std::clamp(z + dz + 1, 0, r.sz - 1);
                    int i = (py * r.sz + pz) * r.sx + px;
                    if (lightCost(r.blocks[i]) >= 99 && !def(r.blocks[i]).emit) continue;
                    uint16_t c = r.radiance[i];
                    for (int k = 0; k < 3; ++k) sum[k] += (c >> (k * 4)) & 15;
                    ++count;
                }
        uint16_t c = 0;
        if (count)
            for (int k = 0; k < 3; ++k) c |= ((sum[k] + count / 2) / count) << (k * 4);
        return c;
    }
    uint32_t clim(int px, int pz, uint32_t cls) const {
        if (!cls) return 0;
        if (px >= 0 && px <= n[0] && pz >= 0 && pz <= n[2]) return cls | climate[size_t(pz) * (n[0] + 1) + px];
        uint32_t t = 128, h = 128;
        if (!r.climT.empty()) {
            int tt = 0, hh = 0;
            for (int dz = 0; dz < 2; ++dz)
                for (int dx = 0; dx < 2; ++dx) {
                    int ix = std::clamp(px + dx, 0, r.sx - 1), iz = std::clamp(pz + dz, 0, r.sz - 1);
                    tt += r.climT[size_t(iz * r.sx + ix)];
                    hh += r.climH[size_t(iz * r.sx + ix)];
                }
            t = uint32_t(tt / 4);
            h = uint32_t(hh / 4);
        }
        return cls | ((t >> 2) << 2) | ((h >> 2) << 8);
    }
};

const int AX_D[6] = {0, 0, 1, 1, 2, 2};
const int AX_U[6] = {2, 2, 0, 0, 0, 0};
const int AX_V[6] = {1, 1, 2, 2, 1, 1};

uint8_t faceTex(uint8_t b, int f) {
    const BlockDef& d = def(b);
    return f == 2 ? d.texTop : f == 3 ? d.texBottom : d.texSide;
}

void turnedUV(const BlockDef& d, int f, int orientIdx, int phase, const int corner[3], float& u, float& v, int& tex) {
    const Orient& O = orient(orientIdx);
    const int N[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    int ln[3], lc[3];
    for (int i = 0; i < 3; ++i) {
        ln[i] = O.m[0][i] * N[f][0] + O.m[1][i] * N[f][1] + O.m[2][i] * N[f][2];
        int w = (2 * corner[0] - 1) * O.m[0][i] + (2 * corner[1] - 1) * O.m[1][i] + (2 * corner[2] - 1) * O.m[2][i];
        lc[i] = (w + 1) / 2;
    }
    int lf = ln[0] > 0 ? 0 : ln[0] < 0 ? 1 : ln[1] > 0 ? 2 : ln[1] < 0 ? 3 : ln[2] > 0 ? 4 : 5;
    float x = float((phase & 1) + lc[0]), y = float(((phase >> 1) & 1) + lc[1]), z = float(((phase >> 2) & 1) + lc[2]);
    switch (lf) {
    case 0:
        u = -z;
        v = -y;
        break;
    case 1:
        u = z;
        v = -y;
        break;
    case 2:
        u = x;
        v = z;
        break;
    case 3:
        u = x;
        v = -z;
        break;
    case 4:
        u = x;
        v = -y;
        break;
    default:
        u = -x;
        v = -y;
        break;
    }
    u *= 0.5f;
    v *= 0.5f;
    if (d.sideTopAligned && lf != 2 && lf != 3) v = (1.0f - float(lc[1])) * 0.5f;
    tex = lf == 2 ? d.texTop : lf == 3 ? d.texBottom : d.texSide;
}

void meshCubes(const Box& bx, MeshOut& out, bool fancy) {
    static thread_local std::vector<uint64_t> mask, colors;
    for (int f = 0; f < 6; ++f) {
        const int d = AX_D[f], ua = AX_U[f], va = AX_V[f];
        const bool pos = (f & 1) == 0;
        const int nu = bx.n[ua], nv = bx.n[va];
        const int strides[3] = {1, bx.r.sx * bx.r.sz, bx.r.sx};
        const int du = strides[ua], dv = strides[va];
        mask.assign(size_t(nu) * nv, 0);
        colors.assign(size_t(nu) * nv, 0);
        int nrm[3] = {0, 0, 0};
        nrm[d] = pos ? 1 : -1;
        const int ds = strides[d], step = pos ? ds : -ds;
        const uint8_t* blk = bx.r.blocks.data();
        for (int s = 0; s < bx.n[d]; ++s) {
            bool any = false;
            for (int v = 0; v < nv; ++v) {
                int p0[3];
                p0[d] = s;
                p0[ua] = 0;
                p0[va] = v;
                const int row = bx.at(p0[0], p0[1], p0[2]);
                uint64_t* mrow = mask.data() + size_t(v) * nu;
                for (int u = 0; u < nu; ++u) {
                    const int idx = row + u * du;
                    const uint8_t a = blk[idx];
                    if (!gCube[a]) {
                        mrow[u] = 0;
                        continue;
                    }
                    const uint8_t nb = blk[idx + step];
                    if (gOcc[nb]) {
                        mrow[u] = 0;
                        continue;
                    }
                    int p[3];
                    p[d] = s;
                    p[ua] = u;
                    p[va] = v;
                    if (!bx.owns(p[0], p[1], p[2]) || !faceVisible(a, nb, fancy)) {
                        mrow[u] = 0;
                        continue;
                    }
                    int q[3] = {p[0] + nrm[0], p[1] + nrm[1], p[2] + nrm[2]};
                    const BlockDef& da = def(a);
                    static const bool kAllLeaf = std::getenv("QC_ALLLEAVES") != nullptr;
                    if (da.render == Render::Leaves && nb == a && !kAllLeaf) {
                        int q2 = q[d] + nrm[d];
                        if (q2 >= -1 && q2 <= bx.n[d]) {
                            int r2[3] = {q[0], q[1], q[2]};
                            r2[d] = q2;
                            uint8_t nb2 = bx.b(r2[0], r2[1], r2[2]);
                            if (nb2 == a || occludes(nb2)) {
                                mask[size_t(v * nu + u)] = 0;
                                continue;
                            }
                        }
                    }
                    uint32_t aoBits = 0, skyBits = 0, blkBits = 0;
                    int front = bx.at(q[0], q[1], q[2]);
                    uint8_t fl = bx.r.light[front];
                    for (int c = 0; c < 4; ++c) {
                        int cu = (c == 1 || c == 2) ? du : -du, cv = (c >= 2) ? dv : -dv;
                        int s1 = front + cu, s2 = front + cv, cc = front + cu + cv;
                        bool o1 = occludes(bx.r.blocks[s1]), o2 = occludes(bx.r.blocks[s2]), o3 = occludes(bx.r.blocks[cc]);
                        int ao = (o1 && o2) ? 0 : 3 - (int(o1) + int(o2) + int(o3));
                        int sk = fl >> 4, bl = fl & 15, cnt = 1;
                        if (!o1) {
                            uint8_t L = bx.r.light[s1];
                            sk += L >> 4;
                            bl += L & 15;
                            ++cnt;
                        }
                        if (!o2) {
                            uint8_t L = bx.r.light[s2];
                            sk += L >> 4;
                            bl += L & 15;
                            ++cnt;
                        }
                        if (!o3 && !(o1 && o2)) {
                            uint8_t L = bx.r.light[cc];
                            sk += L >> 4;
                            bl += L & 15;
                            ++cnt;
                        }
                        sk = (sk + cnt / 2) / cnt;
                        bl = (bl + cnt / 2) / cnt;
                        aoBits |= uint32_t(ao) << (c * 2);
                        skyBits |= uint32_t(sk) << (c * 4);
                        blkBits |= uint32_t(bl) << (c * 4);
                    }
                    uint64_t mode = (da.sideTopAligned && f != 2 && f != 3) ? UV_TOPALIGNED : UV_WORLD;
                    uint64_t sway = da.render == Render::Leaves ? 1 : 0;
                    uint64_t tr = translucentCube(a) ? 1 : 0;
                    uint64_t cut = (!tr && da.render != Render::Opaque) ? 1 : 0;
                    uint8_t meta = bx.m(p[0], p[1], p[2]);
                    if (meta && (orientable(a) || a == B_TNT)) {
                        mask[size_t(v * nu + u)] = 0;
                        int wx = bx.r.x0 + 1 + p[0], wy = bx.r.y0 + 1 + p[1], wz = bx.r.z0 + 1 + p[2];
                        int o = a == B_TNT ? 0 : metaOrient(meta), ph = metaPhase(meta, wx, wy, wz);
                        int ao[4], sk[4], bl[4];
                        for (int c = 0; c < 4; ++c) {
                            ao[c] = int((aoBits >> (c * 2)) & 3);
                            sk[c] = int((skyBits >> (c * 4)) & 15);
                            bl[c] = int((blkBits >> (c * 4)) & 15);
                        }
                        const int cuv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                        bool ccw = (d == 2) == pos;
                        int order[4] = {0, 1, 2, 3};
                        if (!ccw) {
                            order[1] = 3;
                            order[3] = 1;
                        }
                        if (ao[order[0]] + ao[order[2]] < ao[order[1]] + ao[order[3]]) {
                            int t = order[0];
                            order[0] = order[1];
                            order[1] = order[2];
                            order[2] = order[3];
                            order[3] = t;
                        }
                        std::vector<uint32_t>& dst = tr ? out.trans : cut ? out.cutout : out.solid;
                        for (int k = 0; k < 4; ++k) {
                            int c = order[k];
                            int corner[3];
                            corner[d] = pos ? 1 : 0;
                            corner[ua] = cuv[c][0];
                            corner[va] = cuv[c][1];
                            float uu, vv;
                            int tex;
                            turnedUV(da, f, o, ph, corner, uu, vv, tex);
                            uint32_t uw = uint32_t(std::clamp(int((uu + 8.0f) * 4096.0f + 0.5f), 0, 65535)) |
                                          (uint32_t(std::clamp(int((vv + 8.0f) * 4096.0f + 0.5f), 0, 65535)) << 16);
                            int P[3] = {p[0] + corner[0], p[1] + corner[1], p[2] + corner[2]};
                            packVertex(dst, P[0], P[1], P[2], f, cuv[c][0], cuv[c][1], uint32_t(mode), sway, 0, tex, sk[c], bl[c], ao[c],
                                       bx.clim(P[0], P[2], da.tint), 0, true, uw);
                        }
                        continue;
                    }
                    uint8_t ftex = faceTex(a, f);
                    if (da.sideTopAligned && f != 2 && f != 3 && bx.b(p[0], p[1] + 1, p[2]) == a) {
                        ftex = da.texBottom;
                        mode = UV_WORLD;
                    }
                    uint64_t key = uint64_t(ftex) | (mode << 8) | (sway << 10) | (tr << 11) | (uint64_t(aoBits) << 12) |
                                   (uint64_t(skyBits) << 20) | (uint64_t(blkBits) << 36) | (uint64_t(da.tint) << 52) | (cut << 54) |
                                   (1ull << 63);
                    mask[size_t(v * nu + u)] = key;
                    uint64_t rgb = 0;
                    if (blkBits)
                        for (int c = 0; c < 4; ++c) {
                            int P[3] = {p[0], p[1], p[2]};
                            P[d] += pos ? 1 : 0;
                            P[ua] += (c == 1 || c == 2) ? 1 : 0;
                            P[va] += (c >= 2) ? 1 : 0;
                            rgb |= uint64_t(bx.color(P[0], P[1], P[2], f)) << (12 * c);
                        }
                    colors[size_t(v * nu + u)] = rgb;
                    any = true;
                }
            }
            if (!any) continue;
            for (int v = 0; v < nv; ++v)
                for (int u = 0; u < nu;) {
                    uint64_t key = mask[size_t(v * nu + u)];
                    if (!key) {
                        ++u;
                        continue;
                    }
                    uint64_t rgb = colors[size_t(v * nu + u)];
                    int w = 1;
                    while (u + w < nu && mask[size_t(v * nu + u + w)] == key && colors[size_t(v * nu + u + w)] == rgb) ++w;
                    int h = 1;
                    bool topAligned = ((key >> 8) & 3) == UV_TOPALIGNED;
                    if (!topAligned) {
                        for (; v + h < nv; ++h) {
                            bool ok = true;
                            for (int k = 0; k < w; ++k)
                                if (mask[size_t((v + h) * nu + u + k)] != key || colors[size_t((v + h) * nu + u + k)] != rgb) {
                                    ok = false;
                                    break;
                                }
                            if (!ok) break;
                        }
                    }
                    for (int hh = 0; hh < h; ++hh)
                        for (int k = 0; k < w; ++k) mask[size_t((v + hh) * nu + u + k)] = 0;
                    int tex = int(key & 255);
                    uint32_t mode = uint32_t((key >> 8) & 3);
                    bool sway = (key >> 10) & 1;
                    bool tr = (key >> 11) & 1;
                    bool cut = (key >> 54) & 1;
                    uint32_t cls = uint32_t((key >> 52) & 3);
                    int ao[4], sk[4], bl[4];
                    for (int c = 0; c < 4; ++c) {
                        ao[c] = int((key >> (12 + c * 2)) & 3);
                        sk[c] = int((key >> (20 + c * 4)) & 15);
                        bl[c] = int((key >> (36 + c * 4)) & 15);
                    }
                    const int cuv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                    bool ccw = (d == 2) == pos;
                    int order[4] = {0, 1, 2, 3};
                    if (!ccw) {
                        order[1] = 3;
                        order[3] = 1;
                    }
                    if (ao[order[0]] + ao[order[2]] < ao[order[1]] + ao[order[3]]) {
                        int t = order[0];
                        order[0] = order[1];
                        order[1] = order[2];
                        order[2] = order[3];
                        order[3] = t;
                    }
                    std::vector<uint32_t>& dst = tr ? out.trans : cut ? out.cutout : out.solid;
                    for (int k = 0; k < 4; ++k) {
                        int c = order[k];
                        int P[3];
                        P[d] = s + (pos ? 1 : 0);
                        P[ua] = u + cuv[c][0] * w;
                        P[va] = v + cuv[c][1] * h;
                        packVertex(dst, P[0], P[1], P[2], f, cuv[c][0], cuv[c][1], mode, sway, 0, tex, sk[c], bl[c], ao[c],
                                   bx.clim(P[0], P[2], cls));
                    }
                    u += w;
                }
        }
    }
}

void shoreWash(const Box& bx, MeshOut& out, int x, int y, int z, uint8_t a) {
    if (!occludes(a) || a == B_PACKED_ICE || a == B_SNOW || a == B_SNOWY_GRASS) return;
    uint8_t above = bx.b(x, y + 1, z);
    if (def(above).collides || def(above).fluid) return;
    const int dx[] = {1, -1, 0, 0}, dz[] = {0, 0, 1, -1};
    int edges = 0;
    for (int k = 0; k < 4; ++k)
        if (bx.b(x + dx[k], y, z + dz[k]) == B_WATER && fluidSurfaceAmountOf(B_WATER, bx.m(x + dx[k], y, z + dz[k])) >= 220 &&
            !def(bx.b(x + dx[k], y + 1, z + dz[k])).collides && bx.b(x + dx[k], y + 1, z + dz[k]) != B_WATER)
            edges |= 1 << k;
    if (!edges) return;
    auto& dst = out.trans;
    uint8_t L = bx.l(x, y + 1, z);
    const int uv[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    uint32_t motion = 63u | (63u << 7) | (255u << 14) | (uint32_t(2 | (edges << 3)) << 22);
    for (auto& q : uv) {
        packVertex(dst, x + q[0], y + 1, z + q[1], 2, q[0], q[1], UV_FLUID, false, 0, T_WATER, L >> 4, L & 15, 3,
                   bx.clim(x + q[0], z + q[1], 3));
        dst[dst.size() - 2] = motion;
        dst.back() = 1u << 16;
    }
}

void meshSpecial(const Box& bx, MeshOut& out) {
    for (int y = 0; y < bx.n[1]; ++y)
        for (int z = 0; z < bx.n[2]; ++z)
            for (int x = 0; x < bx.n[0]; ++x) {
                if (!bx.owns(x, y, z)) continue;
                uint8_t a = bx.b(x, y, z);
                const BlockDef& da = def(a);
                shoreWash(bx, out, x, y, z, a);
                static const bool noPlants = std::getenv("QC_NOPLANTS") != nullptr;
                if (noPlants && (da.render == Render::Cross || da.render == Render::Vine || da.render == Render::Detail)) continue;
                if ((da.render == Render::Cross && a != B_TORCH && a != B_FIRE) || da.render == Render::Vine) {
                    uint8_t L = bx.l(x, y, z);
                    vec3 offset;
                    uint32_t climate = bx.clim(x, z, da.tint);
                    if (a == B_VINE) {
                        if (def(bx.b(x + 1, y, z)).collides)
                            offset.x = 0.125f;
                        else if (def(bx.b(x - 1, y, z)).collides)
                            offset.x = -0.125f;
                        else if (def(bx.b(x, y, z + 1)).collides)
                            offset.z = 0.125f;
                        else if (def(bx.b(x, y, z - 1)).collides)
                            offset.z = -0.125f;
                    }
                    for (const auto& face : plantGeometry(a))
                        for (int k = 0; k < 4; ++k) {
                            vec3 p = face.p[k] + offset + vec3(float(x), float(y), float(z));
                            int ix = int(std::floor(p.x)), iy = int(std::floor(p.y)), iz = int(std::floor(p.z));
                            bool sway = a != B_CRYSTAL && a != B_GLOWSHROOM && a != B_SUGAR_CANE &&
                                        (a == B_VINE || a == B_LONG_GRASS_TOP || face.p[k].y > 0.25f);
                            packVertex(out.detail, ix, iy, iz, face.face, 0, 0, UV_WORLD, sway, 0, a == B_CRYSTAL ? T_PACKED_ICE : face.tex,
                                       L >> 4, L & 15, 3, climate);
                            uint32_t frac = uint32_t(std::round((p.x - ix) * 16)) | (uint32_t(std::round((p.y - iy) * 16)) << 4) |
                                            (uint32_t(std::round((p.z - iz) * 16)) << 8);
                            uint32_t shade = 16;
                            if (face.tex == T_PLANT_GREEN) {
                                int wy = bx.r.y0 + 1 + y - (a == B_LONG_GRASS_TOP ? 1 : 0);
                                uint32_t h = hash3i(bx.r.x0 + 1 + x, wy, bx.r.z0 + 1 + z, 491);
                                float height = a == B_LONG_GRASS_TOP ? (1 + face.p[k].y) * .5f
                                               : a == B_LONG_GRASS   ? face.p[k].y * .5f
                                                                     : face.p[k].y;
                                shade = uint32_t(std::clamp(int(16 * (.67f + .36f * height + float(h % 17) / 100.0f) + .5f), 1, 31));
                            }
                            out.detail.back() |= (frac << 12) | (shade << 24);
                        }
                    continue;
                }
                if (da.render == Render::Detail || da.render == Render::Vine) {
                    uint8_t L = bx.l(x, y, z);
                    auto vertex = [&](vec3 p, int face, int u, int v, int tex, bool corner, bool sway = false) {
                        p += vec3(float(x), float(y), float(z));
                        int ix = int(p.x), iy = int(p.y), iz = int(p.z);
                        packVertex(out.detail, ix, iy, iz, face, u, v, corner ? UV_CORNER : UV_WORLD, sway, 0, tex, L >> 4, L & 15, 3,
                                   bx.clim(x, z, da.tint));
                        uint32_t frac = uint32_t(std::round((p.x - ix) * 16)) | (uint32_t(std::round((p.y - iy) * 16)) << 4) |
                                        (uint32_t(std::round((p.z - iz) * 16)) << 8);
                        out.detail.back() |= frac << 12;
                    };
                    auto quad = [&](vec3 a, vec3 b, vec3 c, vec3 d, int face, int tex, bool corner = false, bool sway = false) {
                        vertex(a, face, 0, 0, tex, corner, sway);
                        vertex(b, face, 1, 0, tex, corner, sway);
                        vertex(c, face, 1, 1, tex, corner, sway);
                        vertex(d, face, 0, 1, tex, corner, sway);
                    };
                    auto cube = [&](float x0, float y0, float z0, float x1, float y1, float z1, int tex, int top = -1) {
                        quad({x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, 0, tex);
                        quad({x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, 1, tex);
                        quad({x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, 2, top < 0 ? tex : top);
                        quad({x0, y0, z1}, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, 3, tex);
                        quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, 4, tex);
                        quad({x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, 5, tex);
                    };
                    if (a == B_VINE) {
                        bool attached = false;
                        const int dx[] = {1, -1, 0, 0}, dz[] = {0, 0, 1, -1};
                        for (int k = 0; k < 4; ++k)
                            if (def(bx.b(x + dx[k], y, z + dz[k])).collides) {
                                float lo = 1.0f / 16, hi = 15.0f / 16;
                                vec3 a, b;
                                if (k < 2) {
                                    float X = k == 0 ? hi : lo;
                                    a = {X, 0, 0};
                                    b = {X, 0, 1};
                                } else {
                                    float Z = k == 2 ? hi : lo;
                                    a = {0, 0, Z};
                                    b = {1, 0, Z};
                                }
                                quad(a, b, b + vec3(0, 1, 0), a + vec3(0, 1, 0), 6, T_VINE, true);
                                quad(b, a, a + vec3(0, 1, 0), b + vec3(0, 1, 0), 6, T_VINE, true);
                                attached = true;
                            }
                        if (!attached)
                            for (int k = 0; k < 2; ++k) {
                                vec3 a = k ? vec3(0.75, 0, 0.25) : vec3(0.25, 0, 0.25), b = k ? vec3(0.25, 0, 0.75) : vec3(0.75, 0, 0.75);
                                quad(a, b, b + vec3(0, 1, 0), a + vec3(0, 1, 0), 6, T_VINE, true, true);
                                quad(b, a, a + vec3(0, 1, 0), b + vec3(0, 1, 0), 6, T_VINE, true, true);
                            }
                    } else if (a == B_BRANCH || a == B_ROOT) {
                        cube(0.125, 0, 0.375, 0.875, 0.125, 0.5, T_LOG_SIDE, T_LOG_SIDE);
                        cube(0.5, 0, 0.125, 0.625, 0.125, 0.875, T_LOG_SIDE, T_LOG_SIDE);
                        if (a == B_ROOT) {
                            cube(0.125, 0, 0.75, 0.625, 0.125, 0.875, T_LOG_SIDE);
                            cube(0.125, 0, 0.25, 0.25, 0.375, 0.625, T_LOG_SIDE);
                        } else
                            cube(0.625, 0.125, 0.5, 0.75, 0.25, 0.75, T_LOG_SIDE, T_LOG_TOP);
                    } else if (a == B_PEBBLES) {
                        cube(0.125, 0, 0.125, 0.375, 0.125, 0.375, T_ANDESITE);
                        cube(0.5, 0, 0.375, 0.875, 0.25, 0.75, T_GRAVEL);
                        cube(0.125, 0, 0.75, 0.25, 0.125, 0.875, T_STONE);
                    } else if (a == B_LILY_PAD) {
                        cube(0.125, 0, 0.25, 0.875, 0.0625, 0.75, T_LILY_PAD);
                        cube(0.25, 0, 0.125, 0.625, 0.0625, 0.25, T_LILY_PAD);
                        cube(0.25, 0, 0.75, 0.625, 0.0625, 0.875, T_LILY_PAD);
                    } else if (a == B_BARREL_CACTUS) {
                        cube(0.125, 0, 0.25, 0.875, 0.75, 0.75, T_BARREL_CACTUS, T_CACTUS_TOP);
                        cube(0.25, 0, 0.125, 0.75, 0.75, 0.25, T_BARREL_CACTUS, T_CACTUS_TOP);
                        cube(0.25, 0, 0.75, 0.75, 0.75, 0.875, T_BARREL_CACTUS, T_CACTUS_TOP);
                        cube(0.375, 0.75, 0.375, 0.625, 0.875, 0.625, T_CACTUS_FLOWER);
                    }
                    continue;
                }
                if (da.render == Render::Cross) {
                    uint8_t L = bx.l(x, y, z);
                    int sk = L >> 4, bl = L & 15;
                    bool sway = a != B_TORCH && a != B_FIRE && a != B_SUGAR_CANE;
                    uint32_t cl = bx.clim(x, z, da.tint);
                    const int pts[2][2][2] = {{{0, 0}, {1, 1}}, {{1, 0}, {0, 1}}};
                    for (int pl = 0; pl < 2; ++pl)
                        for (int side = 0; side < 2; ++side) {
                            int ax = pts[pl][side][0], az = pts[pl][side][1];
                            int bx2 = pts[pl][1 - side][0], bz = pts[pl][1 - side][1];
                            packVertex(out.cutout, x + ax, y, z + az, 6, 0, 0, UV_CORNER, false, 0, da.texSide, sk, bl, 3, cl);
                            packVertex(out.cutout, x + bx2, y, z + bz, 6, 1, 0, UV_CORNER, false, 0, da.texSide, sk, bl, 3, cl);
                            packVertex(out.cutout, x + bx2, y + 1, z + bz, 6, 1, 1, UV_CORNER, sway, 0, da.texSide, sk, bl, 3, cl);
                            packVertex(out.cutout, x + ax, y + 1, z + az, 6, 0, 1, UV_CORNER, sway, 0, da.texSide, sk, bl, 3, cl);
                        }
                    continue;
                }
                if (da.render != Render::Fluid) continue;
                std::vector<uint32_t>& dst = a == B_WATER ? out.trans : out.solid;
                auto sealed = [&](uint8_t b) { return b == a || occludes(b) || b == B_ICE; };
                if (sealed(bx.b(x, y + 1, z)) && sealed(bx.b(x, y - 1, z)) && sealed(bx.b(x + 1, y, z)) && sealed(bx.b(x - 1, y, z)) &&
                    sealed(bx.b(x, y, z + 1)) && sealed(bx.b(x, y, z - 1)))
                    continue;
                size_t waterStart = dst.size();
                vec3 flow;
                bool bank[4] = {};
                if (a == B_WATER) {
                    vec3 hint;
                    int ri = (z + 1) * bx.r.sx + x + 1, wy = bx.r.y0 + 1 + y;
                    if (!bx.r.riverLevel.empty() && wy < int(bx.r.riverLevel[ri]) && wy >= int(bx.r.riverLevel[ri]) - 12)
                        hint = {float(bx.r.riverX[ri]) / 100, 0, float(bx.r.riverZ[ri]) / 100};
                    flow = waterCurrentAt(
                        x, y, z, hint, [&](int x, int y, int z) { return bx.b(x, y, z); },
                        [&](int x, int y, int z) { return bx.m(x, y, z); });
                    bank[0] = def(bx.b(x + 1, y, z)).collides;
                    bank[1] = def(bx.b(x - 1, y, z)).collides;
                    bank[2] = def(bx.b(x, y, z + 1)).collides;
                    bank[3] = def(bx.b(x, y, z - 1)).collides;
                }
                uint8_t above = bx.b(x, y + 1, z);
                bool fullTop = above == a;
                auto exposedSurface16At = [&](int px, int pz, int refY) -> int {
                    int best = -100000;
                    for (int yy = refY + 1; yy >= refY - 1; --yy) {
                        if (bx.b(px, yy, pz) != a || bx.b(px, yy + 1, pz) == a) continue;
                        int am = fluidSurfaceAmountOf(a, bx.m(px, yy, pz));
                        int hh = yy * 16 + std::clamp((am * 16 + MAX_FLUID / 2) / MAX_FLUID, 1, 16);
                        if (best < -9999 || std::abs(hh - (refY * 16 + 16)) < std::abs(best - (refY * 16 + 16))) best = hh;
                    }
                    return best;
                };
                auto chainCorner = [&](int ownH, int refL, int cxb, int czb) -> int {
                    int cand[5], n = 0;
                    for (int dz = -1; dz <= 0; ++dz)
                        for (int dx = -1; dx <= 0; ++dx) {
                            int hh = exposedSurface16At(cxb + dx, czb + dz, refL);
                            if (hh > -9999) cand[n++] = hh;
                        }
                    bool hasOwn = false;
                    for (int i = 0; i < n; ++i) hasOwn |= cand[i] == ownH;
                    if (!hasOwn) cand[n++] = ownH;
                    std::sort(cand, cand + n);
                    int k = 0;
                    while (cand[k] != ownH) ++k;
                    int lo = k, hi = k;
                    while (lo > 0 && cand[lo] - cand[lo - 1] <= 16) --lo;
                    while (hi + 1 < n && cand[hi + 1] - cand[hi] <= 16) ++hi;
                    int sum = 0;
                    for (int i = lo; i <= hi; ++i) sum += cand[i];
                    int cnt = hi - lo + 1, q = sum + cnt / 2;
                    return q >= 0 ? q / cnt : -((-q + cnt - 1) / cnt);
                };
                const int ownAbsH = y * 16 + std::clamp((fluidSurfaceAmountOf(a, bx.m(x, y, z)) * 16 + MAX_FLUID / 2) / MAX_FLUID, 1, 16);
                auto cornerAbsH = [&](int cx, int cz) -> int {
                    if (fullTop) return (y + 1) * 16;
                    return chainCorner(ownAbsH, y, x + cx, z + cz);
                };
                int h00 = cornerAbsH(0, 0), h10 = cornerAbsH(1, 0), h11 = cornerAbsH(1, 1), h01 = cornerAbsH(0, 1);
                int tex = da.texSide;
                auto lightOf = [&](int px, int py, int pz, int& sk, int& bl) {
                    uint8_t L = bx.l(px, py, pz);
                    sk = L >> 4;
                    bl = L & 15;
                    uint8_t own = bx.l(x, y, z);
                    sk = std::max(sk, own >> 4);
                    bl = std::max(bl, own & 15);
                };
                int sk = 0, bl = 0;
                lightOf(x, y + 1, z, sk, bl);
                if (!fullTop && !occludes(above) && above != B_ICE) {
                    lightOf(x, y + 1, z, sk, bl);
                    auto V = [&](int px, int pz, int hh) {
                        int Y = (hh + 15) / 16, off = Y * 16 - hh;
                        packVertex(dst, x + px, Y, z + pz, 2, px, pz, UV_FLUID, true, std::clamp(off, 0, 15), tex, sk, bl, 3,
                                   bx.clim(x + px, z + pz, da.tint));
                    };
                    V(0, 0, h00);
                    V(0, 1, h01);
                    V(1, 1, h11);
                    V(1, 0, h10);
                }
                uint8_t below = bx.b(x, y - 1, z);
                if (below != a && below != B_ICE && !occludes(below) && !def(below).fluid) {
                    lightOf(x, y - 1, z, sk, bl);
                    packVertex(dst, x, y, z, 3, 0, 0, UV_FLUID, false, 0, tex, sk, bl, 3, bx.clim(x, z, da.tint));
                    packVertex(dst, x + 1, y, z, 3, 1, 0, UV_FLUID, false, 0, tex, sk, bl, 3, bx.clim(x + 1, z, da.tint));
                    packVertex(dst, x + 1, y, z + 1, 3, 1, 1, UV_FLUID, false, 0, tex, sk, bl, 3, bx.clim(x + 1, z + 1, da.tint));
                    packVertex(dst, x, y, z + 1, 3, 0, 1, UV_FLUID, false, 0, tex, sk, bl, 3, bx.clim(x, z + 1, da.tint));
                }
                struct Side {
                    int f, dx, dz, ax, az, bx, bz;
                };
                const Side sides[4] = {{0, 1, 0, 1, 1, 1, 0}, {1, -1, 0, 0, 0, 0, 1}, {4, 0, 1, 0, 1, 1, 1}, {5, 0, -1, 1, 0, 0, 0}};
                auto hAt = [&](int cx, int cz) { return cx == 0 ? (cz == 0 ? h00 : h01) : (cz == 0 ? h10 : h11); };
                auto packFluidY = [&](int px, int pz, int hh, int face, int cu, int cv, uint32_t climate) {
                    int Y = (hh + 15) / 16, off = Y * 16 - hh;
                    packVertex(dst, px, Y, pz, face, cu, cv, UV_FLUID, false, std::clamp(off, 0, 15), tex, sk, bl, 3, climate);
                };
                for (const Side& sd : sides) {
                    uint8_t nb = bx.b(x + sd.dx, y, z + sd.dz);
                    if (nb == a || occludes(nb) || nb == B_ICE) continue;
                    lightOf(x + sd.dx, y, z + sd.dz, sk, bl);
                    int ha = hAt(sd.ax, sd.az), hb = hAt(sd.bx, sd.bz);
                    int baseA = y * 16, baseB = y * 16;
                    if (bx.b(x + sd.dx, y - 1, z + sd.dz) == a) {
                        const int nx = x + sd.dx, nz = z + sd.dz;
                        const int nOwn = (y - 1) * 16 +
                                         std::clamp((fluidSurfaceAmountOf(a, bx.m(nx, y - 1, nz)) * 16 + MAX_FLUID / 2) / MAX_FLUID, 1, 16);
                        baseA = std::min(baseA, chainCorner(nOwn, y - 1, x + sd.ax, z + sd.az));
                        baseB = std::min(baseB, chainCorner(nOwn, y - 1, x + sd.bx, z + sd.bz));
                    }
                    uint32_t ca = bx.clim(x + sd.ax, z + sd.az, da.tint), cb = bx.clim(x + sd.bx, z + sd.bz, da.tint);
                    if (baseA < ha || baseB < hb) {
                        packFluidY(x + sd.ax, z + sd.az, std::min(baseA, ha), sd.f, 0, 0, ca);
                        packFluidY(x + sd.bx, z + sd.bz, std::min(baseB, hb), sd.f, 1, 0, cb);
                        packFluidY(x + sd.bx, z + sd.bz, hb, sd.f, 1, 1, cb);
                        packFluidY(x + sd.ax, z + sd.az, ha, sd.f, 0, 1, ca);
                    }
                }
                if (a == B_WATER) {
                    auto motionWord = [&](float shore, int flags) {
                        return uint32_t(std::clamp(int(flow.x * 63) + 63, 0, 126)) |
                               (uint32_t(std::clamp(int(flow.z * 63) + 63, 0, 126)) << 7) |
                               (uint32_t(std::clamp(int(shore * 255), 0, 255)) << 14) | (uint32_t(flags) << 22);
                    };
                    for (size_t i = waterStart; i < dst.size(); i += VERT_WORDS) {
                        uint32_t v = dst[i];
                        float px = float(int(v & 63) - x), pz = float(int((v >> 12) & 63) - z);
                        float shore = 1;
                        if (bank[0]) shore = std::min(shore, 1 - px);
                        if (bank[1]) shore = std::min(shore, px);
                        if (bank[2]) shore = std::min(shore, 1 - pz);
                        if (bank[3]) shore = std::min(shore, pz);
                        bool topCorner = int((v >> 6) & 63) > y;
                        bool wave = topCorner && hAt(int(px), int(pz)) == (y + 1) * 16;
                        bool ocean = wave && bx.r.y0 + 1 + y == SEA - 1;
                        int ix = int(px), iz = int(pz), waterDepth = 8;
                        for (int dz = -1; dz <= 0; ++dz)
                            for (int dx = -1; dx <= 0; ++dx) {
                                int nx = x + ix + dx, nz = z + iz + dz;
                                if (bx.b(nx, y, nz) != B_WATER || bx.b(nx, y - 1, nz) != B_WATER || !fluidIsSource(bx.m(nx, y, nz)))
                                    ocean = false;
                                int ri = (nz + 1) * bx.r.sx + nx + 1;
                                if (!bx.r.riverX.empty() && (bx.r.riverX[ri] != 0 || bx.r.riverZ[ri] != 0)) ocean = false;
                                if (ocean) {
                                    int depth = 0;
                                    while (depth < 8 && y - depth >= -1 && bx.b(nx, y - depth, nz) == B_WATER) ++depth;
                                    waterDepth = std::min(waterDepth, depth);
                                }
                                uint8_t over = bx.b(x + ix + dx, y + 1, z + iz + dz);
                                if (bx.b(x + ix + dx, y, z + iz + dz) == B_ICE || over == B_WATER || def(over).collides) wave = false;
                            }
                        dst[i + 2] =
                            motionWord(ocean ? float(waterDepth) / 8.0f : shore,
                                       (flow.y < 0 ? 1 : 0) | (wave ? 4 : 0) | (ocean && wave ? 128 : 0) |
                                           ((int(bank[0]) | (int(bank[1]) << 1) | (int(bank[2]) << 2) | (int(bank[3]) << 3)) << 3));
                    }
                }
            }
}

void bakeColor(const Box& bx, MeshOut& out) {
    for (auto* vertices : {&out.solid, &out.cutout, &out.trans, &out.detail})
        for (size_t i = 0; i < vertices->size(); i += VERT_WORDS) {
            uint32_t a = (*vertices)[i];
            uint32_t hi = (*vertices)[i + 3];
            uint16_t rgb = (((*vertices)[i + 1] >> 12) & 15)
                               ? bx.color(int(a & 63) + int((hi >> 29) & 1) * 64, int((a >> 6) & 63) + int((hi >> 30) & 1) * 64,
                                          int((a >> 12) & 63) + int((hi >> 31) & 1) * 64, int((a >> 18) & 7))
                               : 0;
            (*vertices)[i + 3] = ((*vertices)[i + 3] & 0xfffff000u) | rgb;
        }
}
}

void groupFaces(std::vector<uint32_t>& data, std::array<uint32_t, 7>& counts) {
    constexpr int Q = VERT_WORDS * 4;
    counts = {};
    auto bucket = [&](size_t i) {
        uint32_t a = data[i];
        int f = (a >> 18) & 7;
        if (f > 5) return 6;
        for (int k = 1; k < 4; ++k)
            if (((data[i + k * VERT_WORDS] ^ a) & (1u << 25)) != 0) return 6;
        return f;
    };
    for (size_t i = 0; i < data.size(); i += Q) ++counts[bucket(i)];
    std::array<size_t, 7> offset{};
    for (int f = 1; f < 7; ++f) offset[f] = offset[f - 1] + size_t(counts[f - 1]) * Q;
    static thread_local std::vector<uint32_t> sorted;
    sorted.resize(data.size());
    for (size_t i = 0; i < data.size(); i += Q) {
        int f = bucket(i);
        std::memcpy(sorted.data() + offset[f], data.data() + i, Q * sizeof(uint32_t));
        offset[f] += Q;
    }
    data.swap(sorted);
}

namespace {
uint64_t sectionVisibility(const Box& bx) {
    static thread_local std::vector<uint8_t> seen;
    static thread_local std::vector<int> stack;
    const int N = CS;
    seen.assign(size_t(N) * N * N, 0);
    uint64_t vis = 0;
    int open = 0;
    for (int i = 0; i < N * N * N; ++i) {
        int x = i % N, z = (i / N) % N, y = i / (N * N);
        if (seen[size_t(i)]) continue;
        if (occludes(bx.b(x, y, z))) {
            seen[size_t(i)] = 1;
            continue;
        }
        uint8_t faces = 0;
        stack.clear();
        stack.push_back(i);
        seen[size_t(i)] = 1;
        while (!stack.empty()) {
            int c = stack.back();
            stack.pop_back();
            ++open;
            int cx = c % N, cz = (c / N) % N, cy = c / (N * N);
            if (cx == N - 1) faces |= 1;
            if (cx == 0) faces |= 2;
            if (cy == N - 1) faces |= 4;
            if (cy == 0) faces |= 8;
            if (cz == N - 1) faces |= 16;
            if (cz == 0) faces |= 32;
            const int nx[6] = {cx + 1, cx - 1, cx, cx, cx, cx}, ny[6] = {cy, cy, cy + 1, cy - 1, cy, cy},
                      nz[6] = {cz, cz, cz, cz, cz + 1, cz - 1};
            for (int d = 0; d < 6; ++d) {
                if (nx[d] < 0 || ny[d] < 0 || nz[d] < 0 || nx[d] >= N || ny[d] >= N || nz[d] >= N) continue;
                int ni = (ny[d] * N + nz[d]) * N + nx[d];
                if (seen[size_t(ni)]) continue;
                seen[size_t(ni)] = 1;
                if (occludes(bx.b(nx[d], ny[d], nz[d]))) continue;
                stack.push_back(ni);
            }
        }
        for (int a = 0; a < 6; ++a)
            if (faces & (1 << a))
                for (int b = 0; b < 6; ++b)
                    if (faces & (1 << b)) vis |= 1ull << (a * 6 + b);
    }
    (void)open;
    return vis;
}
}

void meshSection(const Region& r, MeshOut& out, bool fancyLeaves) {
    out.clear();
    buildTabs();
    Box bx{r, {CS, CS, CS}};
    out.visibility = sectionVisibility(bx);
    bx.cacheClimate();
    meshCubes(bx, out, fancyLeaves);
    meshSpecial(bx, out);
    bakeColor(bx, out);
    groupFaces(out.solid, out.solidFaces);
    groupFaces(out.cutout, out.cutoutFaces);
}

void meshRegion(const Region& r, int sx, int sy, int sz, MeshOut& out, bool fancyLeaves) {
    out.clear();
    buildTabs();
    Box bx{r, {sx, sy, sz}};
    bx.cacheClimate();
    meshCubes(bx, out, fancyLeaves);
    meshSpecial(bx, out);
    bakeColor(bx, out);
}

void meshRegionOwned(const Region& r, int sx, int sy, int sz, const uint8_t* owned, MeshOut& out, bool fancyLeaves) {
    out.clear();
    buildTabs();
    Box bx{r, {sx, sy, sz}, owned};
    bx.cacheClimate();
    meshCubes(bx, out, fancyLeaves);
    meshSpecial(bx, out);
    bakeColor(bx, out);
}

void meshGrid(const uint8_t* blocks, int sx, int sy, int sz, MeshOut& out, int sky, int blk, uint8_t climT, uint8_t climH) {
    buildTabs();
    Region r;
    r.sx = sx + 2;
    r.sy = sy + 2;
    r.sz = sz + 2;
    r.climT.assign(size_t(r.sx) * r.sz, climT);
    r.climH.assign(size_t(r.sx) * r.sz, climH);
    size_t n = size_t(r.sx) * r.sy * r.sz;
    r.blocks.assign(n, B_AIR);
    r.light.assign(n, uint8_t((sky << 4) | blk));
    for (int y = 0; y < sy; ++y)
        for (int z = 0; z < sz; ++z)
            std::memcpy(&r.blocks[size_t(((y + 1) * r.sz + (z + 1)) * r.sx + 1)], &blocks[size_t((y * sz + z) * sx)], size_t(sx));
    out.clear();
    Box bx{r, {sx, sy, sz}};
    meshCubes(bx, out, true);
    meshSpecial(bx, out);
    bakeColor(bx, out);
}

}
