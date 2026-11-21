/**
 * @file DefenseSystem.cpp
 * @brief Clan alerts, defense quota dispatch and support patrols.
 */
#include "systems/DefenseSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <spdlog/spdlog.h>
#include <string>

void DefenseSystem::baseOnDamage(entt::entity e, int owner, int attacker)
{
    // Cheap gate only; the real work is deferred to flushAlerts().
    if (!isPlayer(owner) || attacker < 1 || attacker == owner || world.clock < world.bp[owner].alertNext ||
        !world.hostile(attacker, owner))
        return;
    world.bp[owner].alertNext = world.clock + 12.0;
    pendingAlerts.push_back({owner, world.posOf(e), attacker});
}

// Land route from a member's home to the fight. The search is bounded so unreachable victims cannot stall a tick.
DefRoute DefenseSystem::planDefenseRoute(int /*member*/, Pos from, Pos to)
{
    DefRoute r;
    std::vector<Pos> path;
    float straight = std::hypot(to.x - from.x, to.y - from.y);
    if (straight < 8.f)
    {
        r.ok = true;
        r.eta = straight / 2.7f;
        return r;
    }
    if (!world.findPath({int(std::floor(from.x)), int(std::floor(from.y))},
                        {int(std::floor(to.x)), int(std::floor(to.y))}, false, 0, path, 15000))
        return r;
    float len = 0;
    Pos prev = from;
    for (auto &wp : path)
    {
        len += std::hypot(wp.x - prev.x, wp.y - prev.y);
        prev = wp;
    }
    float rest = std::hypot(to.x - prev.x, to.y - prev.y);
    if (std::getenv("BW_BASEDEBUG"))
        spdlog::info("[route] straight {:.0f} path {:.0f} remaining {:.0f}", straight, len, rest);
    if (rest > 15.f && rest > straight * .6f)
        return r; // no real progress on foot: unreachable (partial paths continue while repathing)
    r.ok = true;
    r.eta = (len + rest * 1.3f) / 2.7f;
    r.path = std::move(path);
    return r;
}

bool DefenseSystem::idleArmy(entt::entity e, Kind only)
{
    Kind k = reg.get<Info>(e).kind;
    if (only != K_Count ? k != only : !isArmy(k))
        return false;
    if (reg.any_of<Attack, AMove, Convoy, Haul, Raider, Support, Extinguish, Worker, Aboard, Boarding>(e))
        return false;
    return reg.get<Unit>(e).path.empty();
}

std::vector<entt::entity> DefenseSystem::pickIdleArmy(int owner, Pos near2, int n, Kind only)
{
    std::vector<std::pair<float, entt::entity>> c;
    if (n <= 0)
        return {};
    reg.view<Owner, Unit, Info, Pos>().each(
        [&](entt::entity e, const Owner &o, const Unit &, const Info &, const Pos &p)
    {
        if (o.id == owner && idleArmy(e, only))
            c.push_back({std::hypot(p.x - near2.x, p.y - near2.y), e});
    });
    std::sort(c.begin(), c.end(), [](auto &a, auto &b) { return a.first < b.first; });
    std::vector<entt::entity> out;
    for (auto &[d, e] : c)
    {
        if (int(out.size()) >= n)
            break;
        out.push_back(e);
    }
    return out;
}

Pos DefenseSystem::homeOf(int id, Pos fallback)
{
    auto th = world.townHallOf(id);
    return th == entt::null ? fallback : world.posOf(th);
}

void DefenseSystem::sendPatrol(entt::entity e, Pos dest, int victim, bool defense, bool route)
{
    int ow = reg.get<Owner>(e).id;
    Pos p = reg.get<Pos>(e);
    Support sp;
    sp.dest = dest;
    sp.home = homeOf(ow, p);
    sp.victim = uint8_t(victim);
    sp.defense = defense;
    reg.remove<Attack>(e);
    reg.emplace_or_replace<Support>(e, sp);
    if (!route)
        return; // a ship carries the unit; it is ordered to the fight after landing
    reg.emplace_or_replace<AMove>(e, AMove{dest});
    world.order(e, dest, 0, 12000);
}

// Idle troops still needed per quota type: quota minus those already on the way.
std::vector<entt::entity> DefenseSystem::pickDefenders(int m, int victim, Pos at, int &need)
{
    int existing[QUOTA_TYPES]{};
    reg.view<Support, Owner, Info>().each([&](const Support &s, const Owner &o, const Info &i)
    {
        if (o.id != m || !s.defense || s.victim != victim)
            return;
        for (int t = 0; t < QUOTA_TYPES; t++)
            if (QUOTA_KINDS[t] == i.kind)
                existing[t]++;
    });

    std::vector<entt::entity> us;
    for (int t = 0; t < QUOTA_TYPES; t++)
    {
        int want = int(world.bp[m].quota[t]) - existing[t];
        need += std::max(0, want);
        for (auto e : pickIdleArmy(m, at, want, QUOTA_KINDS[t]))
            us.push_back(e);
    }
    return us;
}

void DefenseSystem::announceDefense(int m, int victim, int attacker, size_t n, bool boat, float eta)
{
    std::string how = boat ? " by ship" : "";
    world.say(m, "Defending " + world.pl[victim].name + " against " + world.ownerName(attacker) + ": " +
                     std::to_string(n) + " troops dispatched" + how + " (ETA " + std::to_string(int(eta)) + " s).");
    world.say(victim, world.pl[m].name + " sends " + std::to_string(n) + " troops to defend you" + how + " (ETA " +
                          std::to_string(int(eta)) + " s).");
}

void DefenseSystem::dispatchDefense(int victim, Pos at, int attacker)
{
    int c = world.pl[victim].clan;
    if (!c || world.clock < world.bp[victim].defNext)
        return;
    world.bp[victim].defNext = world.clock + 20.0;

    int total = 0;
    for (int m = 1; m <= MAX_PLAYERS && total < 60; m++)
    {
        if (m == victim || !world.pl[m].used || world.pl[m].clan != c || world.bp[m].quotaTotal() == 0 ||
            world.bp[m].homeless)
            continue;
        auto th = world.townHallOf(m);
        if (th == entt::null)
            continue;

        int need = 0;
        auto us = pickDefenders(m, victim, at, need);
        if (us.empty())
            continue; // skip the route search entirely

        Pos from = world.posOf(th);
        world.spawnSpot(th, false, from); // the hall centre is blocked; take a free tile beside it
        DefRoute rt = planDefenseRoute(m, from, at);
        if (std::getenv("BW_BASEDEBUG"))
            spdlog::info("[defense] {} -> victim {}: route ok={} eta={:.0f} need={}", world.pl[m].name,
                         world.pl[victim].name, int(rt.ok), rt.eta, need);

        // Cross water by boat when clearly faster than marching around.
        TransportPlan plan = s.ferry.planTransport(m, us, at);
        bool boat = plan.useBoat && !plan.deferred && s.ferry.startTransport(plan, us, at);
        if (!rt.ok && !boat)
            continue; // neither a land route nor a free ship

        float eta = boat ? plan.etaBoat : rt.eta;
        // The first path order comes from supportSecond() under its search budget.
        for (auto e : us)
            sendPatrol(e, at, victim, true, false);

        total += int(us.size());
        announceDefense(m, victim, attacker, us.size(), boat, eta);
    }
}

void DefenseSystem::alertFriends(int victim, Pos at, int attacker)
{
    std::string txt = world.pl[victim].name + " is under attack by " + world.ownerName(attacker) + "!";
    for (int i = 1; i <= MAX_PLAYERS; i++)
    {
        if (i == victim || !world.pl[i].online)
            continue;
        if (!(world.sameClan(i, victim) || world.pl[i].allied[victim]))
            continue;
        world.sendAlert(i, 0, at, txt);
        world.say(i, txt + " (" + std::to_string(int(at.x)) + ", " + std::to_string(int(at.y)) + ")");
    }
}

void DefenseSystem::flushAlerts()
{
    if (pendingAlerts.empty())
        return;
    auto v = std::move(pendingAlerts);
    pendingAlerts.clear();
    for (auto &a : v)
    {
        alertFriends(a.victim, a.at, a.attacker);
        dispatchDefense(a.victim, a.at, a.attacker);
    }
}

void DefenseSystem::doSupport(int id, Pos dest, int n)
{
    if (!std::isfinite(dest.x) || !std::isfinite(dest.y) || n < 1)
        return;
    n = std::min(n, MAX_QUOTA);
    if (world.bp[id].homeless || world.townHallOf(id) == entt::null)
    {
        world.say(id, "You need a town to send troops from.");
        return;
    }
    if (world.clock < world.bp[id].supNext)
    {
        world.say(id, "Your troops are still preparing the last patrol.");
        return;
    }
    bool ok = false;
    reg.view<Info, Building, Owner>().each([&](const Info &i, const Building &b, const Owner &o)
    {
        if (!hasTag(i.kind, KT_Anchor) || o.id == id || !isPlayer(o.id) || !world.allied(id, o.id))
            return;
        Pos c = footCenter(i.kind, b.tx, b.ty);
        if (std::max(std::abs(c.x - dest.x), std::abs(c.y - dest.y)) <= 12.f)
            ok = true;
    });
    if (!ok)
    {
        world.say(id, "Support patrols can only go to allied or clan towns and outposts.");
        return;
    }
    auto us = pickIdleArmy(id, dest, n);
    if (us.empty())
    {
        world.say(id, "No idle army units available for a patrol.");
        return;
    }
    for (auto e : us)
        sendPatrol(e, dest, 0, false, false);
    world.bp[id].supNext = world.clock + 6.0;
    world.say(id, "Support patrol of " + std::to_string(us.size()) +
                      " troops sets out. They help fight, then return home.");
}

// Phase 0: march to the destination.
void DefenseSystem::supportOutbound(entt::entity e, Support &sp, Unit &u, float dDest, int &budget)
{
    sp.travel += 1.f;
    if (dDest <= 8.f)
    {
        sp.phase = 1;
        sp.calm = 0;
    }
    else if (sp.travel > 900.f || sp.fails > 5)
        sp.phase = 2;
    else if (u.path.empty() && !reg.all_of<Attack>(e) && budget > 0)
    {
        budget--;
        if (world.order(e, sp.dest, 0, 12000))
            reg.emplace_or_replace<AMove>(e, AMove{sp.dest});
        else
            sp.fails++;
    }
}

// Phase 1: help fight until nothing hostile has been near for 20 s.
void DefenseSystem::supportFight(entt::entity e, Support &sp, Unit &u, bool hostileNear, float dDest, int &budget)
{
    sp.calm = hostileNear ? 0.f : sp.calm + 1.f;
    if (sp.calm >= 20.f)
        sp.phase = 2;
    else if (dDest > 14.f && u.path.empty() && !reg.all_of<Attack>(e) && budget > 0)
    {
        budget--;
        if (world.order(e, sp.dest, 0, 12000))
            reg.emplace_or_replace<AMove>(e, AMove{sp.dest});
    }
}

// Phase 2: wander back home; the Support component is removed on arrival or give-up.
void DefenseSystem::supportReturn(entt::entity e, Support &sp, Unit &u, float dHome, int &budget)
{
    if (dHome <= 6.f || sp.fails > 4 || hasTag(reg.get<Info>(e).kind, KT_Naval))
    {
        reg.remove<Support>(e);
        return;
    }
    if (u.path.empty() && !reg.all_of<Attack>(e) && budget > 0)
    {
        budget--;
        float a = float(world.rng() % 628) / 100.f, rr = 1.5f + float(world.rng() % 300) / 100.f;
        if (!world.order(e, {sp.home.x + std::cos(a) * rr, sp.home.y + std::sin(a) * rr}, 0, 12000))
            sp.fails++;
    }
}

void DefenseSystem::supportSecond()
{
    std::vector<entt::entity> v;
    reg.view<Support, Unit, Pos, Owner>().each(
        [&](entt::entity e, const Support &, const Unit &, const Pos &, const Owner &) { v.push_back(e); });

    int budget = 8; // path searches per second
    for (auto e : v)
    {
        // Ferried units are handled by the ship.
        if (!reg.valid(e) || world.dying.count(reg.get<Uid>(e).id) || reg.any_of<Aboard, Boarding>(e))
            continue;

        auto &sp = reg.get<Support>(e);
        auto &u = reg.get<Unit>(e);
        Pos p = reg.get<Pos>(e);
        int ow = reg.get<Owner>(e).id;
        float dDest = std::hypot(p.x - sp.dest.x, p.y - sp.dest.y);
        float dHome = std::hypot(p.x - sp.home.x, p.y - sp.home.y);

        if (sp.phase < 2)
        {
            bool hostileNear = world.nearestHostile(p, ow, 20.f, false) != entt::null;
            if (sp.phase == 0)
                supportOutbound(e, sp, u, dDest, budget);
            else
                supportFight(e, sp, u, hostileNear, dDest, budget);

            if (sp.phase == 2)
            {
                reg.remove<AMove>(e);
                reg.remove<Attack>(e);
                u.path.clear();
                u.i = 0;
                sp.fails = 0;
                sp.calm = 0;
            }
        }
        if (sp.phase == 2)
            supportReturn(e, sp, u, dHome, budget);
    }
}
