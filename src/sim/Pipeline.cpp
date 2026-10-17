#include "sim/Sim.h"
#include <chrono>
#include <cmath>

namespace qc {
namespace {
double msSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}
int roleOf(EntType t) { return int(t); }
}

Sim::Sim() : bodies_(ecs_) {
    entityId_ = ecs_.component<Entity>().id();
    particleId_ = ecs_.component<Particle>().id();
    roleId_[roleOf(EntType::Item)] = ecs_.component<DroppedItem>().id();
    roleId_[roleOf(EntType::TNT)] = ecs_.component<Explosive>().id();
    roleId_[roleOf(EntType::Falling)] = ecs_.component<FallingVoxel>().id();
    roleId_[roleOf(EntType::Debris)] = ecs_.component<FlyingDebris>().id();
    ecs_.component<SimulationFrame>();
    ecs_.set<SimulationFrame>({});
    objects_.init(ecs_);
    items_.init(ecs_);
    particles_.init(ecs_);
    registerEcsSystems();
}

void Sim::addEntity(Entity&& value) {
    if (!value.id) value.id = nextEntityId_++;
    pendingEnts_.push_back(std::move(value));
    if (!iterating_ && !objects_.busy() && !items_.busy() && !ecs_.is_deferred()) commitEntities();
}

void Sim::commitEntities() {
    if (pendingEnts_.empty()) return;
    ImmediateWrites live(ecs_);
    std::vector<Entity> batch;
    for (int role = 0; role < 4; ++role) {
        batch.clear();
        for (Entity& e : pendingEnts_)
            if (roleOf(e.type) == role) batch.push_back(std::move(e));
        if (batch.empty()) continue;
        ecs_bulk_desc_t desc{};
        desc.count = int32_t(batch.size());
        desc.ids[0] = entityId_;
        desc.ids[1] = roleId_[role];
        void* data[2] = {batch.data(), nullptr};
        desc.data = data;
        ecs_bulk_init(ecs_.c_ptr(), &desc);
    }
    pendingEnts_.clear();
}

void Sim::deleteDeadEntities() {
    doomed_.clear();
    for (auto it = objects_.begin(); it != objects_.end(); ++it)
        if (it->dead) doomed_.push_back(it.entity());
    if (doomed_.empty()) return;
    ImmediateWrites live(ecs_);
    for (flecs::entity_t e : doomed_) ecs_delete(ecs_.c_ptr(), e);
}

void Sim::stepEntities(float dt, Player& player) {
    for (Entity& e : objects_) stepEntity(e, dt, player);
}

void Sim::particle(PKind k, vec3 pos, vec3 vel, float life, float size, vec4 tint, int layer) {
    if (liveParticles_ + int(spawnParticles_.size()) >= settings.maxParticles) return;
    Particle p;
    p.kind = k;
    p.pos = pos;
    p.vel = vel;
    p.life = p.maxLife = life;
    p.size = size;
    p.tint = tint;
    p.layer = layer;
    p.uvOff = rng_.uniform() * 0.75f;
    spawnParticles_.push_back(p);
}

void Sim::stepParticles(float dt) {
    if (decay_.dt != dt) {
        auto d = [dt](float k) { return std::exp(-dt * k); };
        decay_ = {dt, d(0.4f), d(10.0f), d(1.2f), d(1.4f), d(2.4f), d(3.0f)};
    }
    for (auto it = particles_.begin(); it != particles_.end(); ++it) {
        Particle& p = *it;
        if (p.life <= 0) continue;
        stepParticle(p, dt);
        if (p.life <= 0) {
            freeParticles_.push_back(it.entity());
            --liveParticles_;
        }
    }
}

void Sim::commitParticles() {
    ImmediateWrites live(ecs_);
    size_t i = 0, n = std::min<size_t>(spawnParticles_.size(), size_t(std::max(0, settings.maxParticles - liveParticles_)));
    while (i < n && !freeParticles_.empty()) {
        flecs::entity_t e = freeParticles_.back();
        freeParticles_.pop_back();
        if (auto* slot = static_cast<Particle*>(ecs_get_mut_id(ecs_.c_ptr(), e, particleId_))) *slot = spawnParticles_[i++];
    }
    if (i < n) {
        ecs_bulk_desc_t desc{};
        desc.count = int32_t(n - i);
        desc.ids[0] = particleId_;
        void* data[1] = {spawnParticles_.data() + i};
        desc.data = data;
        ecs_bulk_init(ecs_.c_ptr(), &desc);
    }
    liveParticles_ += int(n);
    spawnParticles_.clear();
    if (freeParticles_.size() > size_t(liveParticles_) + 512) {
        for (int k = 0; k < 256 && !freeParticles_.empty(); ++k) {
            ecs_delete(ecs_.c_ptr(), freeParticles_.back());
            freeParticles_.pop_back();
        }
    }
}

void Sim::clearParticles() {
    ImmediateWrites live(ecs_);
    ecs_.delete_with<Particle>();
    spawnParticles_.clear();
    freeParticles_.clear();
    liveParticles_ = 0;
}

void Sim::registerEcsSystems() {
    auto previous = ecs_.entity(flecs::OnUpdate);
    auto stage = [&](const char* name, auto&& body) {
        auto phase = ecs_.entity().add(flecs::Phase).depends_on(previous);
        previous = phase;
        ecs_.system(name).kind(phase).immediate().run([this, body](flecs::iter&) {
            ImmediateWrites live(ecs_);
            body(ecs_.get<SimulationFrame>());
        });
        ++systemCount_;
    };
    using F = const SimulationFrame&;
    stage("TerrainChecks", [this](F f) {
        auto t = std::chrono::steady_clock::now();
        beginFrame(f.dt, *f.player);
        stats.checksMs = msSince(t);
    });
    stage("Fluids", [this](F f) {
        auto t = std::chrono::steady_clock::now();
        tickFluids(f.dt);
        stats.fluidMs = msSince(t);
    });
    stage("RigidBodies", [this](F f) {
        auto t = std::chrono::steady_clock::now();
        tickBodies(f.dt, *f.player);
        stats.bodyMs = msSince(t);
    });
    stage("PlayerOnBodies", [this](F f) { beginEntities(f.dt, *f.player); });
    stage("Objects", [this](F f) {
        auto t = std::chrono::steady_clock::now();
        stepEntities(f.dt, *f.player);
        stats.objectMs = msSince(t);
    });
    stage("ObjectCommit", [this](F f) { finishEntities(f.dt); });
    stage("Canopy", [this](F f) {
        for (Body& b : bodies_) stepCanopy(b, *f.player);
    });
    stage("Stress", [this](F f) { stepStress(f.dt); });
    stage("Power", [this](F f) { stepPower(f.dt, *f.player); });
    stage("Leaves", [this](F f) { stepLeaves(f.dt); });
    stage("Ambience", [this](F f) { stepAmbience(f.dt); });
    stage("Particles", [this](F f) {
        auto t = std::chrono::steady_clock::now();
        stepParticles(f.dt);
        stats.particleMs = msSince(t);
    });
    stage("EffectsCommit", [this](F f) { finishFrame(f.dt); });
}

void Sim::update(float dt, Player& player, float daylight, bool night) {
    if (!world_ || !std::isfinite(dt) || dt <= 0) return;
    auto start = std::chrono::steady_clock::now();
    ecs_.set<SimulationFrame>({&player, dt, daylight, night});
    ecs_.progress(dt);
    ecs_.set<SimulationFrame>({});
    stats.ecsMs = msSince(start);
    stats.ecsSystems = systemCount_;
    ++stats.ecsFrames;
}

}
