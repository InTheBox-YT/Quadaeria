#pragma once
#include "platform/Input.h"
#include "world/Collide.h"
#include "world/Inventory.h"

namespace qc {
enum class GameMode : uint8_t { Creative, Spectator };

struct PlayerInput {
    vec3 move;
    bool jump = false, jumpPressed = false, sneak = false, sprint = false;
    float yawDelta = 0, pitchDelta = 0;
};

struct Player {
    vec3 pos, vel;
    float yaw = 0, pitch = 0;
    float width = 0.6f, height = 1.8f, eyeHeight = 1.62f;
    bool gliding = false;
    bool onGround = false, flying = false, sneaking = false, sprinting = false;
    bool inWater = false, headInWater = false, inLava = false;
    float inLeaves = 0;
    uint8_t leafBlock = 0;
    GameMode mode = GameMode::Creative;
    float specSpeed = 12.0f;
    float fallStart = 0;
    float lastJumpTap = -10;
    float bobPhase = 0, bobAmount = 0, stepDist = 0;
    float coyote = 0, jumpBuffer = 0;
    bool jumpRising = false;
    float landImpact = 0;
    bool jumpEvent = false;
    float dip = 0, dipVel = 0;
    float strafeRoll = 0;
    float sprintBlend = 0;
    float airTime = 0;
    float swapAnim = 0;
    float stepSmooth = 0;
    float stepVelocity = 0;
    float eyeSmooth = 1.62f;
    Inventory inv;
    int selected = 0;
    vec3 spawn;
    vec3 externalVel;
    vec3 platformVel;

    vec3 eye() const { return pos + vec3(0, sneaking && !flying ? eyeHeight - 0.12f : eyeHeight, 0); }
    vec3 viewEye() const { return pos + vec3(0, eyeSmooth + stepSmooth, 0); }
    vec3 visualFeet() const { return pos + vec3(0, stepSmooth, 0); }
    vec3 look() const { return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)}; }
    AABB box() const { return boxAt(pos, width, sneaking && !flying ? height - 0.3f : height); }
    ItemStack& held() { return inv.slots[selected]; }
    bool creative() const { return mode == GameMode::Creative; }
    bool spectator() const { return mode == GameMode::Spectator; }
    void push(vec3 from, float knock);
};

bool updatePlayer(Player& p, const World& w, const PlayerInput& in, float dt, float time);

}
