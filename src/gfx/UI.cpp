#include "gfx/UI.h"
#include <chrono>
#include "core/Assets.h"
#include "stb_image.h"
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace qc {
namespace {
constexpr int kCellW = 8, kCellH = 8, kCols = 16;
constexpr int kAtlasW = kCols * kCellW, kAtlasH = 6 * kCellH;
constexpr int kSolidChar = 127;
const char* kUIVS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in float aLayer;
layout(location = 3) in vec4 aCol;
uniform vec2 uScreen;
out vec3 vUV;
out vec4 vCol;
void main() {
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
    vUV = vec3(aUV, aLayer);
    vCol = aCol;
}
)";
const char* kUIFS = R"(#version 330 core
in vec3 vUV;
in vec4 vCol;
uniform sampler2D uFont;
uniform sampler2DArray uTex;
out vec4 frag;
void main() {
    vec4 c = vUV.z < 0.0 ? texture(uFont, vUV.xy) : texture(uTex, vUV);
    c *= vCol;
    if (c.a < 0.01) discard;
    frag = c;
}
)";
}

bool UI::init(Renderer* r) {
    r_ = r;
    if (!shader_.build("ui", kUIVS, kUIFS)) return false;
    int fw = 0, fh = 0, fn = 0;
    std::string fontPath = assetPath("ui/font.png");
    unsigned char* img = stbi_load(fontPath.c_str(), &fw, &fh, &fn, 4);
    if (!img || fw != kAtlasW || fh != kAtlasH) {
        std::fprintf(stderr, "[quadaeria] can't load %s\n", fontPath.c_str());
        if (img) stbi_image_free(img);
        return false;
    }
    std::vector<uint8_t> px(img, img + size_t(fw) * fh * 4);
    stbi_image_free(img);
    for (int c = 32; c < 128; ++c) {
        int ci = c - 32, ox = (ci % kCols) * kCellW, oy = (ci / kCols) * kCellH, maxX = 0;
        for (int y = 0; y < kCellH; ++y)
            for (int x = 0; x < 5; ++x)
                if (px[(size_t(oy + y) * kAtlasW + size_t(ox + x)) * 4 + 3] > 127) maxX = std::max(maxX, x + 1);
        adv_[c] = maxX ? maxX + 1 : 6;
    }
    for (int i = 0; i < 32; ++i) adv_[i] = 6;
    adv_[int(' ')] = 4;
    gl::GenTextures(1, &fontTex_);
    gl::BindTexture(gl::TEXTURE_2D, fontTex_);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, kAtlasW, kAtlasH, 0, gl::RGBA, gl::UNSIGNED_BYTE, px.data());
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    gl::GenVertexArrays(1, &vao_);
    gl::BindVertexArray(vao_);
    gl::GenBuffers(1, &vbo_);
    gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 2, gl::FLOAT, gl::FALSE_, sizeof(V), (void*)0);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, gl::FALSE_, sizeof(V), (void*)8);
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 1, gl::FLOAT, gl::FALSE_, sizeof(V), (void*)16);
    gl::EnableVertexAttribArray(3);
    gl::VertexAttribPointer(3, 4, gl::UNSIGNED_BYTE, gl::TRUE_, sizeof(V), (void*)20);
    gl::BindVertexArray(0);
    return true;
}

void UI::shutdown() {
    shader_.destroy();
    if (fontTex_) gl::DeleteTextures(1, &fontTex_);
    if (vbo_) gl::DeleteBuffers(1, &vbo_);
    if (vao_) gl::DeleteVertexArrays(1, &vao_);
}

void UI::begin(int winW, int winH, const UIInput& input, int scaleSetting, bool safe43) {
    winW_ = std::max(winW, 1);
    winH_ = std::max(winH, 1);
    int maxScale = std::max(1, std::min(winW_ / 320, winH_ / 240));
    int fit = std::max(1, std::min(winW_ / 384, winH_ / 288));
    int autoScale = std::clamp(int(std::lround(float(winH_) / 440.0f)), 1, fit);
    (void)maxScale;
    scale = scaleSetting > 0 ? std::min(scaleSetting, fit) : autoScale;
    scale = std::max(scale, 1);
    float fullW = float(winW_) / scale;
    h = float(winH_) / scale;
    safeOriginX_ = 0.0f;
    w = fullW;
    if (safe43) {
        float safeW = std::min(fullW, h * (4.0f / 3.0f));
        safeOriginX_ = (fullW - safeW) * 0.5f;
        w = safeW;
    }
    in = input;
    mouse = in.mouse / float(scale) - vec2(safeOriginX_, 0.0f);
    consumedClick = false;
    back_.clear();
    front_.clear();
    labels_.clear();
    icons_.clear();
    layers_.clear();
    sliderSeq_ = 0;
    if (!in.down) activeSlider_ = -1;
    static const auto t0 = std::chrono::steady_clock::now();
    float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    dt_ = std::clamp(t - lastTime_, 0.0f, 0.1f);
    lastTime_ = t;
    time_ = t;
    ++frame_;
    prevHoverId_ = hoverId_;
    hoverId_ = 0;
    if (!in.down && !in.released) pressOwner_ = 0;
}

float UI::ease(float x, float y, bool on, float rate) {
    uint32_t key = uint32_t(int(x * 4)) * 73856093u ^ uint32_t(int(y * 4)) * 19349663u;
    if (!key) key = 1;
    for (auto& e : anim_)
        if (e.key == key) {
            if (e.frame + 1 < frame_) e.v = on ? 1.0f : 0.0f;
            e.frame = frame_;
            e.v += ((on ? 1.0f : 0.0f) - e.v) * (1.0f - std::exp(-dt_ * rate));
            return e.v;
        }
    if (anim_.size() > 384) anim_.erase(anim_.begin(), anim_.begin() + 128);
    anim_.push_back({key, on ? 1.0f : 0.0f, frame_});
    return on ? 1.0f : 0.0f;
}

bool UI::clicked(uint32_t id, bool hov) {
    if (hov && in.pressed && !consumedClick) pressOwner_ = id;
    if (hov && in.released && pressOwner_ == id && !consumedClick) {
        consumedClick = true;
        pressOwner_ = 0;
        return true;
    }
    return false;
}

void UI::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float layer, uint32_t c0, uint32_t c1,
              bool horizontal) {
    std::vector<V>& d = front_;
    float s = float(scale);
    x0 = std::floor((x0 + safeOriginX_) * s + 0.5f);
    x1 = std::floor((x1 + safeOriginX_) * s + 0.5f);
    y0 = std::floor(y0 * s + 0.5f);
    y1 = std::floor(y1 * s + 0.5f);
    const uint32_t a = c0, b = horizontal ? c1 : c0, c = c1, e = horizontal ? c0 : c1;
    d.push_back({x0, y0, u0, v0, layer, a});
    d.push_back({x1, y0, u1, v0, layer, b});
    d.push_back({x1, y1, u1, v1, layer, c});
    d.push_back({x1, y1, u1, v1, layer, c});
    d.push_back({x0, y1, u0, v1, layer, e});
    d.push_back({x0, y0, u0, v0, layer, a});
}

void UI::rect(float x, float y, float rw, float rh, uint32_t c) { gradient(x, y, rw, rh, c, c); }

void UI::gradient(float x, float y, float rw, float rh, uint32_t top, uint32_t bottom) {
    int ci = kSolidChar - 32;
    float u = ((ci % kCols) * kCellW + 4.0f) / kAtlasW;
    float v = ((ci / kCols) * kCellH + 4.0f) / kAtlasH;
    quad(x, y, x + rw, y + rh, u, v, u, v, -1, top, bottom);
}

void UI::inset(float x, float y, float rw, float rh, bool hot) {
    rect(x, y, rw, rh, hot ? col::PaperDeep : col::PaperDim);
    rect(x, y, rw, 1, col::rgba(0, 0, 0, 28));
}

void UI::plate(float x, float y, float rw, float rh, bool hot) {
    rect(x, y + rh, rw, 2, col::rgba(0, 0, 0, 50));
    rect(x, y, rw, rh, hot ? col::PaperLit : col::Paper);
}

void UI::panel(float x, float y, float rw, float rh) { plate(x, y, rw, rh, false); }

void UI::slot(float x, float y, bool hl) { inset(x, y, 18, 18, hl || hovered(x, y, 18, 18)); }

float UI::textWidth(const std::string& s, float size) const {
    float wd = 0;
    for (unsigned char c : s) wd += float(c < 128 ? adv_[c] : 6);
    return wd * size;
}

void UI::drawText(float x, float y, const std::string& s, uint32_t top, uint32_t bottom, float size) {
    float cx = x;
    for (unsigned char ch : s) {
        if (ch < 32 || ch > 126) ch = '?';
        int ci = ch - 32;
        float u0 = float((ci % kCols) * kCellW) / kAtlasW;
        float v0 = float((ci / kCols) * kCellH) / kAtlasH;
        float u1 = u0 + 8.0f / kAtlasW, v1 = v0 + 8.0f / kAtlasH;
        if (ch != ' ') quad(cx, y, cx + 8 * size, y + 8 * size, u0, v0, u1, v1, -1, top, bottom);
        cx += adv_[ch] * size;
    }
}

void UI::text(float x, float y, const std::string& s, uint32_t color, bool shadow, float size) {
    int r = color & 255, g = (color >> 8) & 255, b = (color >> 16) & 255;
    if (shadow && r + g + b > 450) {
        float o = std::max(1.0f, std::floor(size * 0.5f));
        uint32_t sc = col::rgba(0, 0, 0, std::min<int>(160, int(color >> 24)));
        drawText(x + o, y + o, s, sc, sc, size);
    }
    drawText(x, y, s, color, color, size);
}

void UI::heading(float cx, float y, const std::string& s) { textCentered(cx, y, s, col::Ink, 2); }

void UI::textCentered(float cx, float y, const std::string& s, uint32_t color, float size) {
    text(cx - textWidth(s, size) * 0.5f, y, s, color, true, size);
}

void UI::texQuad(float x, float y, float qw, float qh, int layer, uint32_t color) {
    quad(x, y, x + qw, y + qh, 0, 0, 1, 1, float(layer), color, color);
}

void UI::item(float x, float y, float size, ItemId id, int count) {
    if (!id) return;
    bool cube = false;
    int top = 0, side = 0, bottom = 0;
    if (isBlockItem(id)) {
        const BlockDef& d = def(BlockId(id));
        Render r = d.render;
        cube = r == Render::Opaque || r == Render::Leaves || r == Render::Glass || r == Render::Fluid;
        top = d.texTop;
        side = d.texSide;
        bottom = d.texBottom;
    }
    if (!cube) texQuad(x, y, size, size, isBlockItem(id) ? def(BlockId(id)).texSide : itemDef(id).tex);
    back_.insert(back_.end(), front_.begin(), front_.end());
    front_.clear();
    if (cube) {
        float s = float(scale);
        float px = (x + safeOriginX_ + size * 0.5f) * s, py = (y + size * 0.5f) * s, k = size * s * 0.62f;
        float cy = std::cos(PI / 4), sy = std::sin(PI / 4), cx = std::cos(PI / 6), sx = std::sin(PI / 6);
        float R[3][3] = {{cy, 0, sy}, {sx * sy, cx, -sx * cy}, {-cx * sy, sx, cx * cy}};
        float M[3][4];
        const float S[3] = {k, -k, k};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) M[r][c] = S[r] * R[r][c];
            M[r][3] = -0.5f * (M[r][0] + M[r][1] + M[r][2]);
        }
        M[0][3] += px;
        M[1][3] += py;
        M[2][3] += 0;
        BoxInstance b;
        b.r0 = {M[0][0], M[0][1], M[0][2], M[0][3]};
        b.r1 = {M[1][0], M[1][1], M[1][2], M[1][3]};
        b.r2 = {M[2][0], M[2][1], M[2][2], M[2][3]};
        b.layers = {float(top), float(bottom), float(side), float(side)};
        b.params = {15, 0, 0, 0};
        icons_.push_back(b);
    }
    size_t mark = front_.size();
    if (count > 1 || (isBlockItem(id) && count == 1)) {
        std::string n;
        if (isBlockItem(id) && def(uint8_t(id)).render != Render::Model)
            n = count >= 8 ? std::to_string(count / 8) + (count % 8 ? "+" : "") : std::to_string(count) + "m";
        else
            n = std::to_string(count);
        if (n != "1") text(x + size + 1 - textWidth(n), y + size - 7, n);
    }
    labels_.insert(labels_.end(), front_.begin() + long(mark), front_.end());
    front_.resize(mark);
}

void UI::layer() {
    if (back_.empty() && front_.empty() && labels_.empty() && icons_.empty()) return;
    Layer L;
    L.back.swap(back_);
    L.labels.swap(labels_);
    L.front.swap(front_);
    L.icons.swap(icons_);
    layers_.push_back(std::move(L));
}

bool UI::button(const std::string& label, float x, float y, float bw, float bh, bool enabled) {
    bool hov = enabled && hovered(x, y, bw, bh);
    uint32_t id = (uint32_t(int(x * 4)) * 2654435761u ^ uint32_t(int(y * 4)) * 40503u) | 1u;
    if (hov) {
        hoverId_ = id;
        if (prevHoverId_ != id) sfxHover = true;
        if (in.pressed && !consumedClick) sfxPress = true;
    }
    bool pressing = hov && in.down && pressOwner_ == id;
    float dy = pressing ? 1 : 0;
    rect(x, y + bh - 2, bw, 2, col::PaperDeep);
    rect(x, y + dy, bw, bh - 2, !enabled ? col::Paper : hov ? col::PaperLit : col::PaperDim);
    textCentered(x + bw * 0.5f, y + (bh - 10) * 0.5f + 1 + dy, label, enabled ? col::Ink : col::PaperDeep);
    return enabled && clicked(id, hov);
}

bool UI::menuEntry(const std::string& label, float x, float y, float w, float size, bool selected) {
    const float h = menuEntryHeight(size);
    bool hov = hovered(x, y, w, h);
    uint32_t id = (uint32_t(int(x * 4)) * 2654435761u ^ uint32_t(int(y * 4)) * 40503u) | 3u;
    if (hov) {
        hoverId_ = id;
        if (prevHoverId_ != id) sfxHover = true;
        if (in.pressed && !consumedClick) sfxPress = true;
    }
    if (selected)
        rect(x, y, w, h, col::Ink);
    else if (hov)
        rect(x, y, w, h, col::PaperDim);
    text(x + 8, y + 4, label, selected ? col::Paper : col::Ink, false, size);
    return clicked(id, hov);
}

bool UI::slider(const std::string& label, float& v, float lo, float hi, float x, float y, float sw, float sh) {
    int id = sliderSeq_++;
    bool hov = hovered(x, y, sw, sh);
    if (hov && in.pressed && !consumedClick) activeSlider_ = id;
    const float valW = std::max(36.0f, textWidth(label) + 6), tw = sw - valW - 4;
    bool changed = false;
    if (activeSlider_ == id && in.down) {
        float t = std::clamp((mouse.x - x - 3) / (tw - 6), 0.0f, 1.0f);
        float nv = lo + (hi - lo) * t;
        if (nv != v) {
            v = nv;
            changed = true;
        }
    }
    bool act = activeSlider_ == id;
    float t = std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f);
    const float gy = std::floor(y + sh * 0.5f) - 1;
    rect(x, gy, tw, 2, col::PaperDeep);
    rect(x, gy, std::floor(tw * t), 2, col::Ink);
    float kx = std::floor(x + t * (tw - 4));
    rect(kx, y + 3, 4, sh - 6, hov || act ? col::Ink : col::InkSoft);
    text(x + sw - textWidth(label), y + (sh - 8) * 0.5f + 1, label, col::Ink, false);
    return changed;
}

bool UI::textField(std::string& s, int& focus, int id, float x, float y, float fw, float fh, size_t maxLen) {
    bool hov = hovered(x, y, fw, fh);
    if (in.pressed) {
        if (hov)
            focus = id;
        else if (focus == id)
            focus = -1;
    }
    bool changed = false;
    if (focus == id) {
        for (unsigned c : in.typed)
            if (c >= 32 && c < 127 && s.size() < maxLen) {
                s.push_back(char(c));
                changed = true;
            }
        if (in.backspace && !s.empty()) {
            s.pop_back();
            changed = true;
        }
    }
    inset(x, y, fw, fh, focus == id);
    std::string shown = s;
    while (textWidth(shown) > fw - 12 && !shown.empty()) shown.erase(shown.begin());
    bool blink = focus == id && (int(time_ * 2.5f) % 2 == 0);
    text(x + 4, y + (fh - 8) * 0.5f + 1, shown + (blink ? "_" : ""), col::Ink, false);
    return changed;
}

void UI::flush() {
    auto drawVerts = [&](std::vector<V>& v) {
        if (v.empty()) return;
        shader_.use();
        shader_.set("uScreen", vec2(float(winW_), float(winH_)));
        shader_.set("uFont", 1);
        shader_.set("uTex", 0);
        gl::ActiveTexture(gl::TEXTURE1);
        gl::BindTexture(gl::TEXTURE_2D, fontTex_);
        gl::ActiveTexture(gl::TEXTURE0);
        gl::BindTexture(gl::TEXTURE_2D_ARRAY, r_->textureArray());
        gl::BindVertexArray(vao_);
        gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
        size_t bytes = v.size() * sizeof(V);
        if (bytes > vboCap_) {
            vboCap_ = std::max(bytes, vboCap_ * 2);
            gl::BufferData(gl::ARRAY_BUFFER, GLsizeiptr(vboCap_), nullptr, gl::STREAM_DRAW);
        }
        gl::BufferSubData(gl::ARRAY_BUFFER, 0, GLsizeiptr(bytes), v.data());
        gl::DrawArrays(gl::TRIANGLES, 0, GLsizei(v.size()));
        gl::BindVertexArray(0);
        v.clear();
    };
    layer();
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    for (Layer& L : layers_) {
        drawVerts(L.back);
        if (!L.icons.empty()) {
            gl::Clear(gl::DEPTH_BUFFER_BIT);
            mat4 o = ortho(0, float(winW_), float(winH_), 0, -2000, 2000);
            gl::Enable(gl::DEPTH_TEST);
            r_->drawBoxes(L.icons, false, &o, true);
            gl::Disable(gl::DEPTH_TEST);
            gl::Disable(gl::CULL_FACE);
            gl::Enable(gl::BLEND);
            gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
        }
        drawVerts(L.labels);
        drawVerts(L.front);
    }
    layers_.clear();
    gl::Disable(gl::BLEND);
    gl::Enable(gl::DEPTH_TEST);
    gl::Enable(gl::CULL_FACE);
}

}
