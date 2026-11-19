#include "systems/TerritorySystem.hpp"

#include "common/Progression.hpp"
#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void TerritorySystem::evalFunctional()
{
    std::vector<Pos> ths[MAX_PLAYERS + 1], ops[MAX_PLAYERS + 1];
    std::vector<entt::entity> mine;
    reg.view<Info, Building, Owner>().each([&](entt::entity e, const Info &i, const Building &b, const Owner &o)
                                           {
        if (!isPlayer(o.id)) return;
        mine.push_back(e);
        if (i.kind == K_TownHall && !(b.progress < 1.f && reg.all_of<RelocNew>(e))) ths[o.id].push_back(footCenter(i.kind, b.tx, b.ty)); });
    auto within = [](const std::vector<Pos> &v, Pos c, float r)
    {
        for (auto &a : v)
            if (std::max(std::abs(a.x - c.x), std::abs(a.y - c.y)) <= r)
                return true;
        return false;
    };
    auto center = [&](entt::entity e)
    { const auto& b = reg.get<Building>(e); return footCenter(reg.get<Info>(e).kind, b.tx, b.ty); };
    std::vector<uint8_t> want(mine.size(), 0);
    for (size_t n = 0; n < mine.size(); n++)
    { // Outposts need a Town Hall within reach
        auto e = mine[n];
        if (reg.get<Info>(e).kind != K_Outpost)
            continue;
        int o = reg.get<Owner>(e).id;
        Pos c = center(e);
        bool ok = within(ths[o], c, OUTPOST_REACH) && !reg.all_of<Moving>(e);
        want[n] = ok ? 0 : 1;
        if (ok)
            ops[o].push_back(c);
    }
    int off[MAX_PLAYERS + 1]{}, on[MAX_PLAYERS + 1]{};
    for (size_t n = 0; n < mine.size(); n++)
    {
        auto e = mine[n];
        Kind k = reg.get<Info>(e).kind;
        int o = reg.get<Owner>(e).id;
        if (k == K_TownHall || k == K_Campfire)
            want[n] = 0;
        else if (k != K_Outpost)
        {
            Pos c = center(e);
            want[n] = (reg.all_of<Moving>(e) || !(within(ths[o], c, float(anchorRadius(K_TownHall))) || within(ops[o], c, float(anchorRadius(K_Outpost))))) ? 1 : 0;
        }
        auto *f = reg.try_get<BFlags>(e);
        bool cur = f && f->dis;
        if (bool(want[n]) == cur)
            continue;
        reg.get_or_emplace<BFlags>(e).dis = want[n];
        if (!reg.all_of<Moving>(e))
            (want[n] ? off : on)[o]++;
    }
    for (int id = 1; id <= MAX_PLAYERS; id++)
    {
        if (off[id])
            world.say(id, std::to_string(off[id]) + " building(s) are outside your functional territory and stopped working. Right-click them and choose Relocate.");
        if (on[id])
            world.say(id, std::to_string(on[id]) + " building(s) work again.");
    }
}

void TerritorySystem::doMoveTown(int id, int tx, int ty)
{
    auto th = world.townHallOf(id);
    if (th == entt::null || world.bp[id].homeless)
    {
        world.say(id, "You have no Town Hall to move.");
        return;
    }
    if (world.townMoveActive(id))
    {
        world.say(id, "Your town is already being moved.");
        return;
    }
    if (!world.afford(id, TOWN_MOVE_COST))
    {
        world.say(id, "Moving the town costs 500 Gold, 300 Wood and 200 Stone.");
        return;
    }
    PlaceWorld w{world, id};
    int err = checkPlace(w, K_TownHall, tx, ty, PM_MoveHall);
    if (err)
    {
        world.say(id, std::string("Cannot move the town here: ") + PLACE_TEXT[err] + ".");
        return;
    }
    if (!s.clearing.worldBuildOk(id, K_TownHall, tx, ty))
        return; // [world]
    world.spend(id, TOWN_MOVE_COST);
    auto n = world.makeBuilding(K_TownHall, uint8_t(id), tx, ty, false);
    reg.emplace<RelocNew>(n, RelocNew{reg.get<Uid>(th).id, true, TOWN_MOVE_TIME});
    reg.emplace<Moving>(th, Moving{reg.get<Uid>(n).id});
    Pos c = world.posOf(n);
    world.fx(E_Dust, c.x, c.y);
    world.say(id, "Your new town is under construction (" + std::to_string(int(TOWN_MOVE_TIME)) + " s). Troops and pawns will move in when it is finished.");
}
void TerritorySystem::doRelocate(int id, uint32_t uid, int tx, int ty)
{
    auto be = world.find(uid);
    if (be == entt::null || !reg.all_of<Building>(be) || reg.get<Owner>(be).id != id)
        return;
    Kind k = reg.get<Info>(be).kind;
    if (k == K_TownHall || k == K_Campfire || !world.complete(be))
        return;
    if (!world.outOfTerritory(be) || reg.all_of<Moving>(be))
    {
        world.say(id, "Only buildings outside your territory can be relocated.");
        return;
    }
    if (world.townMoveActive(id))
    {
        world.say(id, "Wait until the town move is finished.");
        return;
    }
    if (!world.afford(id, KINFO[k].cost))
    {
        world.say(id, "Not enough resources to relocate.");
        return;
    }
    PlaceWorld w{world, id};
    int err = checkPlace(w, k, tx, ty);
    if (err)
    {
        world.say(id, std::string("Cannot relocate here: ") + PLACE_TEXT[err] + ".");
        return;
    }
    if (!s.clearing.worldBuildOk(id, k, tx, ty))
        return; // [world]
    world.spend(id, KINFO[k].cost);
    auto n = world.makeBuilding(k, uint8_t(id), tx, ty, false);
    float total = 60.f + 30.f * float(reg.get<Info>(be).level - 1);
    reg.emplace<RelocNew>(n, RelocNew{uid, false, total});
    reg.emplace<Moving>(be, Moving{reg.get<Uid>(n).id});
    Pos c = world.posOf(n);
    world.fx(E_Dust, c.x, c.y);
    world.say(id, std::string("Relocating your ") + KINFO[k].name + " (" + std::to_string(int(total)) + " s). Level and stock are kept.");
}
void TerritorySystem::migrateTown(int ow, entt::entity nth)
{ // every troop and pawn walks into the new town (teleport with dust)
    std::vector<IV> spots;
    const auto &nb = reg.get<Building>(nth);
    Kind k = reg.get<Info>(nth).kind;
    for (int r = 1; r <= 6; r++)
        for (int y = nb.ty - r; y <= nb.ty + KINFO[k].fh - 1 + r; y++)
            for (int x = nb.tx - r; x <= nb.tx + KINFO[k].fw - 1 + r; x++)
            {
                if (std::max({nb.tx - x, x - (nb.tx + KINFO[k].fw - 1), nb.ty - y, y - (nb.ty + KINFO[k].fh - 1)}) != r)
                    continue;
                if (world.walkable(x, y, false, 0))
                    spots.push_back({x, y});
            }
    if (spots.empty())
        return;
    std::vector<entt::entity> us;
    reg.view<Owner, Unit, Info, Pos>().each([&](entt::entity e, const Owner &o, const Unit &, const Info &i, const Pos &)
                                            {
        if (o.id != ow || !isTroop(i.kind) || reg.any_of<Convoy, Haul, Aboard, Boarding>(e)) return;
        us.push_back(e); });
    size_t n = 0;
    for (auto e : us)
    {
        IV s = spots[(n * 7 + 3) % spots.size()];
        Pos old = reg.get<Pos>(e);
        if (n < 10)
            world.fx(E_Dust, old.x, old.y);
        reg.get<Pos>(e) = {float(s.x) + .5f, float(s.y) + .5f};
        auto &u = reg.get<Unit>(e);
        u.path.clear();
        u.i = 0;
        u.lastRev = {INT_MIN, INT_MIN};
        reg.remove<Attack>(e);
        reg.remove<AMove>(e);
        reg.remove<Support>(e);
        reg.remove<Extinguish>(e);
        if (n < 10)
            world.fx(E_Dust, float(s.x) + .5f, float(s.y) + .5f);
        n++;
    }
    world.say(ow, std::to_string(n) + " troops and pawns moved into your new town.");
}
void TerritorySystem::finishReloc(entt::entity n)
{
    RelocNew rn = reg.get<RelocNew>(n);
    uint32_t nuid = reg.get<Uid>(n).id;
    reg.remove<RelocNew>(n);
    int ow = reg.get<Owner>(n).id;
    auto old = world.find(rn.from);
    auto &in = reg.get<Info>(n);
    auto &nb = reg.get<Building>(n);
    if (old != entt::null)
    {
        const auto &oi = reg.get<Info>(old);
        const auto &ob = reg.get<Building>(old);
        in.level = oi.level; // level and stock are carried over
        auto &h = reg.get<Health>(n);
        h.maxhp = KINFO[in.kind].hp * levelHp(in.level);
        h.hp = h.maxhp;
        for (int r = 0; r < R_Count; r++)
            nb.stock[r] = ob.stock[r];
        nb.haulPct = ob.haulPct;
        if (auto *tq = reg.try_get<TrainQ>(old))
            reg.emplace_or_replace<TrainQ>(n, *tq);
        reg.view<Worker>().each([&](Worker &w)
                                { if (w.home == rn.from) w.home = nuid; });
        reg.view<Haul>().each([&](Haul &hl)
                              { if (hl.storage == rn.from) hl.storage = nuid; });
        Pos oc = world.posOf(old);
        world.fx(E_Dust, oc.x, oc.y);
        reg.remove<Moving>(old);
        world.destroyNow(old); // the old hall / building is removed (no defeat: another Town Hall exists)
    }
    Pos c = world.posOf(n);
    world.fx(E_Dust, c.x, c.y);
    if (rn.town)
    {
        world.pl[ow].cam = c;
        migrateTown(ow, n);
        world.say(ow, "Your town has moved! Buildings outside the new territory stopped working - relocate them.");
    }
    else
        world.say(ow, std::string("Your ") + KINFO[in.kind].name + " was relocated.");
    evalFunctional();
}
void TerritorySystem::relocSecond()
{
    std::vector<entt::entity> done, stale;
    reg.view<RelocNew, Building>().each([&](entt::entity e, const RelocNew &, const Building &b)
                                        { if (b.progress >= 1.f) done.push_back(e); });
    for (auto e : done)
        finishReloc(e);
    reg.view<Moving>().each([&](entt::entity e, const Moving &m)
                            { if (world.find(m.to) == entt::null) stale.push_back(e); }); // replacement was destroyed: cancel the move
    for (auto e : stale)
        reg.remove<Moving>(e);
    std::vector<entt::entity> orphan; // old building destroyed while the replacement is under construction: keep building
    (void)orphan;
}
