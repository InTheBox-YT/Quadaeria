#define GLFW_INCLUDE_NONE
#include "platform/Input.h"
#include <GLFW/glfw3.h>
#include <cmath>

namespace qc {
void Input::attach(GLFWwindow* window) { window_ = window; }

void Input::beginFrame() {
    pressed_.fill(false);
    released_.fill(false);
    mousePressed_.fill(false);
    mouseReleased_.fill(false);
    repeat_.fill(false);
    typed_.clear();
    mouseDelta_ = {0, 0};
    scroll_ = 0;
}

void Input::onKey(int key, int action) {
    if (!valid(key)) return;
    if (action == GLFW_PRESS) {
        keys_[key] = true;
        pressed_[key] = true;
    } else if (action == GLFW_RELEASE) {
        keys_[key] = false;
        released_[key] = true;
    } else if (action == GLFW_REPEAT)
        repeat_[key] = true;
}

void Input::onMouseButton(int b, int action) {
    if (b < 0 || b >= kButtons) return;
    if (action == GLFW_PRESS) {
        mouse_[b] = true;
        mousePressed_[b] = true;
    } else if (action == GLFW_RELEASE) {
        mouse_[b] = false;
        mouseReleased_[b] = true;
    }
}

void Input::onCursor(double x, double y) {
    vec2 p{float(x), float(y)};
    if (haveCursor_) mouseDelta_ += p - mousePos_;
    mousePos_ = p;
    haveCursor_ = true;
}

void Input::setCursorLocked(bool locked) {
    if (locked == locked_) return;
    locked_ = locked;
    if (!window_) return;
    glfwSetInputMode(window_, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, locked ? GLFW_TRUE : GLFW_FALSE);
    haveCursor_ = false;
}

}
