#include "systems/FireSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void FireSystem::ignite(entt::entity b, int byOwner) {
    Kind k = reg.get<Info>(b).kind;
    if (!isBuilding(k) || k == K_Campfire || !world.complete(b)) return;
    int o = reg.get<Owner>(b).id;
    if (!world.hostile(byOwner, o)) return;                                    // guard: only an enemy can set a building on fire
    auto& f = reg.get_or_emplace<BFlags>(b);
    bool fresh = f.burn <= 0;
    f.burn = BURN_TIME;                                                         // each fire arrow refreshes (never stacks) the timer
    f.burnBy = uint8_t(byOwner);
    if (fresh && isPlayer(o)) { Pos p = world.posOf(b); world.say(o, std::string("Your ") + KINFO[k].name + " is on fire! Idle units will try to extinguish it."); (void)p; }
}
// archers shoot fire arrows from troop level 3 (player level); returns after emitting the projectile effect
void FireSystem::archerShot(int ow, Pos from, Pos to, entt::entity tg) {
    bool fire = isPlayer(ow) && world.pl[ow].ulevel[K_Archer] >= 3 && isBuilding(reg.get<Info>(tg).kind) && reg.get<Info>(tg).kind != K_Campfire;
    if (fire) { world.fx(E_FireArrow, from.x, from.y - .4f, to.x, to.y); ignite(tg, ow); }
    else world.fx(E_Arrow, from.x, from.y - .4f, to.x, to.y);
}
bool FireSystem::canExtinguish(entt::entity e) {
    if (reg.any_of<Attack, AMove, Convoy, Haul, Raider, Support, Extinguish, ClearTask, BuildTask>(e)) return false;
    Kind k = reg.get<Info>(e).kind;
    if (!(k == K_Pawn || isArmy(k) || k == K_Monk)) return false;
    if (auto* w = reg.try_get<Worker>(e)) { if (w->phase != 0) return false; }
    return reg.get<Unit>(e).path.empty();
}
void FireSystem::burnSecond() {
    std::vector<entt::entity> burning;
    reg.view<BFlags>().each([&](entt::entity e, BFlags& f) { if (f.burn > 0) burning.push_back(e); });
    if (burning.empty()) return;
    std::unordered_map<uint32_t, int> cnt;
    reg.view<Extinguish>().each([&](const Extinguish& x) { cnt[x.target]++; });
    std::vector<entt::entity> cand;
    for (auto e : burning) {                                                    // damage over time
        if (!reg.valid(e)) continue;
        auto& f = reg.get<BFlags>(e);
        f.burn = std::max(0.f, f.burn - 1.f);
        int ow = reg.get<Owner>(e).id;
        int by = world.hostile(f.burnBy, ow) ? int(f.burnBy) : 0;               // arson credit (loot, kill) only while the arsonist is still an enemy
        if (!by) f.burnBy = 0;
        if (world.dying.count(reg.get<Uid>(e).id)) continue;
        s.combat.damage(e, reg.get<Health>(e).maxhp * BURN_FRAC_PER_SEC, by);
    }
    s.combat.processKills();
    reg.view<Owner, Unit, Info, Pos>().each([&](entt::entity e, const Owner& o, const Unit&, const Info&, const Pos&) { if (isPlayer(o.id) && canExtinguish(e)) cand.push_back(e); });
    for (auto b : burning) {
        if (!reg.valid(b) || reg.get<BFlags>(b).burn <= 0) continue;
        int ow = reg.get<Owner>(b).id;
        if (!isPlayer(ow)) continue;
        uint32_t buid = reg.get<Uid>(b).id;
        int need = 3 - cnt[buid];
        Pos bc = world.posOf(b);
        std::vector<std::pair<float, size_t>> near2;
        for (size_t i = 0; i < cand.size(); i++) {
            if (!reg.valid(cand[i]) || reg.get<Owner>(cand[i]).id != ow) continue;
            Pos q = reg.get<Pos>(cand[i]);
            float d = std::hypot(q.x - bc.x, q.y - bc.y);
            if (d <= 26.f) near2.push_back({d, i});
        }
        std::sort(near2.begin(), near2.end());
        for (auto& [d, i] : near2) {
            if (need <= 0) break;
            auto e = cand[i];
            if (!world.order(e, bc, buid, 4000)) continue;
            reg.emplace_or_replace<Extinguish>(e, Extinguish{buid, 0.f});
            cand[i] = entt::null;
            need--;
        }
    }
}
// per unit and tick: units on firefighting duty (returns true while the unit is busy with it)
bool FireSystem::extinguishTick(entt::entity e, Unit& u, Pos& p, float dt) {
    auto* ex = reg.try_get<Extinguish>(e);
    if (!ex) return false;
    auto tgt = world.find(ex->target);
    auto* bf = tgt == entt::null ? nullptr : reg.try_get<BFlags>(tgt);
    if (tgt == entt::null || !bf || bf->burn <= 0) { reg.remove<Extinguish>(e); return false; }
    Pos bc = world.posOf(tgt);
    if (world.rectDist(p, tgt) <= 1.8f) {
        u.path.clear();
        u.i = 0;
        u.state = US_Extinguish;
        u.aim = uint8_t(int((std::atan2(bc.y - p.y, bc.x - p.x) + 3.14159f) / 6.28319f * 255.f));
        ex->t += dt;
        if (ex->t >= 1.f) {
            ex->t -= 1.f;
            bf->burn = std::max(0.f, bf->burn - EXTINGUISH_PER_SEC);
            world.fx(E_Extinguish, bc.x + float(int(world.rng() % 100) - 50) / 50.f, bc.y + .2f);
            if (bf->burn <= 0) world.say(reg.get<Owner>(tgt).id, std::string("The fire at your ") + KINFO[reg.get<Info>(tgt).kind].name + " is out.");
        }
        return true;
    }
    if (u.path.empty()) { if (!world.order(e, bc, ex->target, 4000)) reg.remove<Extinguish>(e); }
    return true;
}
