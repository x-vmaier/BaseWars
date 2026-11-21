/**
 * @file FerryPlanner.cpp
 * @brief Ferry planner: decides whether one of the owner's idle ships beats walking, and starts the transport.
 */
#include "systems/FerryPlanner.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <spdlog/spdlog.h>
#include <string>

void FerryPlanner::shoreTiles(Pos around, int maxR, size_t want, std::vector<IV> &out)
{
    out.clear();

    IV c = tileOf(around);
    int firstR = -1;
    for (int r = 0; r <= maxR; r++)
    {
        if (out.size() >= want || (firstR >= 0 && r > firstR + 6))
            break;
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
            {
                if (std::max(std::abs(dx), std::abs(dy)) != r)
                    continue;
                int x = c.x + dx, y = c.y + dy;
                if (!world.walkable(x, y, false, 0))
                    continue;

                if (world.biome(x + 1, y) != B_Ocean && world.biome(x - 1, y) != B_Ocean &&
                    world.biome(x, y + 1) != B_Ocean && world.biome(x, y - 1) != B_Ocean)
                    continue;

                bool apart = true;
                for (auto &o : out)
                    if (std::abs(o.x - x) < 5 && std::abs(o.y - y) < 5)
                        apart = false;
                if (!apart || out.size() >= want)
                    continue;

                out.push_back({x, y});
                if (firstR < 0)
                    firstR = r;
            }
    }
}

IV FerryPlanner::waterNextTo(IV from)
{
    IV best{from.x, from.y};
    int bs = -1;

    static const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto &d : d4)
    {
        int x = from.x + d[0], y = from.y + d[1];
        if (world.biome(x, y) != B_Ocean)
            continue;

        int sc = 0;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (world.biome(x + dx, y + dy) == B_Ocean)
                    sc++;
        if (sc > bs)
        {
            bs = sc;
            best = {x, y};
        }
    }

    return bs < 0 ? IV{INT_MIN, INT_MIN} : best;
}

static float pathLen(Pos start, const std::vector<Pos> &p)
{
    float l = 0;
    Pos c = start;

    for (auto &q : p)
    {
        l += std::hypot(q.x - c.x, q.y - c.y);
        c = q;
    }

    return l;
}

// Route length in tiles from `a` to `b`; INF when there is no complete route.
// Results are cached for 45 s per (start tile, goal tile, medium, ignored building): the planner re-asks the same
// questions for every boarding / landing candidate and for every group sent over the same stretch of coast.
float FerryPlanner::routeLen(Pos a, IV b, bool water, uint32_t ignore, int maxNodes, float tol)
{
    std::vector<Pos> p;
    IV at = tileOf(a);
    if (at.x == b.x && at.y == b.y)
        return 0.f;

    RouteKey key{at.x, at.y, b.x, b.y, water, ignore};
    auto hit = routeCache.find(key);
    if (hit != routeCache.end() && hit->second.second > world.clock)
        return hit->second.first;

    float len = INF_T;
    if (world.findPath(at, b, water, ignore, p, maxNodes))
    {
        Pos last = p.back(), g = tileCenter(b);
        if (std::hypot(last.x - g.x, last.y - g.y) <= tol)
            len = pathLen(a, p);
    }

    if (routeCache.size() > 4096)
        routeCache.clear();
    routeCache[key] = {len, world.clock + 45.0};
    return len;
}

FerryPlanner::GroupInfo FerryPlanner::summarizeGroup(int owner, const std::vector<entt::entity> &group)
{
    GroupInfo gi;
    gi.speed = 1e9f;

    for (auto e : group)
    {
        if (!reg.valid(e) || !reg.all_of<Unit, Pos, Info>(e) || reg.all_of<Aboard>(e))
            continue;

        Kind k = reg.get<Info>(e).kind;
        if (!isTroop(k))
            return GroupInfo{}; // n == 0: the caller treats the group as unplannable

        Pos p = reg.get<Pos>(e);
        gi.from.x += p.x;
        gi.from.y += p.y;
        gi.n++;
        gi.speed = std::min(gi.speed, KINFO[k].speed * unitSpeedMul(world.ulv(owner, k)));
        gi.cargo += s.naval.cargoTotal(e);
    }

    if (gi.n)
    {
        gi.from.x /= float(gi.n);
        gi.from.y /= float(gi.n);
    }
    return gi;
}

std::vector<FerryPlanner::ShipCand> FerryPlanner::idleShips(int owner, const GroupInfo &gi)
{
    std::vector<ShipCand> ships;

    reg.view<Ship, Owner, Info, Pos, Unit>().each(
        [&](entt::entity e, const Ship &, const Owner &o, const Info &in, const Pos &p, const Unit &u)
    {
        if (o.id != owner || !hasTag(in.kind, KT_Naval) || reg.all_of<Voyage>(e) || !u.path.empty() ||
            world.clock - reg.get<Ship>(e).idleSince < FERRY_IDLE || world.dying.count(reg.get<Uid>(e).id))
            return;

        int tf;
        float rf;
        s.naval.shipFree(e, tf, rf);
        if (reg.get<Ship>(e).pax.size() || tf < gi.n || rf + 1e-3f < gi.cargo || tf < shipTroopCap(in.kind, in.level) ||
            reg.all_of<ShipUpgrade>(e))
            return; // fully empty, unreserved ships only
        ships.push_back({e, p, KINFO[in.kind].speed});
    });

    return ships;
}

// Landing: shore near the destination reachable from the boarding water.
void FerryPlanner::bestLanding(const GroupInfo &gi, const ShipCand &sc, IV bs, IV bw, float gather,
                               const std::vector<IV> &lCands, IV toT, uint32_t destUid, float &bestBoat,
                               TransportPlan &plan)
{
    for (auto &ls : lCands)
    {
        IV lw = waterNextTo(ls);
        if (lw.x == INT_MIN || (ls.x == bs.x && ls.y == bs.y))
            continue;

        float sail = routeLen(tileCenter(bw), lw, true, 0, 25000);
        if (sail >= INF_T * .5f)
            continue;

        float walk2 = routeLen(tileCenter(ls), toT, false, destUid, 5000, 4.2f);
        if (walk2 >= INF_T * .5f)
            continue;

        float eta = gather + sail / sc.shipSpeed + 2.f + walk2 / gi.speed;
        if (eta < bestBoat)
        {
            bestBoat = eta;
            plan.ship = sc.e;
            plan.shipUid = reg.get<Uid>(sc.e).id;
            plan.board = bs;
            plan.boardWater = bw;
            plan.land = ls;
            plan.landWater = lw;
        }
    }
}

// Boarding: nearest shore the ship can sail to. Returns the best ETA and fills the plan's route.
float FerryPlanner::bestBoatRoute(const GroupInfo &gi, const std::vector<ShipCand> &ships,
                                  const std::vector<IV> &bCands, const std::vector<IV> &lCands, IV toT,
                                  uint32_t destUid, TransportPlan &plan)
{
    float bestBoat = INF_T;

    for (auto &bs : bCands)
    {
        IV bw = waterNextTo(bs);
        if (bw.x == INT_MIN)
            continue;

        float walk1 = routeLen(gi.from, bs, false, 0, 5000);
        if (walk1 >= INF_T * .5f)
            continue;

        for (auto &sc : ships)
        {
            float sail0 = routeLen(sc.p, bw, true, 0, 15000);
            if (sail0 >= INF_T * .5f)
                continue;

            float gather = std::max(walk1 / gi.speed, sail0 / sc.shipSpeed) + 2.f;
            bestLanding(gi, sc, bs, bw, gather, lCands, toT, destUid, bestBoat, plan);
            break; // the nearest ship that can reach this shore is enough
        }
    }

    return bestBoat;
}

TransportPlan FerryPlanner::planTransport(int owner, const std::vector<entt::entity> &group, Pos to, uint32_t destUid,
                                          float boatFactor)
{
    TransportPlan plan;
    if (group.empty() || !isPlayer(owner))
        return plan;

    GroupInfo gi = summarizeGroup(owner, group);
    if (!gi.n)
        return plan;
    if (std::hypot(gi.from.x - to.x, gi.from.y - to.y) < FERRY_MIN_DIST)
        return plan;

    std::vector<ShipCand> ships = idleShips(owner, gi);
    if (ships.empty())
        return plan;

    if (planBudget <= 0)
    {
        plan.deferred = true;
        return plan;
    }
    planBudget--;

    IV toT = tileOf(to);
    float landLen = routeLen(gi.from, toT, false, destUid, 12000, 4.2f);
    plan.etaLand = landLen >= INF_T * .5f ? INF_T : landLen / gi.speed;
    if (plan.etaLand < INF_T * .5f && plan.etaLand < 6.f)
        return plan;

    std::vector<IV> bCands, lCands;
    shoreTiles(gi.from, 40, 3, bCands);
    shoreTiles(to, 40, 3, lCands);
    if (bCands.empty() || lCands.empty())
        return plan;

    const Pos from = gi.from;
    std::sort(ships.begin(), ships.end(), [&](const ShipCand &a, const ShipCand &b)
    { return std::hypot(a.p.x - from.x, a.p.y - from.y) < std::hypot(b.p.x - from.x, b.p.y - from.y); });
    if (ships.size() > 2)
        ships.resize(2);

    plan.etaBoat = bestBoatRoute(gi, ships, bCands, lCands, toT, destUid, plan);
    plan.useBoat = plan.etaBoat < INF_T * .5f && plan.etaBoat < boatFactor * plan.etaLand;

    static const bool dbg = std::getenv("BW_SHIPDEBUG") != nullptr;
    if (dbg)
        spdlog::info(
            "[ferry] plan owner {} group {} from {:.0f},{:.0f} to {:.0f},{:.0f}: land {:.1f}s boat {:.1f}s -> {}",
            owner, gi.n, from.x, from.y, to.x, to.y, plan.etaLand, plan.etaBoat, plan.useBoat ? "BOAT" : "walk");

    return plan;
}

bool FerryPlanner::startTransport(const TransportPlan &plan, const std::vector<entt::entity> &group, Pos dest,
                                  uint32_t destUid)
{
    if (!plan.useBoat || plan.ship == entt::null || !reg.valid(plan.ship) || !reg.all_of<Ship>(plan.ship) ||
        reg.all_of<Voyage>(plan.ship))
        return false;
    if (!reg.get<Ship>(plan.ship).pax.empty())
        return false;

    Voyage v;
    v.board = plan.board;
    v.boardWater = plan.boardWater;
    v.land = plan.land;
    v.landWater = plan.landWater;
    v.dest = dest;
    v.destUid = destUid;
    v.start = world.clock;

    if (!world.order(plan.ship, tileCenter(plan.boardWater)))
        return false;

    Pos meet = tileCenter(plan.board);
    for (auto e : group)
    {
        if (!reg.valid(e) || !reg.all_of<Unit>(e) || reg.all_of<Aboard>(e))
            continue;
        reg.remove<Attack>(e);
        reg.remove<AMove>(e);
        if (!world.order(e, meet))
            continue;

        Boarding b;
        b.ship = plan.shipUid;
        b.meetSet = true;
        b.meet = meet;
        reg.emplace_or_replace<Boarding>(e, b);
        v.units.push_back(reg.get<Uid>(e).id);
    }

    if (v.units.empty())
    {
        reg.get<Unit>(plan.ship).path.clear();
        return false;
    }

    reg.emplace<Voyage>(plan.ship, v);
    return true;
}

// Haul pawns: 0 = walk, 1 = a voyage was started, 2 = planner budget exhausted (retry soon)
int FerryPlanner::tryFerryHaul(entt::entity e, Haul &h, entt::entity target)
{
    int ow = reg.get<Owner>(e).id;
    std::vector<entt::entity> g{e};
    Pos to = world.posOf(target);
    uint32_t tuid = reg.get<Uid>(target).id;

    TransportPlan plan = planTransport(ow, g, to, tuid);
    if (plan.deferred)
        return 2;

    h.boatPhase = h.phase;
    if (!plan.useBoat)
        return 0;
    if (!startTransport(plan, g, to, tuid))
        return 0;

    world.say(ow, "A hauling party boards a ship.");
    return 1;
}
