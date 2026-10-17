#define GLFW_INCLUDE_NONE
#include "app/Game.h"
#include "gfx/Textures.h"
#include "world/Save.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <random>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace qc {
void Settings::load(const std::string& path) {
    KeyValues kv;
    if (!kv.load(path)) return;
    renderDist = std::clamp(int(kv.num("renderDist", renderDist)), 2, 24);
    fov = float(kv.num("fov", fov));
    sensitivity = float(kv.num("sensitivity", sensitivity));
    guiScale = int(kv.num("guiScale", guiScale));
    graphics = std::clamp(int(kv.num("graphics", graphics)), 0, 2);
    atmosphere = std::clamp(int(kv.num("atmosphere", atmosphere)), 0, 2);
    turnSway = kv.num("turnSway", turnSway) != 0;
    sprintPulse = kv.num("sprintPulse", sprintPulse) != 0;
    stepShake = kv.num("stepShake", stepShake) != 0;
    handDrift = kv.num("handDrift", handDrift) != 0;
    aspect43 = kv.num("aspect43", aspect43) != 0;
    bobbing = kv.num("bobbing", bobbing) != 0;
    vsync = kv.num("vsync", vsync) != 0;
    fullscreen = kv.num("fullscreen", fullscreen) != 0;
    renderScale = std::clamp(float(kv.num("renderScale", renderScale)), 0.4f, 1.0f);
    volume = float(kv.num("volume", volume));
    physics = std::clamp(int(kv.num("physics", physics)), 0, 2);
}

void Settings::save(const std::string& path) const {
    KeyValues kv;
    kv.set("renderDist", renderDist);
    kv.set("fov", fov);
    kv.set("sensitivity", sensitivity);
    kv.set("guiScale", guiScale);
    kv.set("graphics", graphics);
    kv.set("atmosphere", atmosphere);
    kv.set("turnSway", turnSway);
    kv.set("sprintPulse", sprintPulse);
    kv.set("stepShake", stepShake);
    kv.set("handDrift", handDrift);
    kv.set("aspect43", aspect43);
    kv.set("bobbing", bobbing);
    kv.set("vsync", vsync);
    kv.set("fullscreen", fullscreen);
    kv.set("renderScale", renderScale);
    kv.set("volume", volume);
    kv.set("physics", physics);
    kv.save(path);
}

// for realy slow pcs
void Settings::potato() {
    renderDist = 4;
    graphics = 2;
    renderScale = 0.6f;
    physics = 0;
}

bool Game::init(GLFWwindow* win, Audio* audio, Input* input) {
    win_ = win;
    audio_ = audio;
    input_ = input;
    initBlocks();
    makeDirs(dataDir() + "/worlds");
    settings_.load(dataDir() + "/options.txt");
    int layers = 0;
    std::vector<uint32_t> texels = loadTextures(layers);
    if (!renderer_.init(layers, texels)) return false;
    if (!ui_.init(&renderer_)) return false;
    sounds_.init(audio);
    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    pool_.start(std::clamp(hw - 1, 1u, 12u));
    applySettings();
    refreshWorldList();
    return true;
}

void Game::applySettings() {
    bool tear = glfwExtensionSupported("GLX_EXT_swap_control_tear") || glfwExtensionSupported("WGL_EXT_swap_control_tear");
    glfwSwapInterval(settings_.vsync ? (tear ? -1 : 1) : 0);
    if (audio_ && audio_->enabled()) audio_->setMasterVolume(settings_.volume);
    switch (settings_.physics) {
    case 0: sim_.settings = {60, 16, 700, 2500}; break;
    case 2: sim_.settings = {500, 96, 5000, 12000}; break;
    default: sim_.settings = {220, 48, 2500, 6000}; break;
    }
    settings_.fancyLeaves = settings_.graphics < 2;
    settings_.clouds = settings_.graphics < 2;
    settings_.fancyWater = settings_.graphics < 2;
    renderer_.setFancyWater(settings_.fancyWater);
    renderer_.setPointShadowLimit(settings_.graphics == 0 ? 12 : settings_.graphics == 1 ? 6 : 0);
    if (settings_.graphics == 0)
        renderer_.setShadows(2048, 64.0f, true);
    else if (settings_.graphics == 1)
        renderer_.setShadows(1024, 44.0f, false);
    else
        renderer_.setShadows(0, 0, false);
    if (settings_.graphics == 0)
        renderer_.setDetailDistance(44.0f, 60.0f);
    else if (settings_.graphics == 1)
        renderer_.setDetailDistance(34.0f, 48.0f);
    else
        renderer_.setDetailDistance(24.0f, 34.0f);
    // 4:3 by default, looks better imo
    renderer_.setAspect43(settings_.aspect43);
    int volSize = settings_.renderDist <= 5 ? 192 : 256;
    if (lightVol_.size() != volSize) lightVol_.init(&renderer_, volSize);
    settings_.save(dataDir() + "/options.txt");
}

void Game::shutdown() {
    if (worldLoaded_) closeWorld(true);
    pool_.stop();
    ui_.shutdown();
    renderer_.shutdown();
}

void Game::setScreen(Screen s) {
    screen_ = s;
    screenTime_ = clock_;
    focus_ = -1;
    message_.clear();
}

void Game::refreshWorldList() {
    worlds_.clear();
    std::string base = dataDir() + "/worlds";
    for (const std::string& f : listDirs(base)) {
        KeyValues kv;
        if (!kv.load(base + "/" + f + "/level.txt")) continue;
        WorldInfo w;
        w.folder = f;
        w.name = kv.get("name", f);
        w.lastPlayed = kv.num("lastPlayed", 0);
        worlds_.push_back(w);
    }
    std::sort(worlds_.begin(), worlds_.end(), [](const WorldInfo& a, const WorldInfo& b) { return a.lastPlayed > b.lastPlayed; });
    selectedWorld_ = worlds_.empty() ? -1 : 0;
}

bool Game::createWorld(const std::string& name, const std::string& seedText, const GenSettings& genSettings) {
    std::string folder;
    for (char c : name) folder += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    if (folder.empty()) folder = "world";
    std::string base = dataDir() + "/worlds/";
    std::string f = folder;
    for (int i = 2; fileExists(base + f); ++i) f = folder + "_" + std::to_string(i);
    uint64_t seed;
    if (seedText.empty())
        seed = std::random_device{}() ^ (uint64_t(std::time(nullptr)) << 20);
    else {
        bool numeric = !seedText.empty() &&
                       std::all_of(seedText.begin(), seedText.end(), [](char c) { return std::isdigit((unsigned char)c) || c == '-'; });
        if (numeric)
            seed = uint64_t(std::strtoll(seedText.c_str(), nullptr, 10));
        else {
            seed = 1469598103934665603ull;
            for (char c : seedText) {
                seed ^= uint8_t(c);
                seed *= 1099511628211ull;
            }
        }
    }
    makeDirs(base + f + "/region");
    KeyValues kv;
    kv.set("name", name.empty() ? "New World" : name);
    kv.set("seed", std::to_string(seed));
    kv.set("mode", 0);
    kv.set("genTerrainScale", genSettings.terrainScale);
    kv.set("genRelief", genSettings.relief);
    kv.set("genBiomeScale", genSettings.biomeScale);
    kv.set("genRivers", genSettings.rivers);
    kv.set("genCaves", genSettings.caves);
    kv.set("genVegetation", genSettings.vegetation);
    kv.set("genStructures", genSettings.structures);
    kv.set("lastPlayed", double(std::time(nullptr)));
    kv.save(base + f + "/level.txt");
    return openWorld(f);
}

bool Game::openWorld(const std::string& folder) {
    if (worldLoaded_) closeWorld(true);
    worldDir_ = dataDir() + "/worlds/" + folder;
    KeyValues kv;
    if (!kv.load(worldDir_ + "/level.txt")) {
        message_ = "Could not open world";
        return false;
    }
    worldName_ = kv.get("name", folder);
    seed_ = std::strtoull(kv.get("seed", "1").c_str(), nullptr, 10);
    genSettings_ = GenSettings{};
    genSettings_.terrainScale = float(kv.num("genTerrainScale", 1.0));
    genSettings_.relief = float(kv.num("genRelief", 1.0));
    genSettings_.biomeScale = float(kv.num("genBiomeScale", 1.0));
    genSettings_.rivers = float(kv.num("genRivers", 1.0));
    genSettings_.caves = float(kv.num("genCaves", 1.0));
    genSettings_.vegetation = float(kv.num("genVegetation", 1.0));
    genSettings_.structures = float(kv.num("genStructures", 1.0));
    gen_ = std::make_unique<Generator>(seed_, genSettings_);
    genSettings_ = gen_->settings();
    world_.seed = seed_;
    zoomAmount_ = 0.0f;
    viewMotionInit_ = false;
    viewSideSway_ = viewVertSway_ = viewRollSway_ = 0.0f;
    viewModelYawLag_ = viewModelPitchLag_ = viewModelRollLag_ = 0.0f;
    sim_.init(&world_, &renderer_, seed_, &pool_);
    streamer_.init(&world_, gen_.get(), &renderer_, &pool_, worldDir_);
    player_ = Player{};
    player_.mode = kv.num("mode", 0) == 2 ? GameMode::Spectator : GameMode::Creative;
    dayTime_ = float(kv.num("time", 0.02));
    firstSpawn_ = kv.get("px").empty();
    if (firstSpawn_) {
        int sx, sy, sz;
        gen_->findSpawn(sx, sy, sz);
        player_.spawn = vec3((sx + 0.5f) * VOX, sy * VOX + 0.2f, (sz + 0.5f) * VOX);
        player_.pos = player_.spawn;
        player_.yaw = 0.6f;
        const ItemId start[9] = {B_GRASS, B_STONE, B_PLANKS, B_GLASS, B_TNT, B_WATER, B_LAVA, B_TORCH, I_FLINT_STEEL};
        for (int i = 0; i < 9; ++i) player_.inv.slots[i] = makeStack(start[i], maxStack(start[i]));
    } else {
        player_.spawn = vec3(float(kv.num("spawnX")), float(kv.num("spawnY")), float(kv.num("spawnZ")));
        player_.pos = vec3(float(kv.num("px")), float(kv.num("py")), float(kv.num("pz")));
        player_.yaw = float(kv.num("yaw"));
        player_.pitch = float(kv.num("pitch"));
        player_.flying = kv.num("flying", 0) != 0;
        player_.inv.deserialize(kv.get("inv"));
        player_.selected = std::clamp(int(kv.num("sel", 0)), 0, 8);
    }
    loadAdventure();
    player_.fallStart = player_.pos.y;
    worldLoaded_ = true;
    loadingTime_ = 0;
    cursor_.clear();
    setScreen(Screen::Loading);
    return true;
}

void Game::saveLevel() {
    if (!worldLoaded_) return;
    KeyValues kv;
    kv.load(worldDir_ + "/level.txt");
    kv.set("name", worldName_);
    kv.set("seed", std::to_string(seed_));
    kv.set("mode", player_.spectator() ? 2 : 1);
    kv.set("time", dayTime_);
    kv.set("spawnX", player_.spawn.x);
    kv.set("spawnY", player_.spawn.y);
    kv.set("spawnZ", player_.spawn.z);
    kv.set("px", player_.pos.x);
    kv.set("py", player_.pos.y);
    kv.set("pz", player_.pos.z);
    kv.set("yaw", player_.yaw);
    kv.set("pitch", player_.pitch);
    kv.set("flying", player_.flying ? 1 : 0);
    kv.set("inv", player_.inv.serialize());
    kv.set("sel", player_.selected);
    kv.set("lastPlayed", double(std::time(nullptr)));
    kv.save(worldDir_ + "/level.txt");
    saveAdventure();
}

void Game::closeWorld(bool save) {
    lightVol_.reset();
    if (!worldLoaded_) return;
    if (save) {
        if (!cursor_.empty()) {
            player_.inv.add(cursor_.id, cursor_.count);
            cursor_.clear();
        }
        saveLevel();
    }
    pool_.stop();
    if (save) streamer_.saveAll();
    sim_.clear();
    emitterCache_.clear();
    streamer_.unloadAll(false);
    world_.listener = nullptr;
    gen_.reset();
    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    pool_.start(std::clamp(hw - 1, 1u, 12u));
    worldLoaded_ = false;
    refreshWorldList();
}

void Game::frame(float dt) {
    clock_ += dt;
    frameDt_ = std::clamp(dt, 0.0f, 0.1f);
    fpsAccum_ += dt;
    {
        double now = nowSeconds();
        if (lastWall_ > 0) {
            double ms = (now - lastWall_) * 1000.0;
            wallMs_ = wallMs_ <= 0 ? ms : wallMs_ * 0.95 + ms * 0.05;
        }
        lastWall_ = now;
    }
    if (++fpsFrames_ >= 20) {
        fps_ = fpsFrames_ / std::max(fpsAccum_, 1e-4f);
        fpsAccum_ = 0;
        fpsFrames_ = 0;
    }
    glfwGetFramebufferSize(win_, &fbW_, &fbH_);
    int ww, wh;
    glfwGetWindowSize(win_, &ww, &wh);
    float mouseScale = ww > 0 ? float(fbW_) / float(ww) : 1.0f;
    {
        float scale = worldLoaded_ ? settings_.renderScale : 1.0f;
        renderer_.resize(fbW_, fbH_, scale);
    }

    {
        double t0 = nowSeconds();
        pool_.drain(screen_ == Screen::Loading ? 256 : 96, screen_ == Screen::Loading ? 12.0 : 3.0);
        msDrain_ = msDrain_ * 0.9 + (nowSeconds() - t0) * 1000.0 * 0.1;
    }

    if (input_->pressed(Key::F11)) {
        settings_.fullscreen = !settings_.fullscreen;
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;
        if (settings_.fullscreen && vm)
            glfwSetWindowMonitor(win_, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
        else
            glfwSetWindowMonitor(win_, nullptr, 80, 80, 1280, 720, 0);
        applySettings();
    }
    if (input_->pressed(Key::F2)) {
        makeDirs(dataDir() + "/screenshots");
        char buf[64];
        std::time_t t = std::time(nullptr);
        std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", std::localtime(&t));
        screenshotPath = dataDir() + "/screenshots/" + buf + ".png";
    }

    bool inGame = worldLoaded_ && screen_ != Screen::Loading &&
                  (screen_ == Screen::Playing || screen_ == Screen::Pause || screen_ == Screen::Inventory ||
                   (screen_ == Screen::Options && optionsReturn_ == Screen::Pause) ||
                   (screen_ == Screen::Controls && optionsReturn_ == Screen::Pause));
    if (!headless && screen_ == Screen::Playing && !glfwGetWindowAttrib(win_, GLFW_FOCUSED)) setScreen(Screen::Pause);
    input_->setCursorLocked(screen_ == Screen::Playing && !headless);
    if (!script.empty()) runScript();

    if (worldLoaded_ && screen_ == Screen::Loading) {
        loadingTime_ += dt;
        streamer_.update(player_.pos, std::min(settings_.renderDist, 3), settings_.fancyLeaves, clock_);
        if (streamer_.areaReady(player_.pos, 1)) {
            if (firstSpawn_) {
                firstSpawn_ = false;
                int cx = cellOf(player_.pos.x), cz = cellOf(player_.pos.z);
                bool found = false;
                for (int r = 0; r <= 24 && !found; r += 2)
                    for (int dz = -r; dz <= r && !found; dz += 2)
                        for (int dx = -r; dx <= r && !found; dx += 2) {
                            if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                            int x = cx + dx, z = cz + dz;
                            int top = world_.heightAtTop(x, z);
                            uint8_t g = world_.get(x, top - 1, z);
                            if (g != B_GRASS && g != B_SAND && g != B_SNOWY_GRASS && g != B_DIRT) continue;
                            player_.pos = vec3((x + 0.5f) * VOX, top * VOX + 0.01f, (z + 0.5f) * VOX);
                            if (boxHitsWorld(world_, player_.box())) continue;
                            found = true;
                        }
                player_.spawn = player_.pos;
            }
            int x = cellOf(player_.pos.x), z = cellOf(player_.pos.z);
            AABB b = player_.box();
            if (boxHitsWorld(world_, b)) {
                int top = world_.heightAtTop(x, z);
                player_.pos.y = top * VOX + 0.01f;
                if (player_.spawn.y < player_.pos.y &&
                    lengthSq(vec3(player_.spawn.x - player_.pos.x, 0, player_.spawn.z - player_.pos.z)) < 1)
                    player_.spawn.y = player_.pos.y;
            }
            player_.fallStart = player_.pos.y;
            setScreen(Screen::Playing);
        }
    } else if (inGame) {
        double tu = nowSeconds();
        bool simulate = screen_ != Screen::Pause && screen_ != Screen::Options && screen_ != Screen::Controls;
        if (simulate) updatePlaying(dt, screen_ == Screen::Playing);
        double ts = nowSeconds();
        streamUpdateAccum_ += dt;
        const float streamStep = 1.0f / 30.0f;
        if (streamUpdateAccum_ >= streamStep) {
            streamUpdateAccum_ = std::fmod(streamUpdateAccum_, streamStep);
            streamer_.update(player_.pos, settings_.renderDist, settings_.fancyLeaves, clock_);
        }
        msStream_ = msStream_ * 0.9 + (nowSeconds() - ts) * 1000.0 * 0.1;
        autosave_ += dt;
        if (autosave_ > 90.0f) {
            autosave_ = 0;
            saveLevel();
            streamer_.saveAll();
        }
        msUpdate_ = msUpdate_ * 0.9 + (nowSeconds() - tu) * 1000.0 * 0.1;
    }

    double tr = nowSeconds();
    if (worldLoaded_ && screen_ != Screen::Loading) {
        double tl = nowSeconds();
        lightVolUpdateAccum_ += dt;
        const float lightStep = 1.0f / 30.0f;
        if (lightVolUpdateAccum_ >= lightStep) {
            lightVolUpdateAccum_ = std::fmod(lightVolUpdateAccum_, lightStep);
            lightVol_.update(world_, player_.eye(), settings_.physics == 0 ? 3 : 4);
        } else
            lightVol_.uploadsLastFrame = 0;
        msLightVol_ = msLightVol_ * 0.9 + (nowSeconds() - tl) * 1000.0 * 0.1;
        renderWorld();
    } else {
        gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
        gl::Viewport(0, 0, fbW_, fbH_);
        gl::ClearColor(0.1f, 0.1f, 0.12f, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
    }

    UIInput ui;
    ui.mouse = input_->mousePosition() * mouseScale;
    ui.down = input_->mouseDown(Mouse::Left);
    ui.pressed = input_->mousePressed(Mouse::Left);
    ui.released = input_->mouseReleased(Mouse::Left);
    ui.rightPressed = input_->mousePressed(Mouse::Right);
    ui.scroll = input_->scroll();
    ui.typed = input_->typed();
    ui.backspace = input_->repeatPressed(Key::Backspace);
    ui.enter = input_->pressed(Key::Enter);
    ui.escape = input_->pressed(Key::Escape);
    ui.keyUp = input_->repeatPressed(Key::Up);
    ui.keyDown = input_->repeatPressed(Key::Down);
    if (fakeHover_) ui.mouse = fakeMouse_;
    if (fakeMouseFrames_ > 0) {
        ui.mouse = fakeMouse_;
        ui.pressed = fakeMouseFrames_ == 3;
        ui.down = fakeMouseFrames_ == 3 || fakeMouseFrames_ == 2;
        ui.released = fakeMouseFrames_ == 2;
        --fakeMouseFrames_;
    }
    ui_.begin(fbW_, fbH_, ui, settings_.guiScale, true);
    switch (screen_) {
    case Screen::Title:
        drawBackground(false);
        drawTitle();
        break;
    case Screen::Worlds:
        drawBackground(true);
        drawWorlds();
        break;
    case Screen::Create:
        drawBackground(true);
        drawCreate();
        break;
    case Screen::CreateWorldGen:
        drawBackground(true);
        drawCreateWorldGen();
        break;
    case Screen::Confirm:
        drawBackground(true);
        drawConfirm();
        break;
    case Screen::Loading:
        drawBackground(true);
        drawLoading();
        break;
    case Screen::Playing:
        if (!hideHud_) drawHUD();
        break;
    case Screen::Pause: drawPause(); break;
    case Screen::Inventory: drawInventory(); break;
    case Screen::Options:
        if (!worldLoaded_) drawBackground(true);
        drawOptions();
        break;
    case Screen::Controls:
        if (!worldLoaded_) drawBackground(true);
        drawControls();
        break;
    }
    if (showDebug_ && worldLoaded_ && screen_ == Screen::Playing) {
        ui_.layer();
        drawDebug();
    }
    ui_.flush();
    if (ui_.sfxHover) sounds_.uiTick();
    if (ui_.sfxPress) sounds_.uiPress();
    ui_.sfxHover = ui_.sfxPress = false;
    if (worldLoaded_ && screen_ != Screen::Loading) renderer_.gpuFrameEnd();
    msRender_ = msRender_ * 0.9 + (nowSeconds() - tr) * 1000.0 * 0.1;

    if (!screenshotPath.empty()) {
        std::vector<uint8_t> px(size_t(fbW_) * fbH_ * 4), fl(px.size());
        gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
        gl::ReadPixels(0, 0, fbW_, fbH_, gl::RGBA, gl::UNSIGNED_BYTE, px.data());
        for (int y = 0; y < fbH_; ++y) std::memcpy(&fl[size_t(y) * fbW_ * 4], &px[size_t(fbH_ - 1 - y) * fbW_ * 4], size_t(fbW_) * 4);
        for (size_t i = 3; i < fl.size(); i += 4) fl[i] = 255;
        stbi_write_png(screenshotPath.c_str(), fbW_, fbH_, 4, fl.data(), fbW_ * 4);
        std::printf("[quadaeria] screenshot: %s\n", screenshotPath.c_str());
        std::fflush(stdout);
        screenshotPath.clear();
    }
}

void Game::updatePlaying(float dt, bool controls) {
    dt = std::min(dt, 0.1f);
    dayTime_ += dt / 1200.0f;
    if (dayTime_ >= 1.0f) dayTime_ -= 1.0f;
    float sunAngle = dayTime_ * 2 * PI;
    float daylight = std::clamp(std::sin(sunAngle) * 2.2f + 0.45f, 0.0f, 1.0f);
    bool night = daylight < 0.25f;

    PlayerInput in;
    if (controls) {
        float sens = 0.0022f * settings_.sensitivity;
        sens *= (1.0f - zoomAmount_ * 0.62f);
        vec2 md = input_->mouseDelta();
        in.yawDelta = md.x * sens;
        in.pitchDelta = -md.y * sens;
        if (input_->down(Key::W)) in.move.z += 1;
        if (input_->down(Key::S)) in.move.z -= 1;
        if (input_->down(Key::D)) in.move.x += 1;
        if (input_->down(Key::A)) in.move.x -= 1;
        in.jump = input_->down(Key::Space);
        in.jumpPressed = input_->pressed(Key::Space);
        in.sneak = input_->down(Key::LeftShift);
        if (input_->pressed(Key::W)) {
            if (float(clock_) - lastWTap_ < 0.3f) sprintToggle_ = true;
            lastWTap_ = float(clock_);
        }
        if (!input_->down(Key::W)) sprintToggle_ = false;
        in.sprint = input_->down(Key::LeftControl) || sprintToggle_;
        if (scriptMoveT_ > 0) {
            scriptMoveT_ -= dt;
            in.move = vec3(scriptMove_.x, 0, scriptMove_.z);
            in.sprint = scriptMove_.y > 0.5f;
            in.jump = scriptMove_.y < -0.5f;
            in.yawDelta += scriptYawRate_ * dt;
        }
        for (int i = 0; i < 9; ++i)
            if (input_->pressed(Key::Num1 + i)) player_.selected = i;
        if (input_->pressed(Key::X)) std::swap(player_.selected, quickPrev_);
        float sc = input_->scroll();
        if (player_.spectator()) {
            if (sc != 0) player_.specSpeed = std::clamp(player_.specSpeed * (sc > 0 ? 1.25f : 0.8f), 2.0f, 80.0f);
        } else if (sc != 0)
            player_.selected = (player_.selected + (sc < 0 ? 1 : -1) + 9) % 9;
        // f4 flips between creative and spectator
        if (input_->pressed(Key::F4)) {
            if (player_.spectator()) {
                player_.mode = GameMode::Creative;
                player_.vel = {};
                player_.fallStart = player_.pos.y;
                player_.flying = true;
            } else
                player_.mode = GameMode::Spectator;
            sounds_.uiPop(player_.spectator() ? 0.8f : 1.2f);
        }
        if (input_->pressed(Key::R)) { miniMode_ = !miniMode_; }
        if (input_->pressed(Key::Q) && !player_.spectator()) dropHeld(input_->down(Key::LeftControl));
        if (input_->pressed(Key::F1)) hideHud_ = !hideHud_;
        if (input_->pressed(Key::F3)) showDebug_ = !showDebug_;
        if (input_->pressed(Key::F))
            toggleRide(input_->down(Key::LeftShift));
        else if (input_->pressed(Key::LeftShift) && ridingSkiff_ >= 0)
            toggleRide();
        if (input_->pressed(Key::E) && !player_.spectator()) setScreen(Screen::Inventory);
        if (input_->pressed(Key::Escape)) setScreen(Screen::Pause);
        if (input_->pressed(Key::G)) { dayTime_ = (dayTime_ > 0.5f) ? 0.02f : 0.6f; }
    } else if (screen_ == Screen::Inventory) {
        if (input_->pressed(Key::E) || input_->pressed(Key::Escape)) closeInventory();
    }
    if (player_.selected != lastSelected_) {
        if (quickPrev_ != player_.selected) quickPrev_ = lastSelected_;
        player_.swapAnim = 1.0f;
        sounds_.ui();
        heldNameTimer_ = 2.0f;
        lastSelected_ = player_.selected;
    }
    heldNameTimer_ = std::max(0.0f, heldNameTimer_ - dt);

    int steps = std::max(1, int(std::ceil(dt * 120.0f)));
    float h = dt / float(steps);
    bool stepped = false;
    for (int stepIndex = 0; stepIndex < steps; ++stepIndex) {
        PlayerInput pi = in;
        if (stepIndex > 0) {
            pi.yawDelta = pi.pitchDelta = 0;
            pi.jumpPressed = false;
        }
        if (ridingSkiff_ < 0 && ridingRope_ < 0)
            stepped |= updatePlayer(player_, world_, pi, h, float(clock_));
        else {
            player_.yaw += pi.yawDelta;
            player_.pitch = std::clamp(player_.pitch + pi.pitchDelta, -1.55f, 1.55f);
        }
        adventureStep(h, pi);
    }
    if (stepped) {
        uint8_t below = world_.get(cellOf(player_.pos.x), cellOf(player_.pos.y - 0.1f), cellOf(player_.pos.z));
        if (below) sim_.sounds.push_back({Snd::Step, player_.pos, 0.6f, 0.9f + float(std::fmod(clock_ * 7.3, 0.2)), def(below).sound});
    }
    {
        uint8_t under = world_.get(cellOf(player_.pos.x), cellOf(player_.pos.y - 0.1f), cellOf(player_.pos.z));
        if (player_.landImpact > 0) {
            float imp = player_.landImpact;
            if (under && !player_.inWater) {
                sim_.dustPuff(player_.pos + vec3(0, 0.05f, 0), under, imp > 14 ? 14 : imp > 8 ? 8 : 4, 0.9f + imp * 0.05f);
                if (imp > 5) sim_.blockParticles(player_.pos + vec3(0, 0.1f, 0), under, imp > 12 ? 6 : 3, 0.3f, 1.5f + imp * 0.1f);
                sim_.sounds.push_back({imp > 12 ? Snd::Thud : Snd::Step, player_.pos, std::min(1.0f, 0.4f + imp * 0.04f),
                                       imp > 12 ? 1.0f : 0.8f, def(under).sound});
            }
            player_.landImpact = 0;
        }
        if (player_.jumpEvent) {
            player_.jumpEvent = false;
            if (under && !player_.inWater) {
                sim_.dustPuff(player_.pos + vec3(0, 0.05f, 0), under, player_.sprinting ? 4 : 2, 0.6f);
                sim_.sounds.push_back({Snd::Step, player_.pos, 0.35f, 1.25f, def(under).sound});
            }
        }
        if (player_.sprinting && player_.onGround && under && rng01() < dt * 9.0f) {
            vec3 back = -vec3(std::sin(player_.yaw), 0, -std::cos(player_.yaw)) * 0.3f;
            sim_.dustPuff(player_.pos + back + vec3(0, 0.05f, 0), under, 1, 0.5f);
        }
    }
    if (player_.inLeaves > 0.02f) {
        float sp = length(vec3(player_.vel.x, player_.vel.y * 0.5f, player_.vel.z));
        rustleT_ -= dt * sp * (0.6f + player_.inLeaves * 2.0f);
        if (rustleT_ <= 0) {
            rustleT_ = 0.45f;
            sim_.rustle(player_.pos + vec3(0, 1.0f, 0), player_.leafBlock, player_.vel * 0.2f, 0.8f);
        }
    }
    if (player_.pos.y < -60) {
        player_.pos.y = -60;
        player_.vel.y = 0;
        player_.flying = true;
    }

    if (controls && !player_.spectator())
        interact(dt);
    else if (player_.spectator()) {
        target_.hit = false;
        targetEnt_ = -1;
        targetCells_.clear();
        previewCells_.clear();
    }
    swing_ = std::max(0.0f, swing_ - dt * 3.5f);

    sim_.update(dt, player_, daylight, night);
    updateClimate(dt);
    for (int64_t k : sim_.relight) streamer_.requestRelight(k);
    sim_.relight.clear();
    vec3 ear = player_.eye();
    if (audio_ && audio_->enabled()) audio_->setListener(ear, player_.look());
    for (const SoundEvent& e : sim_.sounds) sounds_.play(e, ear);
    sim_.sounds.clear();
    for (auto& pk : sim_.pickups) {
        bool merged = false;
        for (auto& f : feed_)
            if (f.item == pk.item && f.age < 2.0f) {
                f.count += pk.count;
                f.age = 0;
                merged = true;
                break;
            }
        if (!merged) feed_.push_back({pk.item, pk.count, 0});
        if (feed_.size() > 6) feed_.erase(feed_.begin());
    }
    sim_.pickups.clear();
    for (auto& f : feed_) f.age += dt;
    feed_.erase(std::remove_if(feed_.begin(), feed_.end(), [](const FeedLine& f) { return f.age > 3.2f; }), feed_.end());
    for (int i = 0; i < 9; ++i) {
        const ItemStack& st = player_.inv.slots[i];
        if (st.id != lastId_[i] ? !st.empty() : st.count > lastCount_[i]) slotPop_[i] = 1.0f;
        lastId_[i] = st.id;
        lastCount_[i] = st.count;
        slotPop_[i] = std::max(0.0f, slotPop_[i] - dt * 4.5f);
    }
}

std::vector<ivec3> Game::breakCells(const RayHit& h, bool mini) const {
    std::vector<ivec3> out;
    if (!h.hit) return out;
    if (h.y < 0 || h.y >= WH) return out;
    if (mini || isSmallBlock(h.block)) {
        out.push_back({h.x, h.y, h.z});
        return out;
    }
    int hp[3] = {h.x, h.y, h.z}, n[3] = {h.nx, h.ny, h.nz};
    int axis = n[0] ? 0 : n[2] ? 2 : 1;
    int sgn = n[axis] ? n[axis] : 1;
    int lo[3], hi[3];
    for (int a = 0; a < 3; ++a) {
        if (a == axis) {
            lo[a] = std::min(hp[a], hp[a] - sgn);
            hi[a] = std::max(hp[a], hp[a] - sgn);
        } else {
            lo[a] = hp[a] - floorMod(hp[a], 2);
            hi[a] = lo[a] + 1;
        }
    }
    if (orientable(h.block) || h.block == B_TNT) {
        textureTileOrigin(h.block, world_.metaAt(h.x, h.y, h.z), h.x, h.y, h.z, lo);
        for (int a = 0; a < 3; ++a) hi[a] = lo[a] + 1;
    }
    for (int y = lo[1]; y <= hi[1]; ++y)
        for (int z = lo[2]; z <= hi[2]; ++z)
            for (int x = lo[0]; x <= hi[0]; ++x) {
                if (y < 0 || y >= WH) continue;
                uint8_t b = world_.get(x, y, z);
                const BlockDef& d = def(b);
                if (b == B_AIR || d.fluid) continue;
                out.push_back({x, y, z});
            }
    return out;
}

std::vector<ivec3> Game::placeCells(const RayHit& h, bool mini, ItemId item) const {
    std::vector<ivec3> out;
    if (!h.hit || !isBlockItem(item)) return out;
    int p[3] = {h.x + h.nx, h.y + h.ny, h.z + h.nz}, n[3] = {h.nx, h.ny, h.nz};
    if (def(h.block).replaceable && !def(h.block).fluid) {
        p[0] = h.x;
        p[1] = h.y;
        p[2] = h.z;
    }
    const BlockDef& d = def(BlockId(item));
    bool single = mini || isSmallBlock(BlockId(item));
    int tile[3] = {h.x - floorMod(h.x, 2), h.y - floorMod(h.y, 2), h.z - floorMod(h.z, 2)};
    if ((orientable(h.block) || h.block == B_TNT) && !def(h.block).sideTopAligned)
        textureTileOrigin(h.block, world_.metaAt(h.x, h.y, h.z), h.x, h.y, h.z, tile);
    int lo[3], hi[3];
    int axis = n[0] ? 0 : n[2] ? 2 : 1;
    int sgn = n[axis] ? n[axis] : 1;
    for (int a = 0; a < 3; ++a) {
        if (single) {
            lo[a] = hi[a] = p[a];
            continue;
        }
        if (a == axis) {
            lo[a] = std::min(p[a], p[a] + sgn);
            hi[a] = std::max(p[a], p[a] + sgn);
        } else {
            lo[a] = tile[a];
            hi[a] = lo[a] + 1;
        }
    }
    AABB pb = player_.box();
    for (int y = lo[1]; y <= hi[1]; ++y)
        for (int z = lo[2]; z <= hi[2]; ++z)
            for (int x = lo[0]; x <= hi[0]; ++x) {
                if (y < 0 || y >= WH || !world_.loaded(x, z)) continue;
                uint8_t cur = world_.get(x, y, z);
                if (!def(cur).replaceable || cur == BlockId(item)) continue;
                AABB cb{{x * VOX, y * VOX, z * VOX}, {(x + 1) * VOX, (y + 1) * VOX, (z + 1) * VOX}};
                if (d.solid && cb.overlaps(pb)) continue;
                bool blocked = false;
                if (blocked) continue;
                if (isPlantDetail(BlockId(item)) && !world_.plantSupported(x, y, z, BlockId(item))) continue;
                if (d.render == Render::Model) {
                    const ivec3 adj[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    bool attached = false;
                    for (auto n : adj)
                        if (world_.collidesAt(x + n.x, y + n.y, z + n.z)) attached = true;
                    if (!attached) continue;
                }
                out.push_back({x, y, z});
            }
    if (item == B_LONG_GRASS && out.size() == 1) {
        auto p = out[0];
        if (p.y + 1 >= WH || !def(world_.get(p.x, p.y + 1, p.z)).replaceable)
            out.clear();
        else
            out.push_back({p.x, p.y + 1, p.z});
    }
    if (!single && out.size() != 8) out.clear();
    return out;
}

void Game::dropHeld(bool all) {
    ItemStack& s = player_.held();
    if (s.empty()) return;
    int n = all ? s.count : (isBlockItem(s.id) ? std::min(8, s.count) : 1);
    vec3 dir = player_.look();
    sim_.spawnItem(player_.eye() - vec3(0, 0.3f, 0) + dir * 0.3f, s.id, n, dir * 6.0f + vec3(0, 1.5f, 0));
    s.count -= n;
    if (s.count <= 0) s.clear();
}

}
