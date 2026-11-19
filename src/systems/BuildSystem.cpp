#include "systems/BuildSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

float BuildSystem::buildTimeFor(entt::entity e, Kind k) const {                              // construction time; relocations take much longer
    if (auto* rn = reg.try_get<RelocNew>(e)) return std::max(1.f, rn->total / baseTestSpeed());
    return KINFO[k].buildTime;
}

bool BuildSystem::buildingTick(entt::entity e, float dt) {
    if (world.inactive(e)) return false;
    s.training.trainTick(e, dt);
    return true;
}

void BuildSystem::tick(const std::vector<entt::entity>& blds, float dt) {
    std::unordered_map<uint32_t, int> workerCount;
    reg.view<Worker>().each([&](const Worker& w) { workerCount[w.home]++; });

    // construction, production, towers
    for (auto e : blds) {
        auto& b = reg.get<Building>(e);
        auto& in = reg.get<Info>(e);
        int ow = reg.get<Owner>(e).id;
        Pos c = footCenter(in.kind, b.tx, b.ty);
        if (b.progress < 1.f) {
            b.progress = std::min(1.f, b.progress + dt / buildTimeFor(e, in.kind));
            if (b.progress >= 1.f) {
                world.reveal(ow, int(std::floor(c.x)), int(std::floor(c.y)), KINFO[in.kind].vision);
                world.fx(E_Dust, c.x, c.y);
                world.say(ow, std::string(KINFO[in.kind].name) + " completed.");
            }
            continue;
        }
        if (!buildingTick(e, dt)) continue;                         // non-functional buildings do nothing
        if (isResourceBuilding(in.kind) && isPlayer(ow)) {
            Yield y = yieldOf(in.kind, world.biome(int(std::floor(c.x)), int(std::floor(c.y))), in.kind == K_Harbour);
            float f = levelMul(in.level) * s.clearing.mineMul(e, in.kind, c) * (world.pl[ow].online ? 1.f : OFFLINE_PROD) * dt, tot = 0;   // [world] gold deposits boost mines
            for (int r = 0; r < R_Count; r++) tot += b.stock[r];
            if (tot < 150) for (int r = 0; r < R_Count; r++) b.stock[r] += y.r[r] * f;
            int cnt = workerCount[reg.get<Uid>(e).id];
            if (cnt < 1 || (cnt < 3 && tot > 60)) {
                b.respawn -= dt;
                if (b.respawn <= 0 && world.unitCount(ow) < UNIT_CAP) {
                    Pos at;
                    if (world.spawnSpot(e, false, at)) {
                        auto u = world.makeUnit(K_Pawn, uint8_t(ow), at.x, at.y);
                        reg.emplace<Worker>(u, Worker{reg.get<Uid>(e).id});
                        workerCount[reg.get<Uid>(e).id]++;
                    }
                    b.respawn = cnt < 1 ? 6.f : 25.f;
                }
            }
        }
        if (ow > 0 && KINFO[in.kind].dps > 0) {                  // defensive fire (Town Hall, Outpost)
            float range = KINFO[in.kind].range;
            entt::entity best = world.nearestHostile(c, ow, range, false);
            b.cd -= dt;
            if (best != entt::null) {
                s.combat.damage(best, KINFO[in.kind].dps * levelMul(in.level) * dt, ow, reg.get<Uid>(e).id);
                if (b.cd <= 0) { b.cd = .9f; Pos t = reg.get<Pos>(best); world.fx(E_Arrow, c.x, c.y - 1.2f, t.x, t.y); }
            }
        }
    }
}
