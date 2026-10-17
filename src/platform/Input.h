#pragma once
#include "core/Math.h"
#include <array>
#include <vector>

struct GLFWwindow;

namespace qc {
namespace Key {
enum : int {
    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus = 45,
    Period = 46,
    Num0 = 48,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    A = 65,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Escape = 256,
    Enter,
    Tab,
    Backspace,
    Insert,
    Delete,
    Right,
    Left,
    Down,
    Up,
    F1 = 290,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    LeftShift = 340,
    LeftControl,
    LeftAlt,
    LeftSuper,
    RightShift,
    RightControl,
    RightAlt,
};
}
namespace Mouse {
enum : int { Left = 0, Right = 1, Middle = 2 };
}

class Input {
public:
    static constexpr int kKeys = 349;
    static constexpr int kButtons = 8;

    void attach(GLFWwindow* window);
    void beginFrame();

    bool down(int key) const { return valid(key) && keys_[key]; }
    bool pressed(int key) const { return valid(key) && pressed_[key]; }
    bool released(int key) const { return valid(key) && released_[key]; }

    bool mouseDown(int b) const { return b >= 0 && b < kButtons && mouse_[b]; }
    bool mousePressed(int b) const { return b >= 0 && b < kButtons && mousePressed_[b]; }
    bool mouseReleased(int b) const { return b >= 0 && b < kButtons && mouseReleased_[b]; }
    const std::vector<unsigned>& typed() const { return typed_; }
    bool repeatPressed(int key) const { return valid(key) && (pressed_[key] || repeat_[key]); }
    vec2 mouseDelta() const { return mouseDelta_; }
    vec2 mousePosition() const { return mousePos_; }
    float scroll() const { return scroll_; }

    void setCursorLocked(bool locked);

    void onKey(int key, int action);
    void onMouseButton(int button, int action);
    void onCursor(double x, double y);
    void onScroll(double dy) { scroll_ += float(dy); }
    void onChar(unsigned c) { typed_.push_back(c); }

private:
    static bool valid(int k) { return k >= 0 && k < kKeys; }
    GLFWwindow* window_ = nullptr;
    std::array<bool, kKeys> keys_{}, pressed_{}, released_{};
    std::array<bool, kButtons> mouse_{}, mousePressed_{}, mouseReleased_{};
    std::array<bool, kKeys> repeat_{};
    std::vector<unsigned> typed_;
    vec2 mousePos_, mouseDelta_;
    bool haveCursor_ = false, locked_ = false;
    float scroll_ = 0;
};

}
