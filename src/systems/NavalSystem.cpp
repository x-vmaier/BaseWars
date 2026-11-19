#include "systems/NavalSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

float NavalSystem::cargoTotal(entt::entity e) const
{
    const Haul *h = reg.try_get<Haul>(e);
    if (!h)
        return 0.f;
    float t = 0;
    for (float c : h->cargo)
        t += c;
    return t;
}
// Passengers actually aboard (stale uids are purged).
void NavalSystem::paxStats(entt::entity ship, int &troops, float &res, int *kinds)
{
    auto &sh = reg.get<Ship>(ship);
    uint32_t suid = reg.get<Uid>(ship).id;
    std::erase_if(sh.pax, [&](uint32_t uid)
                  {
        auto e = world.find(uid);
        return e == entt::null || !reg.all_of<Aboard>(e) || reg.get<Aboard>(e).ship != suid; });
    troops = 0;
    res = 0;
    if (kinds)
        for (int i = 0; i < 5; i++)
            kinds[i] = 0;
    for (uint32_t uid : sh.pax)
    {
        auto e = world.find(uid);
        troops++;
        res += cargoTotal(e);
        Kind k = reg.get<Info>(e).kind;
        if (kinds && k >= K_Pawn && k <= K_Monk)
            kinds[k - K_Pawn]++;
    }
}
// Free capacity, counting units that are already walking to embark.
void NavalSystem::shipFree(entt::entity ship, int &troopsFree, float &resFree, bool reserved)
{
    int t;
    float r;
    paxStats(ship, t, r);
    int lvl = reg.get<Info>(ship).level;
    troopsFree = shipTroopCap(lvl) - t;
    resFree = float(shipResCap(lvl)) - r;
    if (!reserved)
        return;
    uint32_t suid = reg.get<Uid>(ship).id;
    reg.view<Boarding>().each([&](entt::entity e, const Boarding &b)
                              {
        if (b.ship != suid) return;
        troopsFree--;
        resFree -= cargoTotal(e); });
}
void NavalSystem::initShip(entt::entity ship, int harbourLevel)
{ // called when a Harbour builds a ship: empty boat, tier = harbour level
    auto &in = reg.get<Info>(ship);
    in.level = uint8_t(shipTier(harbourLevel));
    auto &h = reg.get<Health>(ship);
    h.hp = h.maxhp = KINFO[K_Ship].hp * shipHpMul(in.level);
    if (!reg.all_of<Ship>(ship))
        reg.emplace<Ship>(ship, Ship{{}, world.clock});
}

void NavalSystem::embark(entt::entity e, entt::entity ship)
{
    auto &u = reg.get<Unit>(e);
    reg.remove<Attack>(e);
    reg.remove<AMove>(e);
    reg.remove<Boarding>(e);
    u.path.clear();
    u.i = 0;
    u.state = US_Idle;
    reg.emplace_or_replace<Aboard>(e, Aboard{reg.get<Uid>(ship).id});
    reg.get<Ship>(ship).pax.push_back(reg.get<Uid>(e).id);
    reg.get<Pos>(e) = reg.get<Pos>(ship);
}
// Free land tiles around `center` (ring search, nearest first).
std::vector<IV> NavalSystem::freeLandTiles(Pos center, int rmax)
{
    std::vector<std::pair<float, IV>> c;
    IV t = tileOf(center);
    for (int dy = -rmax; dy <= rmax; dy++)
        for (int dx = -rmax; dx <= rmax; dx++)
        {
            int x = t.x + dx, y = t.y + dy;
            if (!world.walkable(x, y, false, 0))
                continue;
            c.push_back({std::hypot(float(x) + .5f - center.x, float(y) + .5f - center.y), IV{x, y}});
        }
    std::sort(c.begin(), c.end(), [](auto &a, auto &b)
              { return a.first < b.first; });
    std::vector<IV> out;
    for (auto &p : c)
        out.push_back(p.second);
    return out;
}
// Put units that are aboard `ship` onto land tiles near `center`; returns the units that were placed.
std::vector<entt::entity> NavalSystem::landUnits(entt::entity ship, const std::vector<entt::entity> &us, Pos center, int rmax)
{
    std::vector<entt::entity> placed;
    auto tiles = freeLandTiles(center, rmax);
    if (tiles.empty())
        return placed;
    auto &sh = reg.get<Ship>(ship);
    size_t n = 0;
    for (auto e : us)
    {
        if (!reg.valid(e) || !reg.all_of<Aboard>(e))
            continue;
        IV t = tiles[n % tiles.size()];
        float j = float(n / tiles.size()) * .3f; // more units than tiles: spread inside the tile
        Pos p{float(t.x) + .5f + j - .15f, float(t.y) + .5f + (n % 2 ? j : -j) * .5f};
        reg.get<Pos>(e) = p;
        reg.remove<Aboard>(e);
        auto &u = reg.get<Unit>(e);
        u.path.clear();
        u.i = 0;
        uint32_t uid = reg.get<Uid>(e).id;
        std::erase(sh.pax, uid);
        placed.push_back(e);
        n++;
    }
    return placed;
}
std::vector<entt::entity> NavalSystem::passengers(entt::entity ship)
{
    std::vector<entt::entity> v;
    for (uint32_t uid : reg.get<Ship>(ship).pax)
    {
        auto e = world.find(uid);
        if (e != entt::null && reg.all_of<Aboard>(e))
            v.push_back(e);
    }
    return v;
}
void NavalSystem::cancelVoyage(entt::entity ship)
{
    auto *v = reg.try_get<Voyage>(ship);
    if (!v)
        return;
    for (uint32_t uid : v->units)
    {
        auto e = world.find(uid);
        if (e == entt::null)
            continue;
        if (auto *b = reg.try_get<Boarding>(e))
            if (b->meetSet)
            {
                reg.remove<Boarding>(e);
                reg.get<Unit>(e).path.clear();
            }
    }
    reg.remove<Voyage>(ship);
}
void NavalSystem::runBoarding(entt::entity e, Unit &u, Boarding &bd, float dt)
{
    auto ship = world.find(bd.ship);
    int ow = reg.get<Owner>(e).id;
    if (ship == entt::null || !reg.all_of<Ship>(ship) || reg.get<Owner>(ship).id != ow || (bd.meetSet && !reg.all_of<Voyage>(ship)))
    {
        reg.remove<Boarding>(e);
        u.path.clear();
        return;
    }
    Pos p = reg.get<Pos>(e), sp = reg.get<Pos>(ship);
    if (std::hypot(p.x - sp.x, p.y - sp.y) <= BOARD_REACH)
    {
        int tf;
        float rf;
        shipFree(ship, tf, rf, false);
        float need = cargoTotal(e);
        if (tf >= 1 && rf + 1e-3f >= need)
            embark(e, ship);
        else
        {
            world.say(ow, "The ship is full.");
            reg.remove<Boarding>(e);
            u.path.clear();
        }
        return;
    }
    bd.repath -= dt;
    if (bd.repath > 0)
        return;
    bd.repath = 1.5f + float(reg.get<Uid>(e).id % 10) * .1f;
    if (bd.meetSet)
    {
        if (u.path.empty() && !world.order(e, bd.meet) && ++bd.stuck > 4)
        {
            reg.remove<Boarding>(e);
            u.path.clear();
        }
        return;
    }
    if (world.order(e, sp))
        bd.stuck = 0;
    else if (++bd.stuck >= 4)
    {
        world.say(ow, "Cannot reach the ship: move it next to the coast.");
        reg.remove<Boarding>(e);
        u.path.clear();
    }
}

void NavalSystem::tickVoyage(entt::entity ship, Voyage &v)
{
    auto &su = reg.get<Unit>(ship);
    Pos sp = reg.get<Pos>(ship);
    std::erase_if(v.units, [&](uint32_t uid)
                  { auto e = world.find(uid); return e == entt::null || (!reg.all_of<Aboard>(e) && !reg.all_of<Boarding>(e)); });
    if (v.phase == 0)
    {
        int aboard = 0, waiting = 0;
        for (uint32_t uid : v.units)
            (reg.all_of<Aboard>(world.find(uid)) ? aboard : waiting)++;
        Pos wc = tileCenter(v.boardWater);
        bool there = std::hypot(sp.x - wc.x, sp.y - wc.y) < 2.2f;
        if (!there && su.path.empty() && world.clock - v.lastOrder > 3.0)
        {
            v.lastOrder = world.clock;
            if (!world.order(ship, wc) && ++v.fails > 3)
            {
                cancelVoyage(ship);
                return;
            }
        }
        if (there && v.arrivedAt < 0)
            v.arrivedAt = world.clock;
        bool timeout = (v.arrivedAt >= 0 && world.clock - v.arrivedAt > 25.0) || world.clock - v.start > 150.0;
        if (waiting == 0 || timeout)
        {
            for (uint32_t uid : v.units)
            {
                auto e = world.find(uid);
                if (reg.all_of<Aboard>(e))
                    continue;
                reg.remove<Boarding>(e);
                reg.get<Unit>(e).path.clear();
            }
            std::erase_if(v.units, [&](uint32_t uid)
                          { return !reg.all_of<Aboard>(world.find(uid)); });
            if (v.units.empty())
            {
                reg.remove<Voyage>(ship);
                return;
            }
            v.phase = 1;
            v.lastOrder = world.clock;
            if (!world.order(ship, tileCenter(v.landWater)))
                v.phase = 2;
        }
    }
    else if (v.phase == 1)
    {
        if (su.path.empty())
        {
            Pos lc = tileCenter(v.landWater);
            if (std::hypot(sp.x - lc.x, sp.y - lc.y) < 3.f || v.retried)
                v.phase = 2;
            else
            {
                v.retried = true;
                if (!world.order(ship, lc))
                    v.phase = 2;
            }
        }
        if (world.clock - v.start > 400.0)
            v.phase = 2;
    }
    if (v.phase == 2)
    {
        auto us = passengers(ship);
        auto placed = landUnits(ship, us, tileCenter(v.land), 4);
        if (placed.empty() && !us.empty())
            placed = landUnits(ship, us, sp, 3);
        for (auto e : placed)
            if (!reg.all_of<Haul>(e))
                world.order(e, v.dest, v.destUid);
        reg.get<Ship>(ship).idleSince = world.clock - FERRY_IDLE; // our own ferry: immediately available for the next run
        reg.remove<Voyage>(ship);
    }
}
void NavalSystem::shipsTick(float dt)
{
    s.ferry.planBudget = 1;
    std::vector<entt::entity> ships;
    reg.view<Info, Unit, Pos, Owner>().each([&](entt::entity e, const Info &in, const Unit &, const Pos &, const Owner &)
                                            { if (in.kind == K_Ship) ships.push_back(e); });
    for (auto e : ships)
    {
        if (!reg.valid(e) || world.dying.count(reg.get<Uid>(e).id))
            continue;
        if (!reg.all_of<Ship>(e))
            reg.emplace<Ship>(e, Ship{{}, world.clock});
        auto &sh = reg.get<Ship>(e);
        auto &su = reg.get<Unit>(e);
        Pos sp = reg.get<Pos>(e);
        if (!su.path.empty() || reg.all_of<Voyage>(e))
            sh.idleSince = world.clock;
        for (uint32_t uid : sh.pax)
        {
            auto q = world.find(uid);
            if (q != entt::null && reg.all_of<Aboard>(q))
                reg.get<Pos>(q) = sp;
        }
        if (auto *v = reg.try_get<Voyage>(e))
        {
            tickVoyage(e, *v);
            continue;
        }
        if (sh.pax.empty() || !su.path.empty() || world.clock - sh.idleSince < 6.0)
            continue;
        // orphans: hauling pawns aboard a ship that no longer serves a voyage must get off
        std::vector<entt::entity> orphans;
        for (auto q : passengers(e))
            if (reg.all_of<Haul>(q))
                orphans.push_back(q);
        if (orphans.empty())
            continue;
        if (!landUnits(e, orphans, sp, 2).empty())
            continue;
        std::vector<IV> sh2;
        s.ferry.shoreTiles(sp, 30, 1, sh2);
        if (!sh2.empty())
        {
            IV w = s.ferry.waterNextTo(sh2[0]);
            if (w.x != INT_MIN)
                world.order(e, tileCenter(w));
        }
        sh.idleSince = world.clock - 4.0;
    }
    (void)dt;
}

// commands
bool NavalSystem::shipPacket(int id, uint8_t type, Reader &r)
{
    if (type == C_Board)
    {
        int n = r.get<uint8_t>();
        std::vector<uint32_t> ids;
        for (int i = 0; i < n; i++)
            ids.push_back(r.get<uint32_t>());
        auto ship = world.find(r.get<uint32_t>());
        if (!r.ok || ship == entt::null || !reg.all_of<Ship>(ship) || reg.get<Owner>(ship).id != id)
            return true;
        if (reg.all_of<Voyage>(ship))
        {
            world.say(id, "That ship is busy on a supply run.");
            return true;
        }
        int tf;
        float rf;
        shipFree(ship, tf, rf);
        int lvl = reg.get<Info>(ship).level, taken = 0, over = 0;
        Pos sp = reg.get<Pos>(ship);
        for (uint32_t uid : ids)
        {
            auto e = world.find(uid);
            if (e == entt::null || !reg.all_of<Unit, Owner, Info, Pos>(e) || reg.get<Owner>(e).id != id || !isTroop(reg.get<Info>(e).kind))
                continue;
            if (reg.all_of<Worker>(e) || reg.all_of<Convoy>(e) || reg.all_of<Haul>(e) || reg.all_of<Raider>(e) || reg.all_of<Aboard>(e))
                continue;
            if (taken >= tf)
            {
                over++;
                continue;
            }
            world.releaseUnit(e); // [base] ends patrols / firefighting
            if (reg.get<Info>(e).kind == K_Pawn)
                s.hunt.huntPause(e); // [world] a commanded pawn stops hunting for a while
            reg.remove<Attack>(e);
            reg.remove<AMove>(e);
            Boarding b;
            b.ship = reg.get<Uid>(ship).id;
            reg.emplace_or_replace<Boarding>(e, b);
            auto &u = reg.get<Unit>(e);
            u.path.clear();
            if (std::hypot(reg.get<Pos>(e).x - sp.x, reg.get<Pos>(e).y - sp.y) > BOARD_REACH && !world.order(e, sp))
            {
                reg.remove<Boarding>(e);
                world.say(id, "Cannot reach the ship: move it next to the coast.");
                continue;
            }
            taken++;
        }
        if (over)
            world.say(id, std::string("The ") + SHIP_NAME[shipTier(lvl) - 1] + " only carries " + std::to_string(shipTroopCap(lvl)) + " troops: " + std::to_string(over) + " could not board.");
        return true;
    }
    if (type == C_Disembark)
    {
        auto ship = world.find(r.get<uint32_t>());
        if (!r.ok || ship == entt::null || !reg.all_of<Ship>(ship) || reg.get<Owner>(ship).id != id)
            return true;
        cancelVoyage(ship);
        auto us = passengers(ship);
        if (us.empty())
            return true;
        auto placed = landUnits(ship, us, reg.get<Pos>(ship), 2);
        if (placed.empty())
            world.say(id, "Move the ship next to a coastline to disembark.");
        else
            world.say(id, std::to_string(placed.size()) + " unit(s) disembarked.");
        return true;
    }
    return false;
}

// outgoing
// Called from sendState for every record: false hides it. Fills the ship/boarded fields.
bool NavalSystem::shipStateHook(int viewer, entt::entity e, int ownerId, const Info &in, uint8_t &aux, uint8_t &prog, uint16_t &stock)
{
    if (reg.all_of<Aboard>(e))
    {
        if (!world.allied(viewer, ownerId))
            return false;
        prog = UNIT_ABOARD_PROG;
        return true;
    }
    if (in.kind == K_Ship && reg.all_of<Ship>(e))
    {
        int t;
        float rs;
        paxStats(e, t, rs);
        aux = uint8_t(shipTier(in.level));
        prog = uint8_t(std::min(t, 254));
        stock = uint16_t(std::min(rs, 65000.f));
        if (!world.allied(viewer, ownerId))
        {
            prog = 0;
            stock = 0;
        } // enemies only see the boat itself, not its load
    }
    return true;
}
