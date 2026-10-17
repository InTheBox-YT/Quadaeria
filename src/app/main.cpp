#define GLFW_INCLUDE_NONE
#include "platform/Audio.h"
#include "core/Base.h"
#include "app/Game.h"
#include "gfx/GL.h"
#include "platform/Input.h"
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace qc;

#if defined(_WIN32)
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace {
Input* g_input = nullptr;
}

int main(int argc, char** argv) {
    int frames = 0, winW = 1152, winH = 864;
    bool audioOn = true, headless = false;
    std::string shot;
    std::vector<std::string> actions;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--frames")
            frames = std::atoi(next().c_str());
        else if (a == "--screenshot")
            shot = next();
        else if (a == "--do")
            actions.push_back(next());
        else if (a == "--noaudio")
            audioOn = false;
        else if (a == "--width")
            winW = std::atoi(next().c_str());
        else if (a == "--height")
            winH = std::atoi(next().c_str());
        else if (a == "--headless")
            headless = true;
        else if (a == "--help" || a == "-h") {
            std::printf("quadaeria [--frames N] [--do ACTION]... [--screenshot out.png] [--noaudio] [--width W --height H]\n");
            return 0;
        }
    }
    if (frames > 0) headless = true;

    glfwSetErrorCallback([](int c, const char* m) { std::fprintf(stderr, "GLFW %d: %s\n", c, m); });
    if (!glfwInit()) {
        std::fprintf(stderr, "Could not initialise GLFW (no display?)\n");
        return 1;
    }
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    GLFWwindow* win = nullptr;
    const int versions[2][2] = {{4, 3}, {3, 3}};
    for (auto& v : versions) {
        if (v[0] == 4 && std::getenv("QC_GL33")) continue;
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, v[0]);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, v[1]);
        glfwSetErrorCallback(nullptr);
        win = glfwCreateWindow(winW, winH, "Quadaeria", nullptr, nullptr);
        glfwSetErrorCallback([](int c, const char* m) { std::fprintf(stderr, "GLFW %d: %s\n", c, m); });
        if (win) break;
    }
    if (!win) {
        std::fprintf(stderr, "Quadaeria needs OpenGL 3.3\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    if (!gl::load([](const char* n) { return reinterpret_cast<void*>(glfwGetProcAddress(n)); })) return 1;
    QC_LOG("OpenGL: %s / %s", (const char*)gl::GetString(gl::RENDERER), (const char*)gl::GetString(gl::VERSION));

    Input input;
    g_input = &input;
    input.attach(win);
    glfwSetKeyCallback(win, [](GLFWwindow*, int k, int, int a, int) { g_input->onKey(k, a); });
    glfwSetMouseButtonCallback(win, [](GLFWwindow*, int b, int a, int) { g_input->onMouseButton(b, a); });
    glfwSetCursorPosCallback(win, [](GLFWwindow*, double x, double y) { g_input->onCursor(x, y); });
    glfwSetScrollCallback(win, [](GLFWwindow*, double, double dy) { g_input->onScroll(dy); });
    glfwSetCharCallback(win, [](GLFWwindow*, unsigned c) { g_input->onChar(c); });

    Audio audio;
    audio.init(audioOn);

    Game game;
    game.headless = headless;
    game.script = actions;
    if (!game.init(win, &audio, &input)) {
        std::fprintf(stderr, "Quadaeria failed to start\n");
        return 1;
    }

    double last = glfwGetTime();
    int count = 0;
    while (!game.quit) {
        input.beginFrame();
        glfwPollEvents();
        if (glfwWindowShouldClose(win)) break;
        double now = glfwGetTime();
        float dt = float(now - last);
        last = now;
        if (frames > 0) dt = 1.0f / 30.0f;
        dt = std::min(dt, 0.1f);
        ++count;
        if (frames > 0 && count == frames && !shot.empty()) game.screenshotPath = shot;
        game.frame(dt);
        glfwSwapBuffers(win);
        if (frames > 0 && count >= frames) break;
    }
    game.shutdown();
    audio.shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
