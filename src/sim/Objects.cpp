#include "sim/Sim.h"
#include "world/Gen.h"
#include <algorithm>
#include <cmath>

namespace qc {
namespace {
inline vec3 cellCenter(int x, int y, int z) { return {(x + 0.5f) * VOX, (y + 0.5f) * VOX, (z + 0.5f) * VOX}; }

float entityStepInterval(const Entity& e) {
    if (e.simTier == 0) return 0.0f;
    if (e.type == EntType::TNT || e.type == EntType::Falling || e.type == EntType::Debris) return 1.0f / 30.0f;
    if (e.simTier == 1) return 1.0f / 30.0f;
    if (e.simTier == 2) return 1.0f / 12.0f;
    return 1.0f / 6.0f;
}
}

void Sim::spawnItem(vec3 pos, ItemId id, int count, vec3 vel) {
    if (!id || count <= 0) return;
    Entity e;
    e.type = EntType::Item;
    e.item = id;
    e.count = count;
    e.w = e.h = 0.25f;
    e.pos = pos - vec3(0, 0.125f, 0);
    e.vel = vel + vec3(rng_.range(-0.8f, 0.8f), 0, rng_.range(-0.8f, 0.8f));
    e.pickupDelay = 0.5f;
    e.yaw = rng_.range(0, 6.28f);
    addEntity(std::move(e));
}

void Sim::landFalling(Entity& e) {
    int cx = cellOf(e.pos.x), cy = cellOf(e.pos.y + 0.05f), cz = cellOf(e.pos.z);
    if (rng_.uniform() < 0.85f) {
        const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
        int start = int(rng_.next() % 4);
        for (int k = 0; k < 4; ++k) {
            int d = (start + k) % 4;
            int nx = cx + dx[d], nz = cz + dz[d];
            if (def(world_->get(nx, cy, nz)).replaceable && !def(world_->get(nx, cy, nz)).fluid &&
                def(world_->get(nx, cy - 1, nz)).replaceable && !def(world_->get(nx, cy - 1, nz)).fluid && world_->loaded(nx, nz)) {
                e.pos = vec3((nx + 0.5f) * VOX, cy * VOX, (nz + 0.5f) * VOX);
                e.onGround = false;
                return;
            }
        }
    }
    uint8_t cur = world_->get(cx, cy, cz);
    if (def(cur).replaceable && cy >= 0 && cy < WH) {
        displaceFluid(cx, cy, cz);
        world_->setBlock(cx, cy, cz, e.block);
        sounds.push_back({Snd::Place, cellCenter(cx, cy, cz), 0.35f, 0.8f, def(e.block).sound});
    } else {
        spawnItem(e.pos + vec3(0, 0.2f, 0), e.block, 1);
    }
    e.dead = true;
}

void Sim::landDebris(Entity& e) {
    int cx = cellOf(e.pos.x), cy = cellOf(e.pos.y + 0.1f), cz = cellOf(e.pos.z);
    const BlockDef& d = def(e.block);
    e.dead = true;
    if (d.fluid) {
        int left = depositFluid(cx, cy, cz, e.block, std::max(1, e.count));
        if (left > 0) fluidPressure_.push_back({{{cx, cy, cz}}, e.block, left});
        if (e.block == B_WATER)
            for (int i = 0; i < 4; ++i)
                particle(PKind::Splash, e.pos + vec3(0, 0.2f, 0),
                         vec3(rng_.range(-1.5f, 1.5f), rng_.range(1.5f, 3.5f), rng_.range(-1.5f, 1.5f)), 0.6f, 0.06f,
                         vec4(0.6f, 0.75f, 1.0f, 0.8f));
        if (rng_.uniform() < 0.3f) sounds.push_back({Snd::Splash, e.pos, 0.25f, 1.3f + rng_.uniform() * 0.3f});
        return;
    }
    bool keeps = d.render != Render::Leaves && d.render != Render::Glass && d.render != Render::Cross;
    if (keeps)
        for (int k = 0; k < 3; ++k) {
            int y = cy + k;
            if (y < 1 || y >= WH || !world_->loaded(cx, cz)) break;
            if (!def(world_->get(cx, y, cz)).replaceable) continue;
            displaceFluid(cx, y, cz);
            world_->setBlock(cx, y, cz, e.block);
            return;
        }
    blockParticles(e.pos + vec3(0, 0.2f, 0), e.block, 4, 0.2f, 2.0f);
}

void Sim::beginEntities(float dt, Player& player) {
    {
        AABB pb = player.box();
        bool og = player.onGround;
        vec3 pv;
        vec3 v = player.vel;
        if (supportOnBodies(pb, v, og, &pv, dt)) {
            player.pos = {pb.center().x, pb.min.y, pb.center().z};
            player.onGround = true;
            player.vel.y = std::max(player.vel.y, 0.0f);
            player.platformVel = vec3(pv.x, std::max(pv.y, 0.0f), pv.z);
            player.fallStart = player.pos.y;
        } else {
            player.pos = {pb.center().x, pb.min.y, pb.center().z};
            player.platformVel = {};
        }
    }
    iterating_ = true;
}

void Sim::stepEntity(Entity& e, float dt, Player& player) {
    const float G = 24.0f;
    if (e.dead) return;
    e.age += dt;
    if (e.type == EntType::Item) e.pickupDelay = std::max(0.0f, e.pickupDelay - dt);
    const float interval = entityStepInterval(e);
    float sdt = dt;
    if (interval > 0.0f) {
        e.simCarry = std::min(.25f, e.simCarry + dt);
        if (e.simCarry + 1e-6f < interval) return;
        sdt = e.simCarry;
        e.simCarry = 0;
    } else {
        e.simCarry = 0;
    }
    AABB box = boxAt(e.pos, e.w, e.h);
    FluidInfo fl = fluidAt(*world_, box, e.pos.y + e.h * 0.85f);
    e.inWater = fl.waterFraction > 0.05f;
    float density = 1.0f;
    if (e.type == EntType::Item)
        density = isBlockItem(e.item) ? def(BlockId(e.item)).density : 0.8f;
    else if (e.type == EntType::Falling || e.type == EntType::Debris)
        density = def(e.block).density;
    else if (e.type == EntType::TNT)
        density = 1.2f;
    if (e.type == EntType::Falling) { e.vel.x = e.vel.z = 0; }
    e.vel.y -= (e.type == EntType::Debris || e.type == EntType::Falling ? 15.0f : G) * sdt;
    float sub = std::max(fl.waterFraction, fl.lavaFraction);
    if (sub > 0) {
        e.vel.y += G * sub / density * sdt;
        e.vel *= std::exp(-sdt * 2.2f * sub);
        e.vel += fl.flow * (sdt * 2.0f);
        if (fl.lavaFraction > 0.2f && e.type == EntType::Item && e.age > 0.5f) {
            e.dead = true;
            particle(PKind::Smoke, e.pos, vec3(0, 1, 0), 1.0f, 0.2f, vec4(0.3f, 0.3f, 0.3f, 0.8f));
            sounds.push_back({Snd::Burn, e.pos, 0.5f, 1.2f});
            return;
        }
    }
    vec3 delta = e.vel * sdt;
    float step = 0.0f;
    MoveResult r = moveBox(*world_, box, delta, step, e.onGround);
    e.onGround = r.onGround;
    const bool droplet = e.type == EntType::Debris && def(e.block).fluid;
    if (droplet && ((r.hitX || r.hitY || r.hitZ) || (e.age > 0.12f && sub > 0.3f && e.vel.y < 0))) {
        e.pos = {box.center().x, box.min.y, box.center().z};
        landDebris(e);
        return;
    }
    if (r.hitY) {
        if (e.type == EntType::Debris && e.vel.y < -3.0f)
            e.vel.y = -e.vel.y * 0.35f;
        else if (e.type == EntType::Item && e.vel.y < -4.0f)
            e.vel.y = -e.vel.y * 0.2f;
        else
            e.vel.y = 0;
    }
    if (e.type == EntType::Debris && (r.hitX || r.hitZ) && e.vel.x * e.vel.x + e.vel.z * e.vel.z > 100.0f) {
        vec3 hv = normalize(vec3(e.vel.x, 0, e.vel.z), {1, 0, 0});
        vec3 hp = box.center() + hv * (e.w * 0.5f + 0.25f);
        int hx = cellOf(hp.x), hy = cellOf(hp.y), hz = cellOf(hp.z);
        uint8_t hb = world_->get(hx, hy, hz);
        const BlockDef& hd = def(hb);
        if (hd.render == Render::Glass || hd.render == Render::Leaves || hb == B_SCAFFOLD) {
            setBlockBatch(hx, hy, hz, B_AIR);
            blockParticles(vec3((hx + 0.5f) * VOX, (hy + 0.5f) * VOX, (hz + 0.5f) * VOX), hb, 6, 0.3f, 5.0f);
            if (hd.render == Render::Glass) sounds.push_back({Snd::Glass, hp, 0.6f, 1.0f});
            structuralEdit({{hx, hy, hz}}, true, 1, 1.25f);
            if (r.hitX) e.vel.x *= -1.0f / 0.3f * 0.6f;
            if (r.hitZ) e.vel.z *= -1.0f / 0.3f * 0.6f;
        }
    }
    if (r.hitX) e.vel.x = e.type == EntType::Debris ? -e.vel.x * 0.3f : 0;
    if (r.hitZ) e.vel.z = e.type == EntType::Debris ? -e.vel.z * 0.3f : 0;
    if (e.onGround) {
        float f = e.type == EntType::Item ? 8.0f : e.type == EntType::Debris ? 5.0f : 6.0f;
        e.vel.x *= std::exp(-sdt * f);
        e.vel.z *= std::exp(-sdt * f);
    }
    bool og = e.onGround;
    supportOnBodies(box, e.vel, og, nullptr, sdt);
    e.onGround = og;
    e.pos = {box.center().x, box.min.y, box.center().z};
    if (e.pos.y < -30) {
        e.dead = true;
        return;
    }

    switch (e.type) {
    case EntType::Item: {
        e.yaw += sdt * 1.5f;
        if (e.age > 300) {
            e.dead = true;
            break;
        }
        vec3 pc = player.pos + vec3(0, 0.6f, 0);
        vec3 d = pc - (e.pos + vec3(0, 0.12f, 0));
        float dist = length(d);
        if (!player.spectator() && e.pickupDelay <= 0 && dist < 1.9f) {
            if (dist < 0.7f) {
                int left = player.inv.add(e.item, e.count);
                if (left < e.count) {
                    sounds.push_back({Snd::Pop, e.pos, 0.35f, 1.4f + rng_.uniform() * 0.5f});
                    for (int k = 0; k < 4; ++k)
                        particle(PKind::Spark, e.pos + vec3(0, 0.2f, 0), vec3(rng_.range(-1, 1), rng_.range(1.0f, 2.2f), rng_.range(-1, 1)),
                                 0.3f, 0.04f, vec4(1.0f, 0.95f, 0.65f, 1));
                    pickups.push_back({e.item, e.count - left});
                }
                e.count = left;
                if (!left) e.dead = true;
            } else if (player.inv.add(e.item, 0) == 0) {
                e.vel += d / dist * (sdt * 40.0f);
            }
        }
        break;
    }
    case EntType::TNT:
        e.fuse -= sdt;
        if (rng_.uniform() < sdt * 25)
            particle(PKind::Smoke, e.pos + vec3(0, e.h, 0), vec3(0, 1.2f, 0), 0.6f, 0.08f, vec4(0.7f, 0.7f, 0.7f, 0.8f));
        if (e.fuse <= 0) {
            e.dead = true;
            explode(e.pos + vec3(0, e.h * 0.5f, 0), e.power, player);
        }
        break;
    case EntType::Falling:
        if (e.onGround) landFalling(e);
        break;
    case EntType::Debris:
        e.angle += e.spin * sdt;
        if (e.onGround) {
            e.spin *= std::exp(-sdt * 6);
            if (lengthSq(e.vel) < 0.3f) e.rest += sdt;
        }
        if (e.rest > 0.35f || e.age > 15.0f) landDebris(e);
        break;
    default: break;
    }
}

void Sim::finishEntities(float dt) {
    iterating_ = false;
    commitEntities();
    itemMergeTimer_ += dt;
    if (itemMergeTimer_ >= .25f) {
        itemMergeTimer_ = std::fmod(itemMergeTimer_, .25f);
        mergeItems();
    }
    deleteDeadEntities();
}

void Sim::mergeItems() {
    {
        using Bucket = std::vector<Entity*>;
        std::unordered_map<uint64_t, Bucket> bins;
        bins.reserve(size_t(items_.count()) / 2 + 8);
        auto key = [](int x, int y, int z) -> uint64_t {
            constexpr int B = 1 << 20;
            uint64_t X = uint32_t(x + B) & 0x1FFFFFu;
            uint64_t Y = uint32_t(y + B) & 0x1FFFFFu;
            uint64_t Z = uint32_t(z + B) & 0x1FFFFFu;
            return (X << 42) | (Z << 21) | Y;
        };
        for (Entity& e : items_) {
            if (e.dead || e.count <= 0) continue;
            int bx = int(std::floor(e.pos.x)), by = int(std::floor(e.pos.y)), bz = int(std::floor(e.pos.z));
            for (int dz = -1; dz <= 1 && !e.dead; ++dz)
                for (int dy = -1; dy <= 1 && !e.dead; ++dy)
                    for (int dx = -1; dx <= 1 && !e.dead; ++dx) {
                        auto it = bins.find(key(bx + dx, by + dy, bz + dz));
                        if (it == bins.end()) continue;
                        for (Entity* other : it->second) {
                            Entity& o = *other;
                            if (o.dead || o.type != EntType::Item || o.item != e.item || lengthSq(o.pos - e.pos) >= .8f) continue;
                            int room = maxStack(o.item) - o.count;
                            if (room <= 0) continue;
                            int moved = std::min(room, e.count);
                            o.count += moved;
                            e.count -= moved;
                            if (e.count <= 0) {
                                e.dead = true;
                                break;
                            }
                        }
                    }
            if (!e.dead) bins[key(bx, by, bz)].push_back(&e);
        }
    }
}

}
