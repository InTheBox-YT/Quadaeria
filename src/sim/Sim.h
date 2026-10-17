#pragma once
#include "core/Mat3.h"
#include "sim/Ecs.h"
#include "core/Tasks.h"
#include "gfx/Renderer.h"
#include "world/Collide.h"
#include "world/Orient.h"
#include "player/Player.h"
#include <algorithm>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace qc {
struct ivec3 {
    int x, y, z;
    bool operator==(const ivec3& o) const { return x == o.x && y == o.y && z == o.z; }
};
inline int64_t packPos(int x, int y, int z) {
    return (int64_t(uint32_t(x + (1 << 26)) & 0x7FFFFFF) << 36) | (int64_t(uint32_t(z + (1 << 26)) & 0x7FFFFFF) << 9) |
           int64_t(uint32_t(y) & 511);
}
inline ivec3 unpackPos(int64_t k) { return {int((k >> 36) & 0x7FFFFFF) - (1 << 26), int(k & 511), int((k >> 9) & 0x7FFFFFF) - (1 << 26)}; }

class ActiveCells {
    std::deque<int64_t> queue_;
    std::unordered_set<int64_t> present_;

public:
    void insert(int64_t k) {
        if (present_.insert(k).second) queue_.push_back(k);
    }
    size_t count(int64_t k) const { return present_.count(k); }
    size_t size() const { return queue_.size(); }
    bool empty() const { return queue_.empty(); }
    void clear() {
        queue_.clear();
        present_.clear();
    }
    auto begin() const { return queue_.begin(); }
    auto end() const { return queue_.end(); }
    std::vector<int64_t> take(int budget) {
        std::vector<int64_t> out;
        out.reserve(std::min(size(), size_t(std::max(0, budget))));
        while (budget-- > 0 && !queue_.empty()) {
            auto k = queue_.front();
            queue_.pop_front();
            present_.erase(k);
            out.push_back(k);
        }
        return out;
    }
};

bool isPowerBlock(uint8_t b);

enum class Snd : uint8_t {
    Dig,
    Place,
    Step,
    Explode,
    Fuse,
    Splash,
    Hiss,
    Pop,
    Thud,
    Crack,
    Glass,
    Click,
    Burn,
    Creak,
    Rumble,
    Drip,
    Drone,
    Count
};
struct SoundEvent {
    Snd s;
    vec3 pos;
    float vol = 1, pitch = 1;
    Sound material = Sound::Stone;
};

enum class PKind : uint8_t { Block, Smoke, Flame, Splash, Spark, Bubble, Dust, Leaf, Mote, Drip };
struct Particle {
    vec3 pos, vel;
    float life = 1, maxLife = 1, size = 0.08f;
    float simCarry = 0;
    PKind kind = PKind::Block;
    int layer = 0;
    vec4 tint{1, 1, 1, 1};
    float uvOff = 0;
};

enum class EntType : uint8_t { Item, TNT, Falling, Debris };
struct Entity {
    uint32_t id = 0;
    EntType type = EntType::Item;
    vec3 pos, vel;
    float w = 0.25f, h = 0.25f;
    bool onGround = false, dead = false, inWater = false;
    float age = 0;
    ItemId item = 0;
    int count = 0;
    float pickupDelay = 0;
    float yaw = 0;
    float fuse = 0, power = 0;
    uint8_t block = 0;
    vec3 spin, angle;
    float rest = 0;
    float simCarry = 0;
    uint8_t simTier = 0;
};

struct Body {
    int id = 0;
    int sx = 0, sy = 0, sz = 0;
    std::vector<uint8_t> vox;
    int count = 0;
    vec3 com;
    vec3 pos, vel, angVel;
    quat rot;
    float mass = 1, invMass = 1;
    mat3 invInertiaLocal;
    std::vector<std::pair<vec3, uint8_t>> emitters;
    std::vector<vec3> samples;
    struct CachedContact {
        int sample = -1;
        vec3 normal;
        float normalImpulse = 0, friction1 = 0, friction2 = 0;
    };
    std::array<CachedContact, 24> contacts{};
    int contactCount = 0;
    bool sleeping = false;
    float simCarry = 0;
    uint8_t simTier = 0;
    bool simStepped = true;
    vec3 contactMin, contactMax;
    float contactRadius = 0;
    float impactStrength = 0;
    std::vector<vec3> floatPts;
    float floatVol = 0;
    int meshId = 0;
    bool meshDirty = true;
    float rest = 0, age = 0, lastImpact = 0, inWater = 0, lowEnergy = 0;
    bool dead = false;
    std::vector<uint8_t> vmeta;
    std::vector<uint8_t> dvox, dmeta, dlight;
    std::vector<uint8_t> dsky;
    int dsx = 0, dsy = 0, dsz = 0;
    vec3 displayOrigin() const { return vec3(float(dorig.x), float(dorig.y), float(dorig.z)) * VOX; }
    vec3 dmin;
    ivec3 dn0{0, 0, 0};
    ivec3 dorig{0, 0, 0};
    ivec3 dTs{0, 0, 0};
    quat drot;
    bool dvalid = false;
    bool meshPending = false;
    bool displayLightDirty = false;
    uint32_t meshVersion = 0;
    float meshAge = -1;
    ivec3 meshOrigin{0, 0, 0};
    uint8_t climT = 128, climH = 128;
    float lastTerrainBreak = -1;
    int terrainBreaks = 0;
    int knocks = 0;
    std::vector<ivec3> leafCells;
    float shed = 0;
    int chips = 0;
    vec3 aabbMin, aabbMax;
    uint8_t at(int x, int y, int z) const {
        if (x < 0 || y < 0 || z < 0 || x >= sx || y >= sy || z >= sz) return 0;
        return vox[size_t((y * sz + z) * sx + x)];
    }
    uint8_t metaAt(int x, int y, int z) const {
        if (vmeta.empty() || x < 0 || y < 0 || z < 0 || x >= sx || y >= sy || z >= sz) return 0;
        return vmeta[size_t((y * sz + z) * sx + x)];
    }
};

struct SimSettings {
    int maxDebris = 220;
    int maxBodies = 48;
    int maxParticles = 2500;
    int fluidBudget = 6000;
};

struct ParticleRenderSettings {
    bool billboards = true;
    bool lowFrameAnim = true;
    bool ditherSmoke = true;
    bool spriteFire = true;
    bool spriteSplash = true;
    bool quantizedRotation = true;
    bool mergeFar = true;
};

class Sim : public World::Listener {

public:
    Sim();
    Sim(const Sim&) = delete;
    Sim& operator=(const Sim&) = delete;
    void init(World* w, Renderer* r, uint64_t seed, TaskPool* pool = nullptr);
    void clear();
    void update(float dt, Player& player, float daylight, bool nightHostiles);
    void onBlockChanged(int x, int y, int z, uint8_t oldB, uint8_t newB) override;

    void explode(vec3 center, float power, Player& player);
    void igniteTNT(int x, int y, int z, float fuse);
    void throwDynamite(vec3 pos, vec3 vel);
    void spawnItem(vec3 pos, ItemId id, int count, vec3 vel = {0, 3, 0});
    void blockParticles(vec3 center, uint8_t block, int n, float spread, float speed);
    void dustPuff(vec3 center, uint8_t block, int n, float speed);
    void chipBody(Body& b, vec3 impact, float speed);
    void particle(PKind k, vec3 pos, vec3 vel, float life, float size, vec4 tint, int layer = -1);
    void removeCells(const std::vector<ivec3>& cells, bool drops, Player* p, vec3 dropCenter);
    void collapseCheck(const std::vector<ivec3>& seeds);
    void setBlockBatch(int x, int y, int z, uint8_t b, uint8_t meta = 0);
    void activateFluid(int x, int y, int z) { fluidActive_.insert(packPos(x, y, z)); }
    int depositFluid(int x, int y, int z, uint8_t type, int amount);
    void displaceFluid(int x, int y, int z);
    void splashFluid(int x, int y, int z, vec3 vel);
    void pushBodies(vec3 center, float radius, float impulse);
    void rustle(vec3 pos, uint8_t leaf, vec3 vel, float amount);
    void structuralEdit(const std::vector<ivec3>& cells, bool removed, int generation = 0, float warnScale = 1.0f);
    static float reachOf(uint8_t block);
    struct Stress {
        ivec3 c;
        uint8_t block;
        float ratio, timer, warn, nextCreak, nextDust, recheck;
        int gen;
        int site = 0;
    };
    const std::vector<Stress>& stressed() const { return stress_; }
    float spanAt(ivec3 c, const std::vector<ivec3>* asSolid = nullptr, const std::vector<ivec3>* asAir = nullptr, bool beams = true) const;
    float spanLimit(ivec3 c) const;
    std::string powerInfo(ivec3 c) const;
    void crankDynamo(ivec3 c);
    void leafEvent(int x, int y, int z);
    float rngRange(float a, float b) { return rng_.range(a, b); }
    vec3 listener;

    void gatherBoxes(std::vector<BoxInstance>& out, std::vector<BoxInstance>& blended, vec3 cam, float time) const;
    void prepareBodies(vec3 cam = {});
    uint64_t bodyShadowStamp(vec3 source, float radius) const;
    void drawBodies(Renderer& r, vec3 cam, bool shadowPass = false);
    void gatherDevices(std::vector<BoxInstance>& out, vec3 cam, float time) const;
    void blobShadow(std::vector<BoxInstance>& blended, vec3 feet, float radius, vec3 cam) const;

    std::vector<SoundEvent> sounds;
    struct Pickup {
        ItemId item;
        int count;
    };
    std::vector<Pickup> pickups;
    std::vector<int64_t> relight;
    SimSettings settings;
    ParticleRenderSettings particleRender;
    float shake = 0;
    struct Stats {
        int entities = 0, bodies = 0, fluids = 0, particles = 0, debris = 0, fluidChanged = 0, bodiesCreated = 0, settled = 0;
        int entityTier[4]{0, 0, 0, 0}, bodyTier[4]{0, 0, 0, 0};
        uint64_t contactSamples = 0, rawContacts = 0, solverContacts = 0, bodyPairs = 0, broadphasePairs = 0, sleepingBodies = 0;
        double checksMs = 0, fluidMs = 0, bodyMs = 0, objectMs = 0, particleMs = 0, ecsMs = 0;
        uint64_t ecsFrames = 0;
        int ecsSystems = 0, particleSlots = 0;
    } stats;
    const EcsTable<Entity>& entities() const { return objects_; }
    const StableList<Body>& bodies() const { return bodies_; }

private:
    void createBodyFromCells(const std::vector<ivec3>& cells, const std::vector<uint8_t>& blocks, vec3 vel, vec3 angVel,
                             const std::vector<uint8_t>* metas = nullptr);
    void lightBody(Body& b, const Region* snapshot = nullptr) const;
    void buildBody(Body& b);
    void stepBodies(float dt, Player& player);
    void solveBodies(float dt, Player& player);
    void settleBody(Body& b, bool force = false);
    ivec3 snapOrigin(const Body& b, vec3 T) const;
    void voxelizeBody(Body& b, const mat3& R, const Region* snapshot = nullptr) const;
    void moveBodyDisplay(Body& b) const;
    Region bodyLightSnapshot(const Body& b, const mat3& R) const;
    void fractureBody(Body& b, vec3 impactPoint, float speed);
    bool bodyContactWorld(vec3 p, vec3& n, float& depth) const;
    bool pointInBody(const Body& b, vec3 p, vec3* exitNormal, float* depth) const;
    bool supportOnBodies(AABB& box, vec3& vel, bool& onGround, vec3* platformVel, float dt);
    struct SimulationFrame {
        Player* player = nullptr;
        float dt = 0, daylight = 1;
        bool night = false;
    };
    void registerEcsSystems();
    void beginFrame(float dt, Player& player);
    void tickFluids(float dt);
    void tickBodies(float dt, Player& player);
    void stepAmbience(float dt);
    void finishFrame(float dt);
    void beginEntities(float dt, Player& player);
    void stepEntities(float dt, Player& player);
    void stepEntity(Entity& e, float dt, Player& player);
    void finishEntities(float dt);
    void mergeItems();
    void commitEntities();
    void deleteDeadEntities();
    void stepCanopy(Body& b, Player& player);
    void stepParticles(float dt);
    struct ParticleDecay {
        float dt = -1, d04 = 1, d10 = 1, d12 = 1, d14 = 1, d24 = 1, d30 = 1;
    } decay_;
    void stepParticle(Particle& p, float dt);
    void commitParticles();
    void clearParticles();
    flecs::world ecs_;
    flecs::entity_t particleId_ = 0, entityId_ = 0, roleId_[4] = {};
    int systemCount_ = 0;
    void refreshSimulationTiers(vec3 focus);
    void landFalling(Entity& e);
    void landDebris(Entity& e);
    void stepFluids();
    void processChecks();
    void markRelight(int x, int z);
    void stepStress(float dt);
    void caveIn(size_t i);
    struct SupportBox {
        int x0 = 0, y0 = 0, z0 = 0, nx = 1, ny = 1, nz = 1;
    };
    SupportBox supportBoxAround(ivec3 mn, ivec3 mx) const;
    void solveSupport(const SupportBox& box, const std::vector<std::pair<ivec3, uint8_t>>* toggle, std::vector<int16_t>& S,
                      const std::vector<ivec3>* cracked = nullptr) const;
    float stressRecheck_ = 0;
    void stepPower(float dt, Player& player);
    void powerCellChanged(int x, int y, int z, uint8_t oldB, uint8_t newB);
    void scanPowerColumns();
    struct PowerNet {
        std::vector<int64_t> cells;
        float gen = 0, supply = 0, demand = 0, ratio = 0;
        bool flowing = false, pulse = false;
    };
    std::vector<PowerNet> nets_;
    std::unordered_map<int64_t, int> netOf_;
    std::unordered_set<int64_t> powerCells_, loadedPlates_;
    std::unordered_map<int64_t, float> cellFrac_, crank_;
    float powerTimer_ = 0;

    void stepLeaves(float dt);
    std::vector<int64_t> leafSeeds_;
    std::vector<std::pair<int64_t, float>> withering_;
    float leafTimer_ = 0;
    std::unordered_map<int, SupportBox> sites_;
    std::unordered_set<int> dirtySites_;
    int nextSite_ = 1;
    std::vector<std::pair<ivec3, float>> cracks_;
    std::unordered_map<int64_t, uint8_t> removedBlocks_;
    std::vector<Stress> stress_;
    float ambienceTimer_ = 3, dripTimer_ = 2, creakCooldown_ = 0, fxTimer_ = 0;
    void ambientParticles(float dt);

    World* world_ = nullptr;
    TaskPool* bodyPool_ = nullptr;
    uint64_t bodyEpoch_ = 0;
    int bodyJobs_ = 0;
    Renderer* renderer_ = nullptr;
    Rng rng_;
    EcsTable<Entity> objects_;
    EcsTable<Entity, DroppedItem> items_;
    EcsTable<Particle> particles_;
    std::vector<Entity> pendingEnts_;
    std::vector<Particle> spawnParticles_;
    std::vector<flecs::entity_t> freeParticles_;
    std::vector<flecs::entity_t> doomed_;
    int liveParticles_ = 0;
    bool iterating_ = false;
    uint32_t nextEntityId_ = 1;
    void addEntity(Entity&& e);
    StableList<Body> bodies_;
    ActiveCells fluidActive_, lavaActive_;
    struct DisplacedFluid {
        ivec3 pos;
        uint8_t type, meta;
    };
    struct FluidPressure {
        std::vector<ivec3> cells;
        uint8_t type = 0;
        int amount = 0;
        uint64_t checkedRevision = 0;
        size_t start = 0;
    };
    std::vector<DisplacedFluid> displacedFluids_;
    std::deque<FluidPressure> fluidPressure_;
    uint64_t fluidEditRevision_ = 1;
    bool internalFluidEdit_ = false;
    void resolveFluidDisplacements();
    ActiveCells gravityChecks_, supportChecks_;
    std::vector<ivec3> collapseSeeds_;
    float fluidTimer_ = 0, lavaTimer_ = 0, bodyAccum_ = 0, spawnTimer_ = 0, fireTimer_ = 0;
    float simTierTimer_ = 0, itemMergeTimer_ = 0;
    unsigned fluidTick_ = 0;
    int nextBodyId_ = 1;
    std::unordered_set<int64_t> relightSet_;
    std::vector<std::pair<ivec3, vec3>> knocked_;
    std::vector<ivec3> crushed_;
    struct Ghost {
        int meshId;
        vec3 pos, uvOrigin;
        std::vector<std::pair<int64_t, uint32_t>> secs;
        int frames = 0;
    };
    std::vector<Ghost> ghosts_;
    unsigned crushTick_ = 0;
};

}
