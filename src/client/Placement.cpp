/**
 * @file Placement.cpp
 * @brief Building placement: modes, checks, costs and the feature-clearing step before building.
 */
#include "client/Placement.hpp"

#include "client/App.hpp"

#include <cmath>

namespace
{
// Adapter for the shared checkPlace(): what the client knows about the world.
struct PlaceWorld
{
    const ClientWorld &a;
    int biome(int x, int y) { return a.biomeAt(x, y); }
    bool explored(int x, int y) { return a.exploredAt(x, y); }
    bool occupied(int x, int y) { return a.occ.count(chunkKey(x, y)) > 0; }
    template <class F> void anchors(F f)
    {
        a.reg.view<Info, Goal>().each([&](const Info &i, const Goal &g)
        {
            if (hasTag(i.kind, KT_Anchor) && !(i.state & BLD_DISABLED))
                f(Pos{g.x, g.y}, i.kind, i.owner == a.me, i.owner != a.me && a.alliedWith(i.owner));
        });
    }
    bool campfireNear(Pos c, float r)
    {
        bool hit = false;
        a.reg.view<Info, Goal>().each([&](const Info &i, const Goal &g)
        {
            if (hasTag(i.kind, KT_Warm) && std::hypot(g.x - c.x, g.y - c.y) < r)
                hit = true;
        });
        return hit;
    }
    bool inClanRadius(Pos c)
    {
        // clan towns we know of (roster locations of clan mates)
        for (auto &p : a.roster)
            if (p.id != a.me && p.hasLoc && p.clan && p.clan == a.myClan() &&
                std::max(std::abs(p.lx - c.x), std::abs(p.ly - c.y)) <= CLAN_TOWN_RADIUS)
                return true;
        return false;
    }
};
} // namespace

int Placement::curPlaceMode() const
{
    int m = effMode();
    return m == HomelessHall ? PM_HomelessHall : (m == MoveTown ? PM_MoveHall : PM_Normal);
}

const float *Placement::placeCost(Kind k) const
{
    static const float zero[R_Count] = {0, 0, 0, 0};
    static float cost[R_Count];

    int m = effMode();
    if (m == HomelessHall)
        return zero;
    if (m == MoveTown)
        return TOWN_MOVE_COST;
    if (m == Relocate)
        return KINFO[k].cost;

    buildCost(k, cost); // includes the food share
    return cost;
}

IV Placement::placeTile(Kind k, Vector2 world)
{
    return {int(std::floor(world.x / TILE - float(KINFO[k].fw) * .5f + .5f)),
            int(std::floor(world.y / TILE - float(KINFO[k].fh) * .5f + .5f))};
}

int Placement::placeCheck(Kind k, IV t)
{
    PlaceWorld pw{app.world};
    return checkPlace(pw, k, t.x, t.y, curPlaceMode());
}

void Placement::place(Kind k, IV t)
{
    switch (effMode())
    {
    case MoveTown:
        app.cmd.moveTown(t);
        break;
    case Relocate:
        app.cmd.relocate(placeTarget, t);
        break;
    default:
        app.cmd.build(k, t);
        break;
    }
}

void Placement::begin(int mode, Kind k, uint32_t target)
{
    placeMode = mode;
    placeTarget = target;
    buildKind = int(k);
    app.input.mode = Input::M_None;
    app.context.close();

    if (mode == HomelessHall)
        app.msg("Your town fell! Click to place a new Town Hall (free) within 120 tiles of a clan town.");

    else if (mode == MoveTown)
        app.msg("Choose the new site for your Town Hall (500 Gold, 300 Wood, 200 Stone). Esc/right-click cancels.");

    else if (mode == Relocate)
        app.msg("Choose the new site for this building (normal cost, it keeps its level and stock).");
}

void Placement::selectBuildKind(Kind k)
{
    if (int g = app.world.buildGateErr(k); g && buildKind != int(k))
    {
        app.msg("Cannot build " + aName(k) + ": " + gateText(g, k, app.world.thLevel()) + ".");
        return;
    }
    placeMode = Normal;
    buildKind = buildKind == int(k) ? -1 : int(k);
    app.input.mode = Input::M_None;
    if (buildKind >= 0)
        app.msg(std::string("Placing ") + KINFO[k].name +
                ": click to build, Shift keeps building, Esc/right-click cancels.");
}

void Placement::click(Vector2 w, bool shift)
{
    Kind k = Kind(buildKind);
    IV t = placeTile(k, w);
    int err = placeCheck(k, t);
    float need[R_Count];
    for (int r = 0; r < R_Count; r++)
        need[r] = placeCost(k)[r];
    if (effMode() == Normal)
        need[R_Wood] += float(app.world.clearWoodCost(app.world.blockedUnder(k, t))); // pawns clear the site first
    if (err)
        app.msg(std::string("Cannot build here: ") + PLACE_TEXT[err] + ".");
    else if (!app.world.affordable(need))
        app.msg("Not enough resources" + std::string(app.world.roster.empty() ? "." : " (allies may cover the rest)."));
    bool clearing =
        !err && effMode() != Normal && clearClick(k, t); // special placements: clear exactly the blocking tiles first
    if (!err && !clearing)
        place(k, t);
    // special placements stay in their mode until a valid site was chosen
    if (effMode() != 0)
    {
        if (!err && !clearing)
            buildKind = -1;
    }
    else if (!shift && !clearing)
        buildKind = -1;
}

bool Placement::clearClick(Kind k, IV t)
{
    auto v = app.world.blockedUnder(k, t);
    if (v.empty())
        return false;
    int cost = app.world.clearWoodCost(v);
    if (app.world.res[R_Wood] + 1e-3f < float(cost))
    {
        app.msg("Clearing " + std::to_string(v.size()) + " tile(s) needs " + std::to_string(cost) + " wood.");
        return true;
    }
    app.cmd.clear(v);
    return true;
}

std::string Placement::ghostLabel(Kind k, IV t) const
{
    auto v = app.world.blockedUnder(k, t);
    if (v.empty())
        return "";
    if (effMode() == Normal)
        return "Pawns clear " + std::to_string(v.size()) + " obstacle" + (v.size() > 1 ? "s" : "") + " first:";
    return "Click to clear " + std::to_string(v.size()) + " tile" + (v.size() > 1 ? "s" : "") + " first:";
}
