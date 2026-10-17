#pragma once
#include "world/Block.h"
#include "gfx/Renderer.h"
#include <string>
#include <vector>

namespace qc {
struct UIInput {
    vec2 mouse;
    bool down = false, pressed = false, released = false, rightPressed = false;
    float scroll = 0;
    std::vector<unsigned> typed;
    bool backspace = false, enter = false, escape = false, tab = false, keyUp = false, keyDown = false;
};

namespace col {
constexpr uint32_t rgba(int r, int g, int b, int a = 255) {
    return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}
// paper and ink, keep it simple
constexpr uint32_t White = rgba(250, 247, 238), Black = rgba(0, 0, 0);
constexpr uint32_t Paper = rgba(236, 230, 214), PaperLit = rgba(248, 244, 232), PaperDim = rgba(220, 212, 192),
                   PaperDeep = rgba(190, 180, 156);
constexpr uint32_t Ink = rgba(48, 44, 38), InkSoft = rgba(118, 110, 96), Red = rgba(170, 60, 48);
constexpr uint32_t SkyTop = rgba(112, 160, 210), SkyLow = rgba(198, 222, 238);
}

class UI {
public:
    bool init(Renderer* r);
    void shutdown();
    void begin(int winW, int winH, const UIInput& in, int scaleSetting, bool safe43 = false);
    void layer();
    void flush();

    int scale = 2;
    float w = 0, h = 0;
    vec2 mouse;
    UIInput in;
    bool consumedClick = false;

    void rect(float x, float y, float rw, float rh, uint32_t color);
    void gradient(float x, float y, float rw, float rh, uint32_t top, uint32_t bottom);
    void inset(float x, float y, float rw, float rh, bool hot = false);
    void plate(float x, float y, float rw, float rh, bool hot = false);
    void panel(float x, float y, float rw, float rh);
    void slot(float x, float y, bool highlight = false);
    void text(float x, float y, const std::string& s, uint32_t color = col::White, bool shadow = true, float size = 1);
    void heading(float cx, float y, const std::string& s);
    bool sfxHover = false, sfxPress = false;
    float time() const { return time_; }
    float dt() const { return dt_; }
    void textCentered(float cx, float y, const std::string& s, uint32_t color = col::White, float size = 1);
    float textWidth(const std::string& s, float size = 1) const;
    void texQuad(float x, float y, float qw, float qh, int layer, uint32_t color = col::White);
    void item(float x, float y, float size, ItemId id, int count = 0);

    bool hovered(float x, float y, float rw, float rh) const {
        return mouse.x >= x && mouse.x < x + rw && mouse.y >= y && mouse.y < y + rh;
    }
    bool button(const std::string& label, float x, float y, float bw, float bh = 20, bool enabled = true);
    bool menuEntry(const std::string& label, float x, float y, float w, float size = 2, bool selected = false);
    static float menuEntryHeight(float size) { return 8 * size + 8; }
    bool slider(const std::string& label, float& v, float lo, float hi, float x, float y, float sw, float sh = 20);
    bool textField(std::string& s, int& focusId, int id, float x, float y, float fw, float fh = 20, size_t maxLen = 32);

private:
    struct V {
        float x, y, u, v, layer;
        uint32_t color;
    };
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float layer, uint32_t c0, uint32_t c1,
              bool horizontal = false);
    Renderer* r_ = nullptr;
    Shader shader_;
    GLuint vao_ = 0, vbo_ = 0, fontTex_ = 0;
    size_t vboCap_ = 0;
    std::vector<V> back_, front_, labels_;
    std::vector<BoxInstance> icons_;
    struct Layer {
        std::vector<V> back, labels, front;
        std::vector<BoxInstance> icons;
    };
    std::vector<Layer> layers_;
    int adv_[128] = {};
    int winW_ = 1, winH_ = 1;
    float safeOriginX_ = 0;
    int activeSlider_ = -1, sliderSeq_ = 0;
    struct Anim {
        uint32_t key;
        float v;
        unsigned frame;
    };
    std::vector<Anim> anim_;
    unsigned frame_ = 0;
    float lastTime_ = 0, dt_ = 0.016f;

public:
    float ease(float x, float y, bool on, float rate = 14.0f);

private:
    float time_ = 0;
    uint32_t hoverId_ = 0, prevHoverId_ = 0;
    uint32_t pressOwner_ = 0;
    bool clicked(uint32_t id, bool hov);
    void drawText(float x, float y, const std::string& s, uint32_t top, uint32_t bottom, float size);
};

}
