#include "systems/FactionSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void FactionSystem::addRep(int player, int f, float d) {
    if (!isPlayer(player)) return;
    float& r = world.pl[player].rep[f];
    float old = r;
    r = std::clamp(r + d, -100.f, 100.f);
    if (old > float(HOSTILE_REP) && r <= float(HOSTILE_REP)) world.say(player, std::string(FACTION_NAME[f]) + " now considers you hostile!");
    if (old < float(FRIENDLY_REP) && r >= float(FRIENDLY_REP)) world.say(player, std::string(FACTION_NAME[f]) + " now counts you as a friend and will come to your aid.");
    if (old <= float(HOSTILE_REP) && r > float(HOSTILE_REP)) world.say(player, std::string(FACTION_NAME[f]) + " no longer wants you dead.");
}
void FactionSystem::alertTown(entt::entity victim, uint32_t by) {                    // a faction town defends itself against whoever hit it
    if (!by) return;
    auto* tt = reg.try_get<TownTag>(victim);
    Town* t = tt ? world.townById(tt->town) : nullptr;
    auto atk = world.find(by);
    if (!t || atk == entt::null || world.clock - t->lastAlert < 3.0) return;
    t->lastAlert = world.clock;
    bool bld = isBuilding(reg.get<Info>(atk).kind);
    Pos ap = world.posOf(atk);
    std::vector<entt::entity> us;
    reg.view<TownTag, Unit, Info>().each([&](entt::entity e, const TownTag& g, const Unit&, const Info& i) { if (g.town == t->id && isArmy(i.kind)) us.push_back(e); });
    for (auto e : us) {
        if (!world.order(e, ap, bld ? by : 0)) continue;
        reg.emplace_or_replace<Attack>(e, Attack{by});
    }
}

void FactionSystem::factionAI(entt::entity e, Unit& u, Pos& p, float) {
    auto* hm = reg.try_get<Home>(e);
    if (!hm) return;
    bool busy = reg.all_of<Attack>(e) || reg.all_of<AMove>(e);
    float dh = std::hypot(p.x - hm->x, p.y - hm->y);
    if (world.clock < hm->helpUntil) return;
    if (busy && dh > 24.f) { reg.remove<Attack>(e); reg.remove<AMove>(e); world.order(e, {hm->x, hm->y}); }
    else if (!busy && u.path.empty() && dh > 5.f) world.order(e, {hm->x, hm->y});
}
void FactionSystem::callHelp() {                                                   // friendly factions send troops when a player is attacked
    struct Req { int victim; uint32_t by; Pos at; };
    std::vector<Req> reqs;
    reg.view<Hit, Owner>().each([&](entt::entity e, const Hit& h, const Owner& o) {
        if (!isPlayer(o.id) || world.clock - h.t > 3.0 || reqs.size() >= 6) return;
        reqs.push_back({o.id, h.by, world.posOf(e)});
    });
    for (auto& rq : reqs) {
        auto atk = world.find(rq.by);
        if (atk == entt::null) continue;
        int aow = reg.get<Owner>(atk).id;
        if (!world.hostile(rq.victim, aow)) continue;
        bool bld = isBuilding(reg.get<Info>(atk).kind);
        Pos ap = world.posOf(atk);
        for (int f = 0; f < FACTION_COUNT; f++) {
            if (world.pl[rq.victim].rep[f] < float(FRIENDLY_REP) || aow == FACTION_BASE + f || world.clock < world.pl[rq.victim].helpCool[f]) continue;
            std::vector<std::pair<float, entt::entity>> cand;
            reg.view<Owner, Unit, Info, Home>().each([&](entt::entity e, const Owner& o, const Unit&, const Info& i, const Home&) {
                if (o.id != FACTION_BASE + f || !isArmy(i.kind) || reg.all_of<Attack>(e)) return;
                Pos q = reg.get<Pos>(e);
                cand.push_back({std::hypot(q.x - rq.at.x, q.y - rq.at.y), e});
            });
            std::sort(cand.begin(), cand.end(), [](auto& a, auto& b) { return a.first < b.first; });
            int want = std::min(8, 2 + int(world.pl[rq.victim].rep[f] / 15.f)), sent = 0;
            for (auto& [d, e] : cand) {
                if (sent >= want || d > 160.f) break;
                if (!world.order(e, ap, bld ? rq.by : 0)) continue;
                reg.emplace_or_replace<Attack>(e, Attack{rq.by});
                reg.get<Home>(e).helpUntil = float(world.clock + 45);
                sent++;
            }
            world.pl[rq.victim].helpCool[f] = world.clock + 15;
            if (sent) world.say(rq.victim, std::string(FACTION_NAME[f]) + " sends " + std::to_string(sent) + " troops to your aid!");
        }
    }
}
