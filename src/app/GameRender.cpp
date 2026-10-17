#include "app/Game.h"
#include "sim/Models.h"
#include <algorithm>
#include <cmath>

namespace qc {
namespace {
struct Xf {
    mat3 R;
    vec3 t;
    Xf operator*(const Xf& b) const { return {R * b.R, R * b.t + t}; }
};
Xf T(vec3 v) { return {mat3(), v}; }
Xf RY(float a) {
    mat3 m;
    m.m[0][0] = std::cos(a);
    m.m[0][2] = std::sin(a);
    m.m[2][0] = -std::sin(a);
    m.m[2][2] = std::cos(a);
    return {m, {}};
}
Xf RX(float a) {
    mat3 m;
    m.m[1][1] = std::cos(a);
    m.m[1][2] = -std::sin(a);
    m.m[2][1] = std::sin(a);
    m.m[2][2] = std::cos(a);
    return {m, {}};
}
Xf RZ(float a) {
    mat3 m;
    m.m[0][0] = std::cos(a);
    m.m[0][1] = -std::sin(a);
    m.m[1][0] = std::sin(a);
    m.m[1][1] = std::cos(a);
    return {m, {}};
}
vec3 mix3(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
}

void Game::computeSky(SkyState& s) const {
    vec3 head = player_.eye();
    s.skyExposure = float(world_.lightAt(cellOf(head.x), cellOf(head.y), cellOf(head.z)) >> 4) / 15.0f;
    bool roof = false;
    for (int y = cellOf(head.y) + 1; y < WH; ++y) {
        const auto& d = def(world_.get(cellOf(head.x), y, cellOf(head.z)));
        if (d.light == LightKind::Opaque && d.render != Render::Leaves) {
            roof = true;
            break;
        }
    }
    if (!roof) s.skyExposure = 1.0f;
    float a = dayTime_ * 2 * PI;
    float sunH = std::sin(a);
    float daylight = std::clamp(sunH * 2.2f + 0.45f, 0.0f, 1.0f);
    s.sunLight = 0.085f + 0.915f * daylight;
    vec3 dayTop(0.20f, 0.43f, 0.70f), dayHor(0.68f, 0.81f, 0.88f);
    vec3 nightTop(0.014f, 0.025f, 0.055f), nightHor(0.040f, 0.060f, 0.098f);
    s.skyTop = mix3(nightTop, dayTop, daylight);
    s.skyHorizon = mix3(nightHor, dayHor, daylight);
    float glow = std::exp(-sunH * sunH * 28.0f) * std::clamp(daylight * 2.0f, 0.0f, 1.0f);
    s.skyHorizon = mix3(s.skyHorizon, vec3(1.0f, 0.53f, 0.27f), glow * 0.65f);
    s.fogColor = mix3(s.skyHorizon, s.skyTop, 0.2f) * 0.92f;
    s.sunDir = normalize(vec3(std::cos(a), sunH, 0.28f));
    s.starAlpha = std::clamp(1.0f - daylight * 2.5f, 0.0f, 1.0f);
    s.fogEnd = std::max(24.0f, (settings_.renderDist - 1.0f) * CS * VOX);
    const float fogStartFrac[3] = {0.70f, 0.62f, 0.50f};
    s.fogStart = s.fogEnd * fogStartFrac[std::clamp(settings_.atmosphere, 0, 2)];
    s.clouds = settings_.clouds && settings_.graphics < 2;
    s.cloudHeight = 124.0f;
    float sunUp = std::clamp((sunH + 0.04f) / 0.2f, 0.0f, 1.0f);
    sunUp = sunUp * sunUp * (3 - 2 * sunUp);
    vec3 sunTint = mix3(vec3(1.12f, 0.98f, 0.8f), vec3(1.04f, 1.0f, 0.93f), std::clamp((sunH - 0.04f) / 0.4f, 0.0f, 1.0f));
    if (sunH > -0.04f) {
        s.lightDir = s.sunDir;
        s.sunCol = sunTint * (0.49f * sunUp);
        s.castShadows = sunUp > 0.05f;
    } else {
        s.lightDir = s.sunDir * -1.0f;
        float moonUp = std::clamp((-sunH - 0.04f) / 0.25f, 0.0f, 1.0f);
        s.sunCol = vec3(0.040f, 0.055f, 0.086f) * moonUp;
        s.castShadows = false;
    }
    s.ambCol = mix3(vec3(0.055f, 0.070f, 0.105f), vec3(0.50f, 0.55f, 0.61f), daylight);
    const float baseFog[3] = {0.0009f, 0.00135f, 0.0022f};
    const float glowFog[3] = {0.0024f, 0.0032f, 0.0042f};
    int atm = std::clamp(settings_.atmosphere, 0, 2);
    s.fogDensity = baseFog[atm] + glowFog[atm] * glow + 0.0006f * (1.0f - daylight);
    float temp = temp_, humid = humid_;
    float cold = std::clamp((0.4f - temp) * 3.0f, 0.0f, 1.0f);
    float dry = std::clamp((temp - 0.5f) * 3.0f, 0.0f, 1.0f) * std::clamp((0.5f - humid) * 3.0f, 0.0f, 1.0f);
    float wet = std::clamp((humid - 0.62f) * 3.0f, 0.0f, 1.0f);
    vec3 haze = mix3(vec3(0.55f, 0.72f, 0.75f), vec3(0.63f, 0.76f, 0.92f), cold);
    haze = mix3(haze, vec3(0.85f, 0.66f, 0.43f), dry);
    haze = mix3(haze, vec3(0.35f, 0.61f, 0.49f), wet);
    vec3 climateFog = haze * (0.68f + daylight * 0.32f);
    s.fogColor = mix3(s.fogColor, climateFog, 0.26f);
    s.fogDensity += 0.0017f * wet + 0.0007f * cold + 0.00055f * dry;
    if (player_.headInWater) {
        s.underwater = true;
        float sky = float(world_.lightAt(cellOf(player_.eye().x), cellOf(player_.eye().y), cellOf(player_.eye().z)) >> 4) / 15.0f;
        vec3 water = mix3(vec3(0.035f, 0.18f, 0.25f), vec3(0.10f, 0.22f, 0.13f), wet);
        s.fogColor = water * (s.sunLight * sky) + vec3(0.006f, 0.009f, 0.012f);
        s.fogStart = 1.0f;
        s.fogEnd = 22.0f;
        s.fogDensity = 0.09f;
    }
    vec3 eye = player_.eye();
    if (world_.get(cellOf(eye.x), cellOf(eye.y), cellOf(eye.z)) == B_LAVA) {
        s.inLava = true;
        s.fogColor = vec3(0.94f, 0.24f, 0.035f);
        s.fogStart = 0;
        s.fogEnd = 2.0f;
        s.fogDensity = 1.5f;
    }
}

void Game::drawSelection() {
    if (!target_.hit || targetCells_.empty() || hideHud_) return;
    ivec3 mn = targetCells_[0], mx = targetCells_[0];
    for (auto& c : targetCells_) {
        mn = {std::min(mn.x, c.x), std::min(mn.y, c.y), std::min(mn.z, c.z)};
        mx = {std::max(mx.x, c.x), std::max(mx.y, c.y), std::max(mx.z, c.z)};
    }
    const float e = 0.004f;
    vec3 a = vec3(mn.x * VOX, mn.y * VOX, mn.z * VOX) - vec3(e) - cam_.pos;
    vec3 b = vec3((mx.x + 1) * VOX, (mx.y + 1) * VOX, (mx.z + 1) * VOX) + vec3(e) - cam_.pos;
    uint32_t c = miniMode_ ? 0xD040E0FFu : 0x99000000u;
    std::vector<LineVertex> L;
    auto edge = [&](vec3 p, vec3 q) {
        L.push_back({p, c});
        L.push_back({q, c});
    };
    vec3 v[8];
    for (int i = 0; i < 8; ++i) v[i] = vec3(i & 1 ? b.x : a.x, i & 2 ? b.y : a.y, i & 4 ? b.z : a.z);
    const int E[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& ed : E) edge(v[ed[0]], v[ed[1]]);
    renderer_.drawLines(L);
}

void Game::drawHeld() {
    const ItemStack& held = player_.inv.slots[player_.selected];
    uint8_t L = world_.lightAt(cellOf(cam_.pos.x), cellOf(cam_.pos.y), cellOf(cam_.pos.z));
    float sky = float(L >> 4), blk = float(L & 15);
    auto smooth = [](float x) {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    };
    float t = 1.0f - swing_;
    float c = swing_ <= 0.0f ? 0.0f
              : t < 0.18f    ? -0.32f * smooth(t / 0.18f)
              : t < 0.42f    ? -0.32f + 1.32f * smooth((t - 0.18f) / 0.24f)
                             : 1.0f - smooth((t - 0.42f) / 0.58f);
    float sb = player_.sprintBlend, sw2 = player_.swapAnim * player_.swapAnim;
    float bob = settings_.bobbing ? player_.bobAmount : 0.0f;
    float ph = player_.bobPhase;
    vec3 bobOff(std::sin(ph) * 0.035f * bob * (1.0f + 0.5f * sb), -std::fabs(std::cos(ph)) * 0.045f * bob * (1.0f + 0.6f * sb), 0);
    vec3 dragOff(viewModelYawLag_ * 0.22f, -viewModelPitchLag_ * 0.13f, std::min(0.035f, std::fabs(viewModelYawLag_) * 0.18f));
    dragOff.y -= zoomAmount_ * 0.10f;
    dragOff.y += player_.dip * 0.55f;
    dragOff.y += std::sin(float(clock_) * 1.7f) * 0.004f;
    dragOff.y -= sw2 * 0.55f;
    dragOff.z += sw2 * 0.15f;
    dragOff += vec3(0.04f * sb, -0.07f * sb, 0.03f * sb);
    const float heldFovDeg = 70.0f - zoomAmount_ * 8.0f;
    const float tanV = std::tan(heldFovDeg * 0.5f * DEG2RAD), vmZ = 0.95f;
    const float halfH = tanV * vmZ, halfW = halfH * cam_.aspect;
    vec3 anchor(halfW * 0.48f, -halfH * 0.78f, -vmZ);
    Xf base = T(anchor + bobOff + dragOff) * RY(viewModelYawLag_) * RX(viewModelPitchLag_ + sw2 * 0.9f - 0.35f * sb) *
              RZ(viewModelRollLag_ + 0.18f * sb);
    std::vector<BoxInstance> out;
    auto scaled = [](const mat3& R, float k) {
        mat3 m = R;
        for (auto& row : m.m)
            for (float& v : row) v *= k;
        return m;
    };
    const float px = 1.0f / 16.0f;
    ToolKind kind = (!held.empty() && !isBlockItem(held.id)) ? itemDef(held.id).tool : ToolKind::None;
    auto drawArm = [&](vec3 grip, vec3 knuckleDir) {
        vec3 shoulder(halfW * 1.10f, -halfH * 1.45f, -0.40f);
        vec3 Y = shoulder - grip;
        float len = length(Y);
        Y = Y / std::max(len, 0.01f);
        vec3 X = normalize(cross(Y, knuckleDir), {1, 0, 0}), Z = cross(X, Y);
        mat3 A;
        for (int i = 0; i < 3; ++i) {
            A.m[i][0] = (&X.x)[i] * px;
            A.m[i][1] = (&Y.x)[i] * px;
            A.m[i][2] = (&Z.x)[i] * px;
        }
        drawModel(out, loadModel("hand"), grip, A, sky, blk, std::max(2.0f, len * 16.0f - 2.6f));
    };
    if (held.empty()) {
        Xf P = base * T(vec3(-0.10f - 0.20f * c, 0.06f + 0.05f * c, -0.05f - 0.42f * c)) * RZ(0.25f * c);
        drawArm(P.t, P.R * vec3(0, 0, -1));
    } else if (isBlockItem(held.id) && def(BlockId(held.id)).render != Render::Cross) {
        Xf P = base * T(vec3(0.02f - 0.12f * c, 0.06f - 0.10f * c, -0.08f - 0.24f * c)) * RX(-0.5f * c);
        Xf B = P * RY(0.75f);
        drawHeldItem(out, held.id, scaled(B.R, px), B.t + B.R * vec3(-0.125f), sky, blk);
        drawArm(P.t + P.R * vec3(0.06f, -0.16f, 0.06f), P.R * vec3(0, 0, -1));
    } else {
        Xf P;
        if (kind == ToolKind::Sword)
            P = base * T(vec3(-0.50f * c, 0.06f * c, -0.14f * c)) * RZ(0.95f * c) * RX(-0.35f * c);
        else if (kind == ToolKind::Axe)
            P = base * T(vec3(-0.30f * c, -0.20f * c, -0.18f * c)) * RX(-1.15f * c) * RZ(0.55f * c);
        else if (kind == ToolKind::Shovel)
            P = base * T(vec3(-0.16f * c, -0.12f * c, -0.28f * c)) * RX(-0.85f * c);
        else if (kind == ToolKind::Pick)
            P = base * T(vec3(-0.22f * c, -0.24f * c, -0.22f * c)) * RX(-1.25f * c) * RZ(0.30f * c);
        else
            P = base * T(vec3(-0.12f * c, 0.0f, -0.20f * c)) * RX(-0.6f * c);
        Xf Sp = P * RY(-1.45f) * RZ(0.1f);
        drawHeldItem(out, held.id, scaled(Sp.R, px), Sp.t + Sp.R * vec3(-0.25f, -0.15f, -0.5f * px), sky, blk);
        drawArm(Sp.t + Sp.R * vec3(-0.13f, -0.07f, 0.0f), Sp.R * vec3(1, 0, 0));
    }
    mat4 proj = perspective(heldFovDeg * DEG2RAD, cam_.aspect, 0.02f, 10.0f);
    gl::Clear(gl::DEPTH_BUFFER_BIT);
    renderer_.drawBoxes(out, false, &proj, true, 0);
}

void Game::renderWorld() {
    vec3 eye = player_.viewEye();
    float bob = settings_.bobbing ? player_.bobAmount : 0.0f;
    vec3 right(std::cos(player_.yaw), 0, std::sin(player_.yaw));
    eye += right * (std::sin(player_.bobPhase) * 0.04f * bob) + vec3(0, (1.0f - std::cos(player_.bobPhase * 2.0f)) * 0.025f * bob, 0);

    float dtView = std::max(frameDt_, 1e-4f);
    if (!viewMotionInit_) {
        viewLastYaw_ = player_.yaw;
        viewLastPitch_ = player_.pitch;
        viewMotionInit_ = true;
    }
    float dyaw = player_.yaw - viewLastYaw_;
    while (dyaw > PI) dyaw -= PI * 2.0f;
    while (dyaw < -PI) dyaw += PI * 2.0f;
    float dpitch = player_.pitch - viewLastPitch_;
    viewLastYaw_ = player_.yaw;
    viewLastPitch_ = player_.pitch;
    float rawYawRate = std::clamp(dyaw / dtView, -8.0f, 8.0f);
    float rawPitchRate = std::clamp(dpitch / dtView, -8.0f, 8.0f);
    float kTurn = 1.0f - std::exp(-dtView * 10.0f);
    viewYawRate_ += (rawYawRate - viewYawRate_) * kTurn;
    viewPitchRate_ += (rawPitchRate - viewPitchRate_) * kTurn;

    float targetSide = settings_.turnSway ? std::clamp(-viewYawRate_ * 0.0048f, -0.020f, 0.020f) : 0.0f;
    float targetVert = settings_.turnSway ? std::clamp(-viewPitchRate_ * 0.0028f, -0.011f, 0.011f) : 0.0f;
    float targetRoll = settings_.turnSway ? std::clamp(-viewYawRate_ * 0.0064f, -1.45f * DEG2RAD, 1.45f * DEG2RAD) : 0.0f;
    float kSway = 1.0f - std::exp(-dtView * 8.5f);
    viewSideSway_ += (targetSide - viewSideSway_) * kSway;
    viewVertSway_ += (targetVert - viewVertSway_) * kSway;
    viewRollSway_ += (targetRoll - viewRollSway_) * kSway;
    eye += right * viewSideSway_;
    eye.y += viewVertSway_ - player_.dip;
    float inertialRoll = viewRollSway_ + player_.strafeRoll;

    float vmYawTarget = std::clamp(-viewYawRate_ * 0.014f, -4.5f * DEG2RAD, 4.5f * DEG2RAD);
    float vmPitchTarget = std::clamp(viewPitchRate_ * 0.010f, -3.2f * DEG2RAD, 3.2f * DEG2RAD);
    float vmRollTarget = std::clamp(-viewYawRate_ * 0.0045f, -1.5f * DEG2RAD, 1.5f * DEG2RAD);
    float kVm = 1.0f - std::exp(-dtView * 10.0f);
    viewModelYawLag_ += (vmYawTarget - viewModelYawLag_) * kVm;
    viewModelPitchLag_ += (vmPitchTarget - viewModelPitchLag_) * kVm;
    viewModelRollLag_ += (vmRollTarget - viewModelRollLag_) * kVm;
    float handYaw = 0, handPitch = 0, handRoll = 0;
    if (settings_.handDrift) {
        float t = float(clock_);
        handYaw = std::sin(t * 0.73f) * 0.11f * DEG2RAD + std::sin(t * 1.31f + 1.7f) * 0.045f * DEG2RAD;
        handPitch = std::sin(t * 0.89f + 0.8f) * 0.085f * DEG2RAD;
        handRoll = std::sin(t * 0.57f + 2.1f) * 0.10f * DEG2RAD;
        eye += right * (std::sin(t * 0.67f) * 0.0035f) + vec3(0, std::sin(t * 0.92f + .4f) * 0.0030f, 0);
    }
    if (sim_.shake > 0) {
        float t = float(clock_);
        if (settings_.stepShake) t = std::floor(t * 24.0f) / 24.0f;
        eye += vec3(std::sin(t * 61.0f), std::sin(t * 47.0f + 1.3f), std::sin(t * 53.0f + 2.1f)) * (sim_.shake * 0.12f);
    }
    cam_.yaw = player_.yaw;
    cam_.pitch = player_.pitch;
    cam_.roll = inertialRoll;
    cam_.pos = eye;
    cam_.yaw += handYaw;
    cam_.pitch = std::clamp(cam_.pitch + handPitch, -1.53f, 1.53f);
    cam_.roll += handRoll;
    float zoomTarget = (input_ && input_->down(Key::C) && screen_ == Screen::Playing) ? 1.0f : 0.0f;
    float zoomK = 1.0f - std::exp(-frameDt_ * (zoomTarget > zoomAmount_ ? 11.0f : 8.0f));
    zoomAmount_ += (zoomTarget - zoomAmount_) * zoomK;
    float z = zoomAmount_ * zoomAmount_ * (3.0f - 2.0f * zoomAmount_);
    float breath = 0.0f;
    if (player_.sprinting && settings_.sprintPulse && z < 0.2f)
        breath = std::sin(float(clock_) * 3.2f) * 0.48f + std::sin(float(clock_) * 1.55f + 0.7f) * 0.18f;
    float normalFov = settings_.fov + player_.sprintBlend * (7.5f + breath) - (player_.headInWater ? 6.0f : 0.0f);
    float zoomFov = std::clamp(settings_.fov * 0.46f, 24.0f, 36.0f);
    float fov = normalFov + (zoomFov - normalFov) * z;
    cam_.fov += (fov * DEG2RAD - cam_.fov) * (1.0f - std::exp(-frameDt_ * 12.0f));
    cam_.aspect = settings_.aspect43 ? (4.0f / 3.0f) : float(fbW_) / float(std::max(fbH_, 1));
    cam_.farZ = settings_.renderDist * CS * VOX + 80.0f;
    cam_.update();

    SkyState sky;
    computeSky(sky);
    {
        uint8_t inside = world_.get(cellOf(cam_.pos.x), cellOf(cam_.pos.y), cellOf(cam_.pos.z));
        renderer_.setCaveCulling(!(player_.spectator() && def(inside).render == Render::Opaque));
    }
    renderer_.beginFrame(cam_, sky, float(clock_));
    sim_.prepareBodies(cam_.pos);
    gatherPointLights();
    boxes_.clear();
    blended_.clear();
    sim_.gatherBoxes(boxes_, blended_, cam_.pos, float(clock_));
    drawAdventure(boxes_);
    drawAmbient(blended_);
    for (int light = 0; light < renderer_.pointShadowCount(); ++light) {
        const auto& source = renderer_.pointLight(light);
        const auto bodyStamp = sim_.bodyShadowStamp(source.pos, source.radius);
        if (!renderer_.pointShadowNeedsUpdate(light, boxes_, bodyStamp)) continue;
        for (int face = 0; face < 6; ++face) {
            renderer_.beginPointShadow(light, face);
            sim_.drawBodies(renderer_, cam_.pos, true);
            renderer_.drawBoxesShadow(boxes_);
        }
    }
    renderer_.endPointShadows();
    if (renderer_.shadowsActive()) {
        renderer_.beginShadow();
        sim_.drawBodies(renderer_, cam_.pos, true);
        renderer_.drawBoxesShadow(boxes_);
        renderer_.endShadow();
    }
    renderer_.drawSky();
    renderer_.drawTerrain(false);
    sim_.drawBodies(renderer_, cam_.pos);
    renderer_.drawBoxes(boxes_, false);
    drawSelection();
    renderer_.drawTerrain(true);
    renderer_.drawClouds();
    renderer_.drawBoxes(blended_, true);
    if (!hideHud_ && !player_.spectator()) drawHeld();
    if (sky.underwater) renderer_.drawFullscreenTint(vec4(0.02f, 0.19f, 0.24f, 0.15f));
    if (sky.inLava) renderer_.drawFullscreenTint(vec4(0.95f, 0.22f, 0.025f, 0.6f));
    renderer_.endWorld();
}

}
