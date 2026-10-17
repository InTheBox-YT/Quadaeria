#include "player/Player.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>

namespace qc {
void Player::push(vec3 from, float knock) {
    vec3 d = pos - from;
    d.y = 0;
    d = normalize(d, {1, 0, 0});
    externalVel += d * knock + vec3(0, knock * 0.6f, 0);
}

bool updatePlayer(Player& p, const World& w, const PlayerInput& in, float dt, float time) {
    p.yaw += in.yawDelta;
    p.pitch = std::clamp(p.pitch + in.pitchDelta, -1.55f, 1.55f);
    if (p.spectator()) {
        vec3 look = p.look(), right{std::cos(p.yaw), 0, std::sin(p.yaw)};
        vec3 wish = look * in.move.z + right * in.move.x + vec3(0, (in.jump ? 1.0f : 0.0f) - (in.sneak ? 1.0f : 0.0f), 0);
        float wl = length(wish);
        if (wl > 1) wish = wish / wl;
        float speed = p.specSpeed * (in.sprint ? 2.2f : 1.0f);
        p.vel += (wish * speed - p.vel) * (1.0f - std::exp(-dt * 7.0f));
        p.pos += p.vel * dt;
        p.pos.y = std::clamp(p.pos.y, -60.0f, float(WH) * VOX + 80.0f);
        p.flying = true;
        p.onGround = false;
        p.sneaking = false;
        p.sprinting = false;
        p.gliding = false;
        p.externalVel = {};
        p.fallStart = p.pos.y;
        p.inWater = p.inLava = p.headInWater = false;
        p.inLeaves = 0;
        p.bobAmount *= std::exp(-dt * 10.0f);
        p.stepSmooth = p.stepVelocity = 0;
        p.dip *= std::exp(-dt * 10.0f);
        p.dipVel = 0;
        p.eyeSmooth = p.eyeHeight;
        p.strafeRoll *= std::exp(-dt * 8.0f);
        p.sprintBlend *= std::exp(-dt * 8.0f);
        return false;
    }
    p.jumpBuffer = in.jumpPressed ? 0.13f : std::max(0.0f, p.jumpBuffer - dt);
    p.coyote = p.onGround ? 0.10f : std::max(0.0f, p.coyote - dt);
    p.airTime = p.onGround ? 0.0f : p.airTime + dt;

    if (in.jumpPressed) {
        // double tap space to fly
        if (time - p.lastJumpTap < 0.3f) {
            p.flying = !p.flying;
            p.vel.y = 0;
            p.lastJumpTap = -10;
        } else
            p.lastJumpTap = time;
    }
    p.sneaking = in.sneak && !p.flying;
    p.sprinting = in.sprint && in.move.z > 0.1f && !p.sneaking;

    AABB box = p.box();
    FluidInfo fl = fluidAt(w, box, p.eye().y);
    p.inWater = fl.waterFraction > 0.05f;
    p.headInWater = fl.headInWater;
    p.inLava = fl.lavaFraction > 0.05f;
    p.inLeaves = leafFraction(w, box, &p.leafBlock);

    vec3 fwd{std::sin(p.yaw), 0, -std::cos(p.yaw)}, right{std::cos(p.yaw), 0, std::sin(p.yaw)};
    vec3 wish = fwd * in.move.z + right * in.move.x;
    float wl = length(wish);
    if (wl > 1) wish = wish / wl;

    vec3 previousVelocity = p.vel;
    const float G = 32.0f;
    bool wingReady = p.held().id == I_GLIDER && !p.onGround && !p.flying && !p.inWater && !p.inLava;
    if (!wingReady) p.gliding = false;
    if (wingReady && (in.jumpPressed || (in.jump && p.vel.y < -0.1f))) p.gliding = true;
    if (in.sneak) p.gliding = false;
    if (p.flying) {
        float speed = p.sprinting ? 21.0f : 10.9f;
        vec3 target = wish * speed;
        target.y = (in.jump ? 1.0f : 0.0f) * 7.5f - (in.sneak ? 1.0f : 0.0f) * 7.5f;
        float k = 1.0f - std::exp(-dt * 10.0f);
        p.vel += (target - p.vel) * k;
    } else if (p.inWater || p.inLava) {
        float speed = p.inLava ? 1.0f : (p.sprinting ? 3.0f : 2.2f);
        vec3 target = wish * speed;
        float k = 1.0f - std::exp(-dt * 4.0f);
        p.vel.x += (target.x - p.vel.x) * k;
        p.vel.z += (target.z - p.vel.z) * k;
        float sub = std::max(fl.waterFraction, fl.lavaFraction);
        p.vel.y -= G * 0.25f * dt;
        p.vel.y += G * 0.18f * sub * dt;
        if (in.jump) p.vel.y += 22.0f * dt;
        p.vel.y *= std::exp(-dt * 3.0f);
        p.vel.y = std::clamp(p.vel.y, -3.0f, 4.0f);
        if (in.jump && sub < 0.6f && (p.vel.x != 0 || p.vel.z != 0)) {
            AABB test = box.offset(normalize(vec3(p.vel.x, 0, p.vel.z), {0, 0, 0}) * 0.2f);
            if (boxHitsWorld(w, test)) p.vel.y = std::max(p.vel.y, 5.2f);
        }
        p.vel += fl.flow * (dt * 1.8f);
        p.fallStart = p.pos.y;
    } else if (p.gliding) {
        float dive = std::clamp(-p.pitch, 0.0f, 1.2f);
        float climb = std::clamp(p.pitch, 0.0f, 0.8f);
        float speed = 10.5f + dive * 9.0f - climb * 3.5f;
        float k = 1.0f - std::exp(-dt * 2.8f);
        vec3 heading = normalize(fwd + right * in.move.x * 0.35f, fwd);
        p.vel.x += (heading.x * speed - p.vel.x) * k;
        p.vel.z += (heading.z * speed - p.vel.z) * k;
        float sink = -1.35f - dive * 8.0f - climb * 0.6f;
        p.vel.y += (sink - p.vel.y) * (1.0f - std::exp(-dt * 6.0f));
        p.fallStart = p.pos.y;
    } else {
        float speed = p.sneaking ? 1.31f : p.sprinting ? 5.9f : 4.32f;
        speed *= 1.0f - 0.55f * std::min(1.0f, p.inLeaves * 1.6f);
        vec3 target = wish * speed;
        float accel;
        if (p.onGround) {
            bool moving = lengthSq(wish) > 0.0004f;
            float along = moving ? dot(normalize(vec3(p.vel.x, 0, p.vel.z), {0, 0, 0}), normalize(wish, {0, 0, 0})) : 1.0f;
            accel = !moving ? 30.0f : (along < -0.2f ? 34.0f : 20.0f);
        } else
            accel = 4.6f;
        float k = 1.0f - std::exp(-dt * accel);
        p.vel.x += (target.x - p.vel.x) * k;
        p.vel.z += (target.z - p.vel.z) * k;
        float g = G;
        if (p.vel.y < 0)
            g *= 1.22f;
        else if (std::fabs(p.vel.y) < 1.6f && !p.onGround)
            g *= 0.78f;
        p.vel.y -= g * dt;
        p.vel.y *= std::exp(-dt * (0.4f + 7.0f * p.inLeaves));
        if (p.inLeaves > 0.1f) p.vel.y = std::max(p.vel.y, -6.0f);
        p.vel.y = std::max(p.vel.y, -78.4f);
        if (p.jumpRising && !in.jump && p.vel.y > 3.4f) {
            p.vel.y = 3.4f;
            p.jumpRising = false;
        }
        if (p.vel.y <= 0) p.jumpRising = false;
        bool canJump = p.onGround || p.coyote > 0;
        if (canJump && ((in.jump && p.onGround) || p.jumpBuffer > 0) && p.vel.y < 4.0f) {
            p.vel.y = 8.6f;
            p.jumpRising = true;
            p.jumpBuffer = 0;
            p.coyote = 0;
            p.jumpEvent = true;
            p.dipVel -= 1.6f;
            if (p.sprinting) p.vel += fwd * 1.6f;
        }
    }
    bool vines = false;
    if (!p.flying)
        for (int y = cellOf(p.pos.y); y <= cellOf(p.pos.y + 1.4f); ++y)
            for (int z = cellOf(p.pos.z - 0.28f); z <= cellOf(p.pos.z + 0.28f); ++z)
                for (int x = cellOf(p.pos.x - 0.28f); x <= cellOf(p.pos.x + 0.28f); ++x)
                    if (w.get(x, y, z) == B_VINE) vines = true;
    if (vines && !p.gliding) {
        p.vel.y = in.sneak ? 0.0f : (in.jump || in.move.z > 0.1f ? 2.4f : std::max(p.vel.y, -1.6f));
        p.fallStart = p.pos.y;
    }
    if (!p.flying)
        for (int y = cellOf(p.pos.y); y >= std::max(1, cellOf(p.pos.y) - 14); --y) {
            int x = cellOf(p.pos.x), z = cellOf(p.pos.z);
            uint8_t b = w.get(x, y, z);
            if (b == B_GEYSER) {
                float jet = geyserJet(x, z, time);
                if (jet > 0) {
                    p.vel.y += jet * 48.0f * dt;
                    p.vel.y = std::min(p.vel.y, 10.0f);
                    p.fallStart = p.pos.y;
                }
                break;
            }
            if (def(b).collides) break;
        }
    vec3 motion = p.vel;
    motion.x = (previousVelocity.x + p.vel.x) * 0.5f;
    motion.z = (previousVelocity.z + p.vel.z) * 0.5f;
    vec3 move = (motion + p.externalVel + p.platformVel) * dt;
    p.externalVel *= std::exp(-dt * (p.onGround ? 8.0f : 1.5f));
    if (lengthSq(p.externalVel) < 0.01f) p.externalVel = {};

    if (p.sneaking && p.onGround) {
        auto supported = [&](vec3 d) { return boxHitsWorld(w, box.offset(d + vec3(0, -0.55f, 0))); };
        if (move.x != 0 && !supported({move.x, 0, 0})) move.x = 0;
        if (move.z != 0 && !supported({0, 0, move.z})) move.z = 0;
    }

    float step = (p.onGround || p.inWater) && !p.flying ? 0.51f : 0.0f;
    MoveResult r = moveBox(w, box, move, step, p.onGround);
    if (p.onGround && !in.jump && !p.flying && !p.gliding && p.vel.y <= 0 && !r.onGround) {
        AABB lower = box;
        auto settle = moveBox(w, lower, {0, -0.51f, 0}, 0, false);
        if (settle.onGround) {
            box = lower;
            r.onGround = true;
            r.hitY = true;
        }
    }
    vec3 oldPos = p.pos;
    const float vyBefore = p.vel.y;
    p.pos = {box.center().x, box.min.y, box.center().z};
    if (r.hitX) p.vel.x = 0, p.externalVel.x = 0;
    if (r.hitZ) p.vel.z = 0, p.externalVel.z = 0;
    bool wasGround = p.onGround;
    p.onGround = r.onGround || (r.hitY && move.y < 0);
    if (!p.onGround && wasGround && p.vel.y <= 0.01f && !p.flying && boxHitsWorld(w, box.offset({0, -0.02f, 0}))) p.onGround = true;
    if (r.hitY) {
        uint8_t under = w.get(cellOf(p.pos.x), cellOf(p.pos.y - 0.06f), cellOf(p.pos.z));
        if (under == B_RUBBER && vyBefore < -2.5f && !p.sneaking && !p.flying) {
            p.vel.y = std::min(15.5f, -vyBefore * 0.9f);
            p.onGround = false;
            p.jumpRising = false;
            p.fallStart = p.pos.y;
            p.landImpact = std::max(p.landImpact, std::min(-vyBefore, 16.0f));
            p.dipVel += 2.0f;
            p.externalVel.y = 0;
        } else if (p.onGround && !wasGround && vyBefore < -4.0f && !p.inWater) {
            p.landImpact = std::max(p.landImpact, -vyBefore);
            p.dipVel += std::min(-vyBefore, 26.0f) * 0.20f;
        }
        if (!(under == B_RUBBER && p.vel.y > 0)) {
            p.vel.y = 0;
            p.externalVel.y = 0;
        }
    }
    {
        float rise = (p.pos.y - oldPos.y) - std::max(move.y, 0.0f);
        if (std::fabs(rise) > 0.04f && wasGround && p.onGround && !p.flying) p.stepSmooth = std::clamp(p.stepSmooth - rise, -0.65f, 0.65f);
        float spring = p.stepVelocity + 18.0f * p.stepSmooth, decay = std::exp(-18.0f * dt);
        p.stepSmooth = (p.stepSmooth + spring * dt) * decay;
        p.stepVelocity = (p.stepVelocity - 18.0f * spring * dt) * decay;
        if (std::fabs(p.stepSmooth) < 0.0001f && std::fabs(p.stepVelocity) < 0.001f) p.stepSmooth = p.stepVelocity = 0;
        float targetEye = p.sneaking && !p.flying ? p.eyeHeight - 0.12f : p.eyeHeight;
        p.eyeSmooth += (targetEye - p.eyeSmooth) * (1.0f - std::exp(-dt * 14.0f));
    }
    if (p.flying && p.onGround && in.sneak) p.flying = false;

    if (p.onGround || p.flying || p.inWater || p.inLeaves > 0.15f)
        p.fallStart = p.pos.y;
    else
        p.fallStart = std::max(p.fallStart, p.pos.y);

    {
        float acc = -240.0f * p.dip - 21.0f * p.dipVel;
        p.dipVel += acc * dt;
        p.dip = std::clamp(p.dip + p.dipVel * dt, -0.12f, 0.32f);
        float leanT = (p.onGround && !p.flying)
                          ? -in.move.x * 0.016f * std::min(1.0f, std::sqrt(p.vel.x * p.vel.x + p.vel.z * p.vel.z) / 4.3f)
                          : 0.0f;
        p.strafeRoll += (leanT - p.strafeRoll) * (1.0f - std::exp(-dt * 9.0f));
        p.sprintBlend += ((p.sprinting && p.onGround ? 1.0f : 0.0f) - p.sprintBlend) * (1.0f - std::exp(-dt * 7.0f));
        p.swapAnim = std::max(0.0f, p.swapAnim - dt * 4.2f);
    }
    vec3 dm = p.pos - oldPos;
    float hspeed = std::sqrt(dm.x * dm.x + dm.z * dm.z) / std::max(dt, 1e-4f);
    float target = (p.onGround && !p.flying) ? std::min(hspeed / 4.3f, 1.3f) : 0.0f;
    p.bobAmount += (target - p.bobAmount) * (1.0f - std::exp(-dt * 10.0f));
    p.bobPhase += hspeed * dt * 1.9f;
    bool stepped = false;
    if (p.onGround && !p.sneaking) {
        p.stepDist += hspeed * dt;
        if (p.stepDist > 1.6f) {
            p.stepDist = 0;
            stepped = true;
        }
    }
    return stepped;
}

}
