#include "systems/UnitSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void UnitSystem::behave(const std::vector<entt::entity>& units, float dt) {
    for (auto e : units) {
        if (!reg.valid(e) || world.dying.count(reg.get<Uid>(e).id) || reg.all_of<Aboard>(e)) continue;
        auto& u = reg.get<Unit>(e);
        Kind k = reg.get<Info>(e).kind;
        int ow = reg.get<Owner>(e).id;
        Pos& p = reg.get<Pos>(e);
        u.state = US_Idle;
        u.cd -= dt;
        u.repath -= dt;
        if (reg.all_of<Wander>(e)) {
            auto& wd = reg.get<Wander>(e);
            wd.timer -= dt;
            if (wd.timer <= 0 && u.path.empty()) {
                wd.timer = 2.f + float(world.rng() % 400) / 100.f;
                int nx = int(std::floor(p.x)) + int(world.rng() % 7) - 3, ny = int(std::floor(p.y)) + int(world.rng() % 7) - 3;
                if (world.walkable(nx, ny, false, 0) && world.biome(nx, ny) != B_Highlands) { u.path = {{float(nx) + .5f, float(ny) + .5f}}; u.i = 0; }
            }
        }
        if (s.fire.extinguishTick(e, u, p, dt)) continue;                  // firefighting duty
        if (isFactionOwner(ow)) s.faction.factionAI(e, u, p, dt);
        if (auto* ct = reg.try_get<ClearTask>(e)) { s.clearing.pawnTick(e, u, *ct, p, dt); }   // clearing an obstacle (prior work is restored after)
        else if (auto* bt = reg.try_get<BuildTask>(e)) { s.construction.pawnTick(e, u, *bt, p, dt); }   // building / upgrading / repairing a site
        else if (auto* wk = reg.try_get<Worker>(e)) { s.convoy.runWorker(e, u, *wk, dt); }
        else if (auto* cv = reg.try_get<Convoy>(e)) { s.convoy.runConvoy(e, u, *cv, dt); }
        else if (auto* hl = reg.try_get<Haul>(e)) { s.convoy.runHaul(e, u, *hl, dt); }
        else if (auto* bd = reg.try_get<Boarding>(e)) { s.naval.runBoarding(e, u, *bd, dt); }
        else if (auto* rd = reg.try_get<Raider>(e)) { s.raid.runRaider(e, u, *rd, dt); if (reg.valid(e) && KINFO[k].dps > 0) s.combat.fight(e, u, k, ow, p, dt); }
        else if (k == K_Pawn && isPlayer(ow) && s.hunt.huntTick(e, u, ow, p, dt)) {}   // [world] free pawns hunt sheep
        else if (world.isPatrol(e)) { s.patrol.runPatrol(e, u, reg.get<Patrol>(e), dt); if (reg.valid(e) && KINFO[k].dps > 0) s.combat.fight(e, u, k, ow, p, dt); }   // [world] faction patrol
        else if (k == K_Monk) {                                  // heal the most wounded friendly unit in range
            entt::entity best = entt::null;
            float bn = 1e9f;
            for (auto o : units) {
                if (o == e || !reg.valid(o) || reg.all_of<Aboard>(o) || !world.allied(ow, reg.get<Owner>(o).id)) continue;
                auto& h = reg.get<Health>(o);
                if (h.hp >= h.maxhp - .5f) continue;
                Pos q = reg.get<Pos>(o);
                float d = std::hypot(p.x - q.x, p.y - q.y);
                if (d <= KINFO[K_Monk].range && h.hp / h.maxhp < bn) { bn = h.hp / h.maxhp; best = o; }
            }
            if (best != entt::null) {
                auto& h = reg.get<Health>(best);
                h.hp = std::min(h.maxhp, h.hp + KINFO[K_Monk].dps * unitDmgMul(world.ulv(ow, K_Monk)) * dt);
                u.state = US_Heal;
                u.path.clear();
                Pos q = reg.get<Pos>(best);
                u.aim = uint8_t(int((std::atan2(q.y - p.y, q.x - p.x) + 3.14159f) / 6.28319f * 255.f));
                if (u.cd <= 0) { u.cd = 1.f; world.fx(E_Heal, q.x, q.y); }
            }
        } else if (KINFO[k].dps > 0 && ow > 0) {
            s.combat.fight(e, u, k, ow, p, dt);
        }
    }
}

void UnitSystem::move(const std::vector<entt::entity>& units, float dt) {
    for (auto e : units) {
        if (!reg.valid(e)) continue;
        auto& u = reg.get<Unit>(e);
        Pos& p = reg.get<Pos>(e);
        Kind k = reg.get<Info>(e).kind;
        if (u.i >= u.path.size()) { if (!u.path.empty()) u.path.clear(); u.i = 0; if (reg.all_of<AMove>(e) && u.state != US_Attack) reg.remove<AMove>(e); continue; }
        float step = KINFO[k].speed * unitSpeedMul(world.ulv(reg.get<Owner>(e).id, k)) * dt;
        Pos t = u.path[u.i];
        float dx = t.x - p.x, dy = t.y - p.y, d = std::hypot(dx, dy);
        u.aim = uint8_t(int((std::atan2(dy, dx) + 3.14159f) / 6.28319f * 255.f));
        if (u.state == US_Idle) u.state = US_Move;
        if (d <= step) { p = t; u.i++; }
        else { p.x += dx / d * step; p.y += dy / d * step; }
    }
}
