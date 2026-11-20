/**
 * @file ConvoyHaulSystem.cpp
 * @brief Resource logistics: building workers, clan tribute convoys (+ clan spotting), hauling parties.
 */
#include "systems/ConvoyHaulSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void ConvoyHaulSystem::refundConvoy(entt::entity e, const Convoy &cv, const std::string &why)
{
    for (int r = 0; r < R_Count; r++)
        if (isPlayer(cv.from))
            world.pl[cv.from].res[r] += cv.cargo[r];

    world.say(cv.from, "Convoy turned back: " + why + ". Resources returned.");
    world.destroyNow(e);
}

entt::entity ConvoyHaulSystem::recipientHall(const Convoy &cv, Pos p)
{
    entt::entity th = entt::null;
    float bd = 1e9f;

    reg.view<Info, Building, Owner>().each([&](entt::entity b, const Info &i, const Building &bb, const Owner &o)
    {
        if (i.kind != K_TownHall || o.id != cv.to || bb.progress < 1.f)
            return;

        Pos c = footCenter(i.kind, bb.tx, bb.ty);
        float d = std::hypot(c.x - p.x, c.y - p.y);
        if (d < bd)
        {
            bd = d;
            th = b;
        }
    });

    return th;
}

void ConvoyHaulSystem::deliverConvoy(entt::entity e, const Convoy &cv, Pos p)
{
    float tot = 0;
    for (int r = 0; r < R_Count; r++)
    {
        world.pl[cv.to].res[r] += cv.cargo[r];
        tot += cv.cargo[r];
    }

    world.say(cv.to, "A convoy from " + world.ownerName(cv.from) + " arrived with " + std::to_string(int(tot)) +
                         " resources.");
    world.say(cv.from, "Your convoy reached " + world.ownerName(cv.to) + ".");
    world.fx(E_Dust, p.x, p.y);
    world.destroyNow(e);
}

void ConvoyHaulSystem::runConvoy(entt::entity e, Unit &u, Convoy &cv, float dt)
{
    Pos p = reg.get<Pos>(e);
    entt::entity th = recipientHall(cv, p);

    if (th == entt::null || !isPlayer(cv.to) || !world.pl[cv.to].used || !world.sameClan(cv.from, cv.to))
    {
        refundConvoy(e, cv, "the recipient is no longer reachable");
        return;
    }
    if (world.rectDist(p, th) <= 1.6f)
    {
        deliverConvoy(e, cv, p);
        return;
    }

    cv.repath -= dt;
    if (u.path.empty() && cv.repath <= 0)
    {
        cv.repath = 3.f;
        float d = world.rectDist(p, th);
        if (d < cv.bestDist - 2.f)
        {
            cv.bestDist = d;
            cv.stuck = 0;
        }
        else if (++cv.stuck >= 8)
        {
            refundConvoy(e, cv, "no land route");
            return;
        }

        if (!world.order(e, world.posOf(th), reg.get<Uid>(th).id, 40000))
            cv.stuck += 2;
    }
}

void ConvoyHaulSystem::updateSpotting()
{
    std::vector<entt::entity> cvs;
    reg.view<Convoy>().each([&](entt::entity e, const Convoy &) { cvs.push_back(e); });
    reg.view<Haul>().each([&](entt::entity e, const Haul &) { cvs.push_back(e); });

    for (auto &[c, m] : clanSpot)
        std::erase_if(m, [&](auto &kv) { return kv.second.until < world.clock; });
    if (cvs.empty())
        return;

    std::vector<Source> srcs[MAX_PLAYERS + 1];
    for (int pid = 1; pid <= MAX_PLAYERS; pid++)
        if (world.pl[pid].used && world.pl[pid].clan)
            srcs[pid] = world.sourcesFor(pid);

    for (auto e : cvs)
    {
        int owner = reg.get<Owner>(e).id;
        Pos p = reg.get<Pos>(e);
        uint32_t uid = reg.get<Uid>(e).id;

        for (int pid = 1; pid <= MAX_PLAYERS; pid++)
        {
            if (!world.pl[pid].used || !world.pl[pid].clan || world.allied(pid, owner) || !seen(srcs[pid], p.x, p.y))
                continue;

            auto &m = clanSpot[world.pl[pid].clan];
            bool fresh = m.find(uid) == m.end();
            m[uid] = {world.clock + 6.0, uint8_t(pid)};
            if (fresh)
                world.notifyClan(world.pl[pid].clan, "Enemy convoy of " + world.ownerName(owner) + " spotted by " +
                                                         world.pl[pid].name + " at (" + std::to_string(int(p.x)) +
                                                         ", " + std::to_string(int(p.y)) + ")!");
        }
    }
}

// Hauling parties: why a party only leaves when the hall has room. Root cause of the old endless haul / "burning
// convoy" loop, traced deterministically:
//   1. spawnHauls() only looked at the storage fill (stock/cap >= haulPct) and never at the Town Hall. The hall's stock
//      is the owner's whole pool (totalRes), so once that pool reached the hall cap, storeCargo(th) stored nothing.
//   2. runHaul() then called giveUp(), which teleported the full cargo back into the storage: the storage was above
//      haulPct again, so the next spawnHauls() pass (1 s later) sent a new party immediately - forever. A party that
//      got stuck or could not reach the storage was likewise destroyed and re-spawned at once (no cooldown).
//   3. Every party removed on arrival/give-up vanished from S_State, which the client drew as a "destroyed entity"
//      ghost dissolving into ash at the hall/storage - the loop looked like convoys repeatedly burning down.
//   4. giveUp() into an inactive (cap 0) or full storage silently destroyed the cargo.
// Fix: a party only leaves when the hall has room for it, loads at most that room, every ended haul puts the storage on
// a cooldown (long after a failure), undeliverable cargo goes back to the owner, and the client no longer ghosts
// arriving convoys (see PacketHandler::onState). Fire damage is only credited to an arsonist that is still hostile
// (FireSystem).
float ConvoyHaulSystem::hallRoom(int owner)
{
    auto th = world.townHallOf(owner);
    return th == entt::null ? 0.f : std::max(0.f, world.capOf(th) - world.stockOf(th));
}

void ConvoyHaulSystem::endHaul(entt::entity e, Haul &h, bool ok)
{
    int ow = reg.get<Owner>(e).id;
    auto st = world.find(h.storage);
    if (st != entt::null)
        world.storeCargo(st, h.cargo); // what the hall could not take goes back where it came from
    if (auto th = world.townHallOf(ow); th != entt::null)
        world.storeCargo(th, h.cargo);

    float left = 0;
    for (float c : h.cargo)
        left += c;
    if (left > .5f && isPlayer(ow) && world.pl[ow].used)
        for (int r = 0; r < R_Count; r++)
            world.pl[ow].res[r] += h.cargo[r]; // never destroy cargo
    for (float &c : h.cargo)
        c = 0;

    haulCool[h.storage] = world.clock + (ok ? 4.0 : 90.0);

    Pos p = reg.get<Pos>(e);
    world.fx(E_Dust, p.x, p.y);
    world.destroyNow(e);
}

// The hall cannot take the load: nobody is sent, and the owner is told at most every 5 minutes.
void ConvoyHaulSystem::noteHallFull(int ow, uint32_t storageUid)
{
    if (world.clock > fullNote[ow])
    {
        fullNote[ow] = world.clock + 300.0;
        world.say(ow, "Your Town Hall storage is full: hauling parties wait until there is room again.");
    }
    haulCool[storageUid] = world.clock + 20.0;
}

void ConvoyHaulSystem::spawnHauls()
{
    std::unordered_set<uint32_t> busy;
    reg.view<Haul>().each([&](const Haul &h) { busy.insert(h.storage); });
    std::erase_if(haulCool, [&](auto &kv) { return kv.second < world.clock; });

    std::vector<entt::entity> blds;
    reg.view<Info, Building, Owner>().each([&](entt::entity e, const Info &i, const Building &b, const Owner &o)
    {
        if (hasTag(i.kind, KT_Depot) && b.progress >= 1.f && isPlayer(o.id) && !world.inactive(e) &&
            !reg.all_of<Moving>(e))
            blds.push_back(e);
    });

    for (auto e : blds)
    {
        uint32_t uid = reg.get<Uid>(e).id;
        if (busy.count(uid) || haulCool.count(uid))
            continue; // one party per storage, and a pause after each trip

        auto &b = reg.get<Building>(e);
        int ow = reg.get<Owner>(e).id;
        float stock = world.stockOf(e), cap = world.capOf(e);
        if (cap <= 0.f || stock < 20.f || stock / cap * 100.f < float(b.haulPct))
            continue;

        auto th = world.townHallOf(ow);
        if (th == entt::null)
            continue;

        if (hallRoom(ow) < std::min(stock, 50.f))
        {
            noteHallFull(ow, uid);
            continue;
        }

        Pos at;
        if (!world.spawnSpot(th, false, at))
            continue;

        auto u = world.makeUnit(K_Pawn, uint8_t(ow), at.x, at.y);
        Haul h;
        h.storage = uid;
        reg.emplace<Haul>(u, h);

        busy.insert(uid);
        world.say(ow, std::string("A hauling party leaves the Town Hall for your ") +
                          KINFO[reg.get<Info>(e).kind].name + ".");
    }
}

void ConvoyHaulSystem::runHaul(entt::entity e, Unit &u, Haul &h, float dt)
{
    if (auto *bd = reg.try_get<Boarding>(e))
    {
        s.naval.runBoarding(e, u, *bd, dt); // walking to a ferry
        return;
    }

    Pos p = reg.get<Pos>(e);
    int ow = reg.get<Owner>(e).id;
    auto st = world.find(h.storage);
    auto th = world.townHallOf(ow);
    if (th == entt::null)
    {
        endHaul(e, h, false);
        return;
    }

    entt::entity target = h.phase == 0 ? st : th;
    if (target == entt::null)
    {
        endHaul(e, h, false);
        return;
    }

    float d = world.rectDist(p, target);
    if (d <= 1.6f)
        arriveHaul(e, u, h, st, th, ow);
    else
        walkHaul(e, u, h, target, d, dt);
}

void ConvoyHaulSystem::arriveHaul(entt::entity e, Unit &u, Haul &h, entt::entity st, entt::entity th, int ow)
{
    u.path.clear();
    h.stuck = 0;
    h.bestDist = 1e9f;

    if (h.phase != 0)
    {
        world.storeCargo(th, h.cargo);
        endHaul(e, h, true);
        return;
    }

    auto &sb = reg.get<Building>(st);
    float tot = 0;
    for (float v : sb.stock)
        tot += v;

    float load = std::min({HAULER_CARRY, hallRoom(ow), tot}); // never carry more than the hall can take
    if (load < 1.f)
    {
        endHaul(e, h, false);
        return;
    }

    float f = load / tot;
    for (int r = 0; r < R_Count; r++)
    {
        h.cargo[r] = sb.stock[r] * f;
        sb.stock[r] -= h.cargo[r];
    }
    h.phase = 1;
}

void ConvoyHaulSystem::walkHaul(entt::entity e, Unit &u, Haul &h, entt::entity target, float d, float dt)
{
    h.repath -= dt;
    if (!u.path.empty() || h.repath > 0)
        return;
    h.repath = 3.f;

    // long trip: would one of our ships be clearly faster?
    if (h.boatPhase != h.phase)
    {
        int ferry = s.ferry.tryFerryHaul(e, h, target);
        if (ferry == 2)
        {
            h.repath = .6f;
            return;
        }
        if (ferry == 1)
            return;
    }

    if (d < h.bestDist - 2.f)
    {
        h.bestDist = d;
        h.stuck = 0;
    }
    else if (++h.stuck >= 8)
    {
        endHaul(e, h, false);
        return;
    }

    if (!world.order(e, world.posOf(target), reg.get<Uid>(target).id, 40000))
        h.stuck += 2;
}

bool ConvoyHaulSystem::spottedForClan(int id, uint32_t uid) const
{
    if (!isPlayer(id) || !world.pl[id].clan)
        return false;
    auto it = clanSpot.find(world.pl[id].clan);
    if (it == clanSpot.end())
        return false;
    auto f = it->second.find(uid);
    return f != it->second.end() && f->second.until >= world.clock;
}

bool ConvoyHaulSystem::workerToward(entt::entity e, entt::entity b)
{
    return world.order(e, world.posOf(b), reg.get<Uid>(b).id);
}

entt::entity ConvoyHaulSystem::nearestStorageWithRoom(Pos p, int owner)
{
    entt::entity best = entt::null;
    float bd = 1e9f;

    reg.view<Info, Building, Owner>().each([&](entt::entity st, const Info &i, const Building &sb, const Owner &so)
    {
        if (!isStorage(i.kind) || sb.progress < 1.f || so.id != owner || world.capOf(st) - world.stockOf(st) < 1.f)
            return;

        Pos c = footCenter(i.kind, sb.tx, sb.ty);
        float d = std::hypot(p.x - c.x, p.y - c.y);
        if (d < bd)
        {
            bd = d;
            best = st;
        }
    });

    return best;
}

void ConvoyHaulSystem::runWorker(entt::entity e, Unit &u, Worker &w, float dt)
{
    Pos p = reg.get<Pos>(e);
    auto home = world.find(w.home);
    if (home == entt::null)
    {
        reg.remove<Worker>(e);
        return;
    }

    switch (w.phase)
    {
    case 0:
        workerCollect(e, u, w, home, p);
        break;
    case 1:
        u.state = US_Work;
        u.path.clear();
        w.timer -= dt;
        if (w.timer <= 0)
            w.phase = 2;
        break;
    case 2:
        workerDeliver(e, u, w, p);
        break;
    default:
        if (world.rectDist(p, home) <= 1.6f)
        {
            w.phase = 0;
            u.path.clear();
        }
        else if (u.path.empty())
            workerToward(e, home);
        break;
    }
}

void ConvoyHaulSystem::workerCollect(entt::entity e, Unit &u, Worker &w, entt::entity home, Pos p)
{
    auto &hb = reg.get<Building>(home);
    int ow = reg.get<Owner>(e).id;
    if (hb.progress < 1.f)
        return;

    if (world.rectDist(p, home) > 1.6f)
    {
        if (u.path.empty())
            workerToward(e, home);
        return;
    }

    float tot = 0;
    for (float v : hb.stock)
        tot += v;

    if (tot >= 8.f && nearestStorageWithRoom(p, ow) != entt::null)
    {
        w.dest = 0;
        float f = std::min(1.f, 30.f / tot);
        for (int r = 0; r < R_Count; r++)
        {
            w.cargo[r] = hb.stock[r] * f;
            hb.stock[r] -= w.cargo[r];
        }
        w.phase = 1;
        w.timer = 1.f;
    }
}

void ConvoyHaulSystem::workerDeliver(entt::entity e, Unit &u, Worker &w, Pos p)
{
    int ow = reg.get<Owner>(e).id;
    auto st = world.find(w.dest);
    if (st == entt::null || !world.complete(st) || world.capOf(st) - world.stockOf(st) < 1.f)
    {
        st = nearestStorageWithRoom(p, ow);
        w.dest = st == entt::null ? 0 : reg.get<Uid>(st).id;
        u.path.clear();
    }
    if (st == entt::null)
        return; // every storage is full: wait with the cargo

    if (world.rectDist(p, st) <= 1.5f)
    {
        world.storeCargo(st, w.cargo);
        float left = 0;
        for (float c : w.cargo)
            left += c;

        if (left < .5f)
        {
            for (float &c : w.cargo)
                c = 0;
            w.phase = 3;
            w.dest = 0;
        }
        else
            w.dest = 0; // partly full: try the next nearest storage
        u.path.clear();
        return;
    }

    if (u.path.empty() && !workerToward(e, st))
        w.timer = 0;
}
