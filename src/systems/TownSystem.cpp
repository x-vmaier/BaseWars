/**
 * @file TownSystem.cpp
 * @brief NPC faction towns: founding per region, garrisons, destruction / respawn, regeneration.
 */
#include "systems/TownSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

bool TownSystem::townSiteOk(int cx, int cy)
{
    for (int y = cy - 7; y <= cy + 7; y++)
        for (int x = cx - 7; x <= cx + 7; x++)
        {
            int b = world.biome(x, y);
            if (!isLand(b) || b == B_Highlands || isCliffTile(b, world.biome(x, y - 1)) || world.occ.count(tkey(x, y)))
                return false;
        }

    bool nearTh = false;
    reg.view<Info, Building>().each([&](const Info &i, const Building &b)
    {
        if (i.kind == K_TownHall && std::max(std::abs(b.tx - cx), std::abs(b.ty - cy)) < cfg().townMinDistance)
            nearTh = true;
    });

    return !nearTh;
}

bool TownSystem::footprintFree(Kind k, int tx, int ty)
{
    for (int y = ty; y < ty + KINFO[k].fh; y++)
        for (int x = tx; x < tx + KINFO[k].fw; x++)
            if (!world.walkable(x, y, false, 0))
                return false;
    return true;
}

void TownSystem::buildTown(Town &t)
{
    uint8_t owner = uint8_t(FACTION_BASE + t.faction);

    struct Piece
    {
        Kind k;
        int dx, dy;
    };
    static const Piece pieces[] = {{K_TownHall, -2, -1}, {K_Storage, 3, -1}, {K_Outpost, -5, -3},
                                   {K_Outpost, 3, 3},    {K_Lumber, -5, 1},  {K_Lumber, 0, 3}};
    for (const Piece &pc : pieces)
    {
        if (!footprintFree(pc.k, t.cx + pc.dx, t.cy + pc.dy))
            continue;

        auto e = world.makeBuilding(pc.k, owner, t.cx + pc.dx, t.cy + pc.dy, true);
        reg.emplace<TownTag>(e, TownTag{t.id});
        if (pc.k == K_TownHall)
            t.th = reg.get<Uid>(e).id;
    }

    static const Kind roster[] = {K_Warrior, K_Warrior, K_Warrior, K_Archer, K_Archer, K_Pawn, K_Pawn};
    int i = 0;
    for (Kind k : roster)
        spawnTownUnit(t, k, i++);

    t.alive = true;
    t.nextSpawn = world.clock + 25;
}

void TownSystem::spawnTownUnit(const Town &t, Kind k, int i)
{
    float a = float(i) * 0.9f + float(t.id);
    float x = float(t.cx) + .5f + std::cos(a) * 3.2f, y = float(t.cy) + .5f + 2.4f + std::sin(a) * 1.4f;
    if (!world.walkable(int(std::floor(x)), int(std::floor(y)), false, 0))
    {
        x = float(t.cx) + .5f;
        y = float(t.cy) + 3.5f;
    }

    auto u = world.makeUnit(k, uint8_t(FACTION_BASE + t.faction), x, y);
    reg.emplace<TownTag>(u, TownTag{t.id});
    reg.emplace<Home>(u, Home{float(t.cx) + .5f, float(t.cy) + 3.f, 0});
    reg.emplace<Wander>(u, Wander{float(world.rng() % 400) / 100.f});
}

void TownSystem::processRegion(int rx, int ry)
{
    if (!world.regionsDone.insert(chunkKey(rx, ry)).second)
        return;
    if (hash2(rx, ry, world.seed + 1111) % 100 >= uint32_t(cfg().townRegionChance))
        return;

    int span = REGION_CHUNKS * CH, x0 = rx * span, y0 = ry * span;
    for (int n = 0; n < 40; n++)
    {
        int x = x0 + 14 + int(hash2(rx * 40 + n, ry, world.seed + 21) % uint32_t(span - 28)),
            y = y0 + 14 + int(hash2(rx, ry * 40 + n, world.seed + 22) % uint32_t(span - 28));
        if (!townSiteOk(x, y))
            continue;

        Town t{uint16_t(world.towns.size() + 1),
               uint8_t(hash2(x, y, world.seed + 33) % FACTION_COUNT),
               x,
               y,
               0,
               true,
               0,
               0,
               -100};

        world.towns.push_back(t);
        buildTown(world.towns.back());
        std::printf("Town %d (%s) founded at %d,%d\n", t.id, FACTION_NAME[t.faction], x, y);
        return;
    }
}

void TownSystem::townDestroyed(uint16_t town, int killer)
{
    Town *t = world.townById(town);
    if (!t || !t->alive)
        return;
    t->alive = false;
    t->respawnAt = world.clock + 600;

    std::vector<entt::entity> all;
    reg.view<TownTag>().each([&](entt::entity e, const TownTag &g)
    {
        if (g.town == town)
            all.push_back(e);
    });

    for (auto e : all)
    {
        if (isBuilding(reg.get<Info>(e).kind))
        {
            Pos p = world.posOf(e);
            world.fx(E_Explosion, p.x, p.y);
        }
        world.destroyNow(e);
    }

    if (isPlayer(killer))
    {
        for (int r = 0; r < R_Count; r++)
            world.pl[killer].res[r] += 120;
        world.say(killer,
                  std::string("You razed a ") + FACTION_NAME[t->faction] + " town! Loot: 120 of every resource.");
    }
}

void TownSystem::townTick()
{
    for (auto &t : world.towns)
    {
        if (!t.alive)
        {
            if (world.clock >= t.respawnAt)
                buildTown(t);
            continue;
        }

        int army = 0;
        reg.view<TownTag, Info>().each([&](entt::entity pe, const TownTag &g, const Info &i)
        {
            if (g.town == t.id && isArmy(i.kind) && !world.isPatrol(pe))
                army++;
        });

        int desired = std::min(9, 5 + int(world.facDonated[t.faction] / 300.f));
        if (army < desired && world.clock >= t.nextSpawn)
        {
            spawnTownUnit(t, hash2(t.id, int(world.clock), world.seed) & 1 ? K_Warrior : K_Archer, army);
            t.nextSpawn = world.clock + 25;
        }
    }

    // faction buildings and units mend themselves when left alone
    std::vector<entt::entity> hurt;
    reg.view<Owner, Health>().each([&](entt::entity e, const Owner &o, const Health &h)
    {
        if (isFactionOwner(o.id) && h.hp < h.maxhp)
            hurt.push_back(e);
    });

    for (auto e : hurt)
    {
        auto *hit = reg.try_get<Hit>(e);
        if (hit && world.clock - hit->t < 20)
            continue;

        auto &h = reg.get<Health>(e);
        h.hp = std::min(h.maxhp, h.hp + h.maxhp * .02f);
    }
}
