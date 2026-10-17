#include "stream/LightVolume.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace qc {
// 3d light texture around the player, its what lights the stuff that moves
void LightVolume::init(Renderer* r, int sizeMinis) {
    r_ = r;
    n_ = std::max(64, sizeMinis / CS * CS);
    s_ = n_ / CS;
    r_->initLightVolume(n_);
    slots_.assign(size_t(s_) * s_, Slot{});
    buf_.resize(size_t(CS3) * 4);
    order_.clear();
    for (int z = 0; z < s_; ++z)
        for (int x = 0; x < s_; ++x) order_.push_back({x, z});
}

void LightVolume::reset() {
    for (Slot& s : slots_) s = Slot{};
}

void LightVolume::upload(const Column& c, int s, int slotX, int slotZ) {
    const Section& sec = c.sec[s];
    uint8_t* out = buf_.data();
    if (!sec.light && !sec.blocks) {
        uint8_t L = sec.fillLight;
        uint8_t sky = uint8_t((L >> 4) * 17), blk = uint8_t((L & 15) * 17);
        if (def(sec.fillBlock).light == LightKind::Opaque) {
            sky = 0;
            blk = 0;
        }
        uint8_t weight = def(sec.fillBlock).light == LightKind::Opaque ? 0 : 255;
        for (size_t i = 0; i < buf_.size(); i += 4) {
            out[i] = sky;
            out[i + 1] = blk;
            out[i + 2] = lightCost(sec.fillBlock) >= 99 ? 255 : sec.fillBlock == B_WATER ? 64 : 0;
            out[i + 3] = weight;
        }
    } else {
        static bool opq[256];
        static bool opqInit = false;
        if (!opqInit) {
            for (int b = 0; b < 256; ++b) opq[b] = def(uint8_t(b)).light == LightKind::Opaque;
            opqInit = true;
        }
        auto opaque = [&](int i) { return opq[sec.block(i)]; };
        for (int z = 0; z < CS; ++z)
            for (int y = 0; y < CS; ++y)
                for (int x = 0; x < CS; ++x) {
                    int i = idx3(x, y, z);
                    uint8_t L = sec.lightAt(i);
                    int sky = L >> 4, blk = L & 15;
                    if (opaque(i)) sky = blk = 0;
                    size_t o = (size_t((z * CS + y) * CS + x)) * 4;
                    out[o] = uint8_t(sky * 17);
                    out[o + 1] = uint8_t(blk * 17);
                    out[o + 2] = lightCost(sec.block(i)) >= 99 ? 255 : sec.block(i) == B_WATER ? 64 : 0;
                    out[o + 3] = opaque(i) ? 0 : 255;
                }
    }
    static thread_local std::vector<uint8_t> rgb(CS3 * 3);
    if (!sec.radiance && sec.fillRadiance == 0)
        std::fill(rgb.begin(), rgb.end(), 0);
    else
        for (int z = 0; z < CS; ++z)
            for (int y = 0; y < CS; ++y)
                for (int x = 0; x < CS; ++x) {
                    int i = idx3(x, y, z), v[3] = {};
                    uint16_t c = sec.radianceAt(i);
                    for (int ch = 0; ch < 3; ++ch) v[ch] = (c >> (ch * 4)) & 15;
                    if (def(sec.block(i)).light == LightKind::Opaque) v[0] = v[1] = v[2] = 0;
                    size_t o = size_t((z * CS + y) * CS + x) * 3;
                    for (int ch = 0; ch < 3; ++ch) rgb[o + ch] = uint8_t(v[ch] * 17);
                }
    r_->uploadLightBlock(slotX * CS, s * CS, slotZ * CS, out, rgb.data());
}

void LightVolume::update(World& w, vec3 cam, int budget) {
    uploadsLastFrame = 0;
    auto started = std::chrono::steady_clock::now();
    if (!r_ || !n_) return;
    int camX = int(std::floor(cam.x / VOX)), camZ = int(std::floor(cam.z / VOX));
    int minCx = floorDiv(camX - n_ / 2 + CS / 2, CS), minCz = floorDiv(camZ - n_ / 2 + CS / 2, CS);
    r_->setLightWindow(minCx * CS, minCz * CS);
    for (int oz = 0; oz < s_; ++oz)
        for (int ox = 0; ox < s_; ++ox) {
            int cx = minCx + ox, cz = minCz + oz, sx = floorMod(cx, s_), sz = floorMod(cz, s_);
            auto& slot = slots_[size_t(sz * s_ + sx)];
            int64_t key = colKey(cx, cz);
            if (slot.key != key) {
                slot.key = key;
                for (bool& v : slot.valid) v = false;
                r_->invalidateLightColumn(sx, sz);
            }
        }
    int ccx = floorDiv(camX, CS), ccz = floorDiv(camZ, CS);
    std::sort(order_.begin(), order_.end(), [&](const std::pair<int, int>& a, const std::pair<int, int>& b) {
        int ax = minCx + a.first - ccx, az = minCz + a.second - ccz, bx = minCx + b.first - ccx, bz = minCz + b.second - ccz;
        return ax * ax + az * az < bx * bx + bz * bz;
    });
    for (int pass = 0; pass < 2; ++pass)
        for (auto& [ox, oz] : order_) {
            int cx = minCx + ox, cz = minCz + oz;
            Column* c = w.column(cx, cz);
            if (!c || (!c->hasLight && c->state != ColState::Lit)) continue;
            int sx = floorMod(cx, s_), sz = floorMod(cz, s_);
            Slot& slot = slots_[size_t(sz * s_ + sx)];
            int64_t key = colKey(cx, cz);
            if (slot.key != key) {
                slot.key = key;
                for (bool& v : slot.valid) v = false;
            }
            int eyeSection = std::clamp(int(std::floor(cam.y / VOX)) / CS, 0, NSEC - 1);
            for (int dist = 0; dist < NSEC; ++dist) {
                int cand[2] = {eyeSection - dist, eyeSection + dist};
                int n = dist == 0 ? 1 : 2;
                for (int q = 0; q < n; ++q) {
                    int s = cand[q];
                    if (s < 0 || s >= NSEC) continue;
                    Section& sec = c->sec[s];
                    if ((pass == 0) != slot.valid[s]) continue;
                    if (slot.valid[s] && !sec.lightDirty) continue;
                    if (uploadsLastFrame >= budget ||
                        (uploadsLastFrame > 0 &&
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() > 2.0))
                        return;
                    upload(*c, s, sx, sz);
                    sec.lightDirty = false;
                    slot.valid[s] = true;
                    ++uploadsLastFrame;
                }
            }
        }
}

}
