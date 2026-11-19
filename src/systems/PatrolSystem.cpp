#include "systems/PatrolSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void PatrolSystem::patrolLoot(Kind k, float mul, int killer, Pos p) {
    static const float base[3][R_Count] = {{10, 12, 0, 6}, {8, 14, 0, 5}, {6, 22, 4, 8}};     // warrior, archer, lancer
    const float* b = base[k == K_Archer ? 1 : (k == K_Lancer ? 2 : 0)];
    float tot = 0;
    int shown = 0;
    for (int r = 0; r < R_Count; r++) {
        int amt = int(std::lround(b[r] * mul * (.7f + float(world.rng() % 60) / 100.f)));
        if (amt <= 0) continue;
        tot += float(amt);
        s.clearing.creditDrop(killer, {p.x + float(shown % 2) * .4f - .2f, p.y + float(shown) * .15f}, r, amt);
        shown++;
    }
    if (isPlayer(killer) && tot > 0) world.say(killer, std::string("Loot from the fallen ") + KINFO[k].name + ": +" + std::to_string(int(tot)) + " resources.");
}
PatGroup* PatrolSystem::groupOf(uint32_t id) { auto it = pgroups.find(id); return it == pgroups.end() ? nullptr : &it->second; }
void PatrolSystem::spawnPatrol(uint32_t gid, PatGroup& g) {
    Town* t = world.townById(g.town);
    if (!t || !t->alive) return;
    int n = 2 + int(world.rng() % 3);
    static const Kind mix[] = {K_Warrior, K_Archer, K_Lancer};
    g.m.clear();
    for (int i = 0; i < n; i++) {
        Kind k = i == 0 ? (world.rng() % 2 ? K_Warrior : K_Lancer) : mix[world.rng() % 3];
        float a = float(i) * 1.3f + float(g.town);
        float x = float(t->cx) + .5f + std::cos(a) * 3.4f, y = float(t->cy) + 3.8f + std::sin(a) * 1.3f;
        if (!world.walkable(int(std::floor(x)), int(std::floor(y)), false, 0)) { x = float(t->cx) + .5f; y = float(t->cy) + 3.8f; }
        auto u = world.makeUnit(k, uint8_t(FACTION_BASE + t->faction), x, y);
        reg.emplace<TownTag>(u, TownTag{t->id});
        reg.emplace<Patrol>(u, Patrol{gid, uint8_t(i), 0.f});
        g.m.push_back(reg.get<Uid>(u).id);
    }
    g.leader = g.m.front();
    static const bool logIt = std::getenv("BW_WORLDLOG") != nullptr;
    if (logIt) std::printf("[world] patrol %u of town %u spawned with %d units\n", gid, g.town, n);
    g.hasWp = false;
    g.wait = world.clock + 2.0;
    g.pending = false;
    g.ret = 0;
    g.tgt = 0;
}
// hook for processKills (before the victim is destroyed): patrol loot and retaliation
void PatrolSystem::worldOnKill(entt::entity e, Kind k, int owner, int killer, Pos p) {
    if (!isFactionOwner(owner) || !isArmy(k)) return;
    const Patrol* pt = reg.try_get<Patrol>(e);
    const TownTag* tt = reg.try_get<TownTag>(e);
    if (!pt && !tt) return;
    if (isPlayer(killer) && killer != owner) patrolLoot(k, pt ? 1.f : .6f, killer, p);
    if (!isPlayer(killer)) return;
    Pos where = p;
    if (const Hit* hit = reg.try_get<Hit>(e)) { auto a = world.find(hit->by); if (a != entt::null) where = world.posOf(a); }
    uint16_t town = tt ? tt->town : uint16_t(0);
    bool told = false;
    for (auto& [gid, g] : pgroups) {
        if (g.m.empty() || (pt ? gid != pt->group : g.town != town)) continue;
        g.ret = killer;
        g.retUntil = world.clock + 240.0;
        g.retPos = where;
        g.tgt = 0;
        told = true;
    }
    static const bool logIt = std::getenv("BW_WORLDLOG") != nullptr;
    if (told && logIt) std::printf("[world] retaliation against player %d started (patrol unit killed)\n", killer);
    if (told) world.say(killer, std::string("The ") + FACTION_NAME[owner - FACTION_BASE] + " patrol is hunting you!");
}
Pos PatrolSystem::groupPos(const PatGroup& g) {
    auto l = world.find(g.leader);
    if (l != entt::null) return reg.get<Pos>(l);
    for (auto id : g.m) { auto e = world.find(id); if (e != entt::null) return reg.get<Pos>(e); }
    return {0, 0};
}
bool PatrolSystem::pickWaypoint(PatGroup& g) {
    Town* t = world.townById(g.town);
    if (!t) return false;
    for (int n = 0; n < 10; n++) {
        float a = float(world.rng() % 6283) / 1000.f, r = 18.f + float(world.rng() % 38);
        int x = int(std::floor(float(t->cx) + std::cos(a) * r)), y = int(std::floor(float(t->cy) + std::sin(a) * r));
        if (!world.walkable(x, y, false, 0) || world.biome(x, y) == B_Highlands) continue;
        g.wp = {float(x) + .5f, float(y) + .5f};
        g.hasWp = true;
        g.fails = 0;
        g.wpUntil = world.clock + 100.0;
        return true;
    }
    return false;
}
// hook for the unit loop: movement of one patrol member (combat is handled by the normal combat() pass)
void PatrolSystem::runPatrol(entt::entity e, Unit& u, Patrol& pt, float dt) {
    pt.repath -= dt;
    if (reg.all_of<Attack>(e) || reg.all_of<AMove>(e)) return;
    PatGroup* g = groupOf(pt.group);
    if (!g) return;
    Pos p = reg.get<Pos>(e);
    uint32_t uid = reg.get<Uid>(e).id;
    if (g->ret) {                                                // retaliation: hunt the perpetrator
        if (g->tgt) {
            auto te = world.find(g->tgt);
            if (te != entt::null && !world.dying.count(g->tgt)) {
                if (pt.repath <= 0) {
                    pt.repath = 1.5f;
                    if (world.order(e, world.posOf(te), isBuilding(reg.get<Info>(te).kind) ? g->tgt : 0)) reg.emplace_or_replace<Attack>(e, Attack{g->tgt});
                }
                return;
            }
        }
        if (u.path.empty() && pt.repath <= 0) {
            pt.repath = 2.5f + float(uid % 7) * .1f;
            if (std::hypot(p.x - g->retPos.x, p.y - g->retPos.y) > 3.f) world.order(e, g->retPos, 0, 20000);   // repeated partial-path replans carry us across the map
        }
        return;
    }
    if (uid == g->leader) {
        if (g->hasWp && (std::hypot(p.x - g->wp.x, p.y - g->wp.y) < 3.f || world.clock > g->wpUntil)) { g->hasWp = false; g->wait = world.clock + 6.0 + double(world.rng() % 12); u.path.clear(); }
        if (!g->hasWp) { if (world.clock >= g->wait) pickWaypoint(*g); return; }
        if (u.path.empty() && world.clock >= g->repath) {
            g->repath = world.clock + 2.0;
            if (world.order(e, g->wp, 0, 9000)) g->fails = 0;
            else if (++g->fails > 2) { g->hasWp = false; g->wait = world.clock + 2.0; g->fails = 0; }
        }
        return;
    }
    auto le = world.find(g->leader);
    if (le == entt::null) return;
    Pos lp = reg.get<Pos>(le);
    float a = float(pt.slot) * 2.1f + 0.7f, rad = 1.5f + .35f * float(pt.slot);
    Pos slot{lp.x + std::cos(a) * rad, lp.y + std::sin(a) * rad};
    float d = std::hypot(p.x - slot.x, p.y - slot.y);
    if (d > 2.2f) {
        if (pt.repath <= 0) { pt.repath = 1.f; world.order(e, slot, 0, 900); }
    } else if (d < 1.0f && !u.path.empty()) u.path.clear();
}
void PatrolSystem::patrolSecond() {
    for (auto& t : world.towns) {                                      // make sure each living town owns its patrol groups
        int want = 1 + int(hash2(t.id, 0, world.seed + 444) & 1), have = 0;
        for (auto& [id, g] : pgroups) if (g.town == t.id) have++;
        if (t.alive && have < want) {
            static const bool fast = std::getenv("BW_PATROLNOW") != nullptr;      // test hook: patrols appear immediately
            PatGroup g;
            g.town = t.id;
            g.pending = true;
            g.respawnAt = world.clock + (fast ? 1.0 : 15.0 + double(world.rng() % 25));
            pgroups[nextGroup++] = g;
        }
    }
    static const bool fastAll = std::getenv("BW_PATROLNOW") != nullptr;
    for (auto& [gid, g] : pgroups) {
        std::vector<uint32_t> alive;
        for (auto id : g.m) { auto e = world.find(id); if (e != entt::null && !world.dying.count(id)) alive.push_back(id); }
        g.m = alive;
        Town* t = world.townById(g.town);
        if (g.m.empty()) {
            g.ret = 0;
            if (!g.pending) { g.pending = true; g.respawnAt = world.clock + (fastAll ? 8.0 : 90.0 + double(world.rng() % 90)); }
            else if (world.clock >= g.respawnAt && t && t->alive) spawnPatrol(gid, g);
            continue;
        }
        if (world.find(g.leader) == entt::null) g.leader = g.m.front();
        auto le = world.find(g.leader);
        Pos lp = reg.get<Pos>(le);
        int owner = reg.get<Owner>(le).id;
        // react to attackers: whoever recently hit a member becomes the group's target
        if (!g.ret) {
            for (auto id : g.m) {
                auto e = world.find(id);
                const Hit* h = reg.try_get<Hit>(e);
                if (!h || world.clock - h->t > 3.0) continue;
                auto atk = world.find(h->by);
                if (atk == entt::null || world.allied(owner, reg.get<Owner>(atk).id) || world.dying.count(h->by)) continue;
                for (auto m2 : g.m) {
                    auto me = world.find(m2);
                    if (me == entt::null || reg.all_of<Attack>(me)) continue;
                    if (world.order(me, world.posOf(atk), isBuilding(reg.get<Info>(atk).kind) ? h->by : 0)) reg.emplace_or_replace<Attack>(me, Attack{h->by});
                }
                break;
            }
            // hostile factions attack on sight (neutral / friendly factions are left alone by hostile()'s rules)
            if (!reg.all_of<Attack>(le)) {
                auto tg = world.nearestHostile(lp, owner, 11.f, true);
                if (tg != entt::null) {
                    uint32_t tu = reg.get<Uid>(tg).id;
                    bool bld = isBuilding(reg.get<Info>(tg).kind);
                    for (auto m2 : g.m) {
                        auto me = world.find(m2);
                        if (me == entt::null || reg.all_of<Attack>(me)) continue;
                        if (world.order(me, world.posOf(tg), bld ? tu : 0)) reg.emplace_or_replace<Attack>(me, Attack{tu});
                    }
                }
            }
            continue;
        }
        // retaliation bookkeeping: give up after a while, otherwise keep a fresh idea of where the perpetrator is
        if (world.clock > g.retUntil || !world.pl[g.ret].used) { g.ret = 0; g.tgt = 0; g.hasWp = false; g.wait = world.clock + 1.0; continue; }
        if (g.tgt && (world.find(g.tgt) == entt::null || world.dying.count(g.tgt))) g.tgt = 0;
        float bestNear = 1e9f, bestAny = 1e9f;
        uint32_t nearUid = 0, anyUid = 0;
        Pos nearPos{0, 0}, anyPos{0, 0};
        reg.view<Owner, Uid, Info>().each([&](entt::entity e, const Owner& o, const Uid& u, const Info&) {
            if (o.id != g.ret || world.dying.count(u.id)) return;
            Pos q = world.posOf(e);
            float d = std::hypot(q.x - lp.x, q.y - lp.y);
            if (d < bestAny) { bestAny = d; anyUid = u.id; anyPos = q; }
            for (auto id : g.m) { auto me = world.find(id); if (me != entt::null) { Pos mp = reg.get<Pos>(me); float dm = std::hypot(q.x - mp.x, q.y - mp.y); if (dm < bestNear) { bestNear = dm; nearUid = u.id; nearPos = q; } } }
        });
        if (!anyUid) { g.ret = 0; g.tgt = 0; continue; }          // the perpetrator has nothing left to hunt
        if (nearUid && bestNear <= 20.f) { g.tgt = nearUid; g.retPos = nearPos; }
        else {
            g.tgt = 0;
            if (std::hypot(lp.x - g.retPos.x, lp.y - g.retPos.y) < 5.f) g.retPos = anyPos;   // nothing here: hunt the nearest entity of that player
        }
    }
}
