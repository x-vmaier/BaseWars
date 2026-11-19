#include "systems/CombatSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void CombatSystem::lootStorage(entt::entity e, float amt, int attacker) {
    Kind k = reg.get<Info>(e).kind;
    if (!isStorage(k) || !world.complete(e)) return;
    int o = reg.get<Owner>(e).id;
    float hpBefore = reg.get<Health>(e).hp + amt, frac = std::clamp(amt / std::max(1.f, hpBefore), 0.f, 1.f);
    float* stock = (k == K_TownHall && isPlayer(o)) ? world.pl[o].res : reg.get<Building>(e).stock;
    float taken[R_Count], tot = 0;
    for (int r = 0; r < R_Count; r++) { taken[r] = stock[r] * frac; stock[r] -= taken[r]; tot += taken[r]; }
    if (tot <= 0) return;
    if (isPlayer(attacker)) {
        std::vector<int> mates;
        for (int i = 1; i <= MAX_PLAYERS; i++) if (i != attacker && world.pl[i].used && world.sameClan(attacker, i)) mates.push_back(i);
        float mine = mates.empty() ? 1.f : .5f;              // a clan shares half of its raiders' loot
        for (int r = 0; r < R_Count; r++) {
            world.pl[attacker].res[r] += taken[r] * mine;
            for (int m : mates) world.pl[m].res[r] += taken[r] * (1.f - mine) / float(mates.size());
        }
    } else if (isFactionOwner(attacker)) world.facDonated[attacker - FACTION_BASE] += tot;
    if (isPlayer(o) && world.pl[o].online && world.clock - world.pl[o].lastLootWarn > 10) { world.pl[o].lastLootWarn = world.clock; world.say(o, std::string("Your ") + KINFO[k].name + " is being looted by " + world.ownerName(attacker) + "!"); }
}
void CombatSystem::damage(entt::entity e, float amt, int attacker, uint32_t by) {
    auto& h = reg.get<Health>(e);
    h.hp -= amt;
    h.lastHit = uint8_t(attacker);
    int o = reg.get<Owner>(e).id;
    s.defense.baseOnDamage(e, o, attacker);                                    // clan alerts / defense dispatch
    if (by) reg.emplace_or_replace<Hit>(e, Hit{by, world.clock});
    if (isFactionOwner(o) && isPlayer(attacker)) {
        s.faction.addRep(attacker, o - FACTION_BASE, -amt * .06f);
        s.faction.alertTown(e, by);
    }
    if (attacker > 0 && attacker != o && world.hostile(attacker, o)) lootStorage(e, amt, attacker);
    if (isPlayer(o) && attacker != o && world.pl[o].online && world.clock - world.pl[o].lastWarn > 8) { world.pl[o].lastWarn = world.clock; world.say(o, "Your forces are under attack!"); }
    uint32_t uid = reg.get<Uid>(e).id;
    if (h.hp <= 0 && world.dying.insert(uid).second) world.toKill.push_back({e, attacker});
}
void CombatSystem::processKills() {
    for (size_t n = 0; n < world.toKill.size(); n++) {
        auto [e, killer] = world.toKill[n];
        if (!reg.valid(e)) continue;
        Kind k = reg.get<Info>(e).kind;
        int owner = reg.get<Owner>(e).id;
        Pos p = world.posOf(e);
        if (isBuilding(k)) world.fx(E_Explosion, p.x, p.y);
        auto raid = [&](const float* cargo) {
            float tot = 0;
            for (int r = 0; r < R_Count; r++) tot += cargo[r];
            if (tot <= 0) return;
            if (isPlayer(killer) && killer != owner && !world.sameClan(killer, owner)) {
                for (int r = 0; r < R_Count; r++) world.pl[killer].res[r] += cargo[r];
                world.say(killer, "You raided a supply party: +" + std::to_string(int(tot)) + " resources!");
            }
            world.say(owner, "Your convoy was destroyed!");
        };
        if (const Worker* w = reg.try_get<Worker>(e)) raid(w->cargo);
        if (const Convoy* c = reg.try_get<Convoy>(e)) raid(c->cargo);
        if (const Haul* hl = reg.try_get<Haul>(e)) raid(hl->cargo);
        if (k == K_Sheep && isPlayer(killer) && !s.hunt.huntKill(e, killer)) { world.pl[killer].res[R_Meat] += 12; world.say(killer, "Hunted a sheep: +12 meat."); }
        s.patrol.worldOnKill(e, k, owner, killer, p);                         // [world] patrol loot + retaliation
        if (isFactionOwner(owner) && isPlayer(killer)) s.faction.addRep(killer, owner - FACTION_BASE, k == K_TownHall ? -40.f : (isBuilding(k) ? -10.f : -4.f));
        uint16_t town = reg.all_of<TownTag>(e) ? reg.get<TownTag>(e).town : uint16_t(0);
        world.destroyNow(e);
        if (k == K_TownHall && isFactionOwner(owner)) s.town.townDestroyed(town, killer);
        else if (k == K_TownHall && isPlayer(owner)) {
            bool other = false;
            reg.view<Info, Owner>().each([&](const Info& i, const Owner& o) { if (i.kind == K_TownHall && o.id == owner) other = true; });
            if (!other) defeat(owner, killer);
        }
    }
    world.toKill.clear();
}

void CombatSystem::defeat(int victim, int killer) {
    if (isPlayer(killer) && killer != victim && world.pl[killer].used && !world.sameClan(killer, victim)) {
        float tot = 0;
        for (int r = 0; r < R_Count; r++) { float l = std::floor(world.pl[victim].res[r] * .5f); world.pl[killer].res[r] += l; tot += l; }
        world.say(killer, "You destroyed " + world.pl[victim].name + "'s Town Hall! Loot: " + std::to_string(int(tot)) + " resources.");
    }
    if (s.clan.baseDefeat(victim)) return;                                    // clan members become homeless
    world.removeOwner(victim);
    world.resetProfile(victim);
    s.spawn.spawnBase(victim, world.bp[victim].friends);                            // respawn near Steam friends / allies
    world.pl[victim].sentFog.clear();
    world.pl[victim].sentTerr.clear();
    world.say(victim, "Your Town Hall fell! You rebuild at a fresh clearing elsewhere in the world.");
    std::printf("%s was defeated\n", world.pl[victim].name.c_str());
}

void CombatSystem::fight(entt::entity e, Unit& u, Kind k, int ow, Pos& p, float dt) {
    entt::entity tg = entt::null;
    bool explicitOrder = false;
    if (auto* a = reg.try_get<Attack>(e)) {
        tg = world.find(a->target);
        explicitOrder = true;
        if (tg == entt::null || world.dying.count(a->target) || world.allied(ow, reg.get<Owner>(tg).id) || reg.all_of<Aboard>(tg)) { reg.remove<Attack>(e); tg = entt::null; explicitOrder = false; }
    }
    bool amove = reg.all_of<AMove>(e);
    if (tg == entt::null && (isArmy(k) || amove) && u.repath <= 0) {
        float aggro = amove ? 7.f : 5.f;
        tg = world.nearestHostile(p, ow, aggro, amove);
        if (tg == entt::null) u.repath = .3f;
    }
    if (tg == entt::null) return;
    Pos tp = world.posOf(tg);
    float d = world.rectDist(p, tg);
    u.aim = uint8_t(int((std::atan2(tp.y - p.y, tp.x - p.x) + 3.14159f) / 6.28319f * 255.f));
    if (d <= KINFO[k].range) {
        u.path.clear();
        u.i = 0;
        u.state = US_Attack;
        damage(tg, KINFO[k].dps * unitDmgMul(world.ulv(ow, k)) * damageVs(k, reg.get<Info>(tg).kind) * armorOf(reg.get<Info>(tg).kind) * dt, ow, reg.get<Uid>(e).id);
        if (k == K_Archer && u.cd <= 0) { u.cd = .8f; s.fire.archerShot(ow, p, tp, tg); }   // fire arrows at troop level 3
    } else if (u.repath <= 0 || u.path.empty()) {
        u.repath = 1.f;
        bool bld = isBuilding(reg.get<Info>(tg).kind);
        if (!world.order(e, tp, bld ? reg.get<Uid>(tg).id : 0) && explicitOrder) reg.remove<Attack>(e);
        if (!explicitOrder && !reg.all_of<Attack>(e)) { /* auto-acquired targets are re-evaluated each pass */ }
    }
    if (!explicitOrder && tg != entt::null && !reg.all_of<Attack>(e) && d > KINFO[k].range && !u.path.empty()) u.state = US_Move;
}
