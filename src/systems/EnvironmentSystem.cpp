#include "systems/EnvironmentSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void EnvironmentSystem::coldAndWake(const std::vector<entt::entity> &units, const std::vector<entt::entity> &blds)
{
    // cold attrition in the snow unless a campfire warms the spot
    std::vector<Pos> fires;
    std::vector<int> fireOwner;
    for (auto e : blds)
        if (hasTag(reg.get<Info>(e).kind, KT_Warm) && world.complete(e))
        {
            fires.push_back(world.posOf(e));
            fireOwner.push_back(reg.get<Owner>(e).id);
        }
    auto warm = [&](Pos p, int owner)
    {
        for (size_t i = 0; i < fires.size(); i++)
            if (world.allied(owner, fireOwner[i]) && std::hypot(p.x - fires[i].x, p.y - fires[i].y) <= WARMTH_RADIUS)
                return true;
        return false;
    };
    std::vector<entt::entity> all = units;
    all.insert(all.end(), blds.begin(), blds.end());
    for (auto e : all)
    {
        if (!reg.valid(e))
            continue;
        int ow = reg.get<Owner>(e).id;
        if (ow == 0 || isFactionOwner(ow))
            continue;
        Pos p = world.posOf(e);
        if (world.biome(int(std::floor(p.x)), int(std::floor(p.y))) != B_Snow || warm(p, ow))
            continue;
        auto &h = reg.get<Health>(e);
        if (isBuilding(reg.get<Info>(e).kind))
            h.hp = std::max(h.maxhp * .25f, h.hp - h.maxhp * COLD_BUILDING_FRAC);
        else
            s.combat.damage(e, COLD_UNIT_DPS, 0);
    }
    s.combat.processKills();
    // wake up NPC chunks around every player-owned entity
    for (auto e : all)
    {
        if (!reg.valid(e) || !isPlayer(reg.get<Owner>(e).id))
            continue;
        Pos p = world.posOf(e);
        int cx = chunkOf(int(std::floor(p.x))), cy = chunkOf(int(std::floor(p.y)));
        for (int y = cy - 2; y <= cy + 2; y++)
            for (int x = cx - 2; x <= cx + 2; x++)
                s.hunt.spawnChunkNpcs(x, y);
        int rx = floorDiv(cx, REGION_CHUNKS), ry = floorDiv(cy, REGION_CHUNKS);
        for (int y = ry - 1; y <= ry + 1; y++)
            for (int x = rx - 1; x <= rx + 1; x++)
                s.town.processRegion(x, y);
    }
}

void EnvironmentSystem::explore(const std::vector<entt::entity> &units, const std::vector<entt::entity> &blds)
{
    // exploration by online players' units
    for (auto e : units)
    {
        if (!reg.valid(e))
            continue;
        int ow = reg.get<Owner>(e).id;
        if (!isPlayer(ow) || !world.pl[ow].online)
            continue;
        Pos p = reg.get<Pos>(e);
        IV t{int(std::floor(p.x)), int(std::floor(p.y))};
        auto &u = reg.get<Unit>(e);
        if (u.lastRev.x == t.x && u.lastRev.y == t.y)
            continue;
        u.lastRev = t;
        world.reveal(ow, t.x, t.y, KINFO[reg.get<Info>(e).kind].vision);
    }
    for (auto e : blds)
    { // keep building vision fresh for players who just (re)joined
        if (!reg.valid(e))
            continue; // (baseSecond may have removed relocated buildings)
        int ow = reg.get<Owner>(e).id;
        if (!isPlayer(ow) || !world.pl[ow].online || !world.complete(e))
            continue;
        if (world.tick % 40 != 0)
            continue;
        Pos c = world.posOf(e);
        world.reveal(ow, int(std::floor(c.x)), int(std::floor(c.y)), KINFO[reg.get<Info>(e).kind].vision);
    }
}
