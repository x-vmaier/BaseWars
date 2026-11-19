/**
 * @file SpawnSystem.cpp
 * @brief Where new or defeated players found their base, plus the spawn test hooks.
 */
#include "systems/SpawnSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

bool SpawnSystem::clearingOk(int cx, int cy)
{
    for (int y = cy - 6; y <= cy + 6; y++)
        for (int x = cx - 6; x <= cx + 6; x++)
        {
            int b = world.biome(x, y);
            if (!isLand(b) || b == B_Highlands || b == B_Snow || b == B_Desert || isCliffTile(b, world.biome(x, y - 1)))
                return false;
        }
    for (int a = 1; a <= MAX_PLAYERS; a++)
        if (world.pl[a].used && world.bitAt(world.pl[a].expl, cx, cy))
            return false;
    bool nearBase = false;
    reg.view<Info, Building>().each([&](const Info &i, const Building &b)
    {
        if (i.kind != K_TownHall)
            return;
        if (std::max(std::abs(b.tx - cx), std::abs(b.ty - cy)) < 70)
            nearBase = true;
    });
    return !nearBase;
}
IV SpawnSystem::findClearing(const Pos *anchor)
{
    if (anchor) // new bases appear close to an existing player, but outside their base
        for (int n = 0; n < 6000; n++)
        {
            float ang = float(world.rng() % 6283) / 1000.f,
                  r = float(SPAWN_MIN_DIST + int(world.rng() % uint32_t(SPAWN_RADIUS - SPAWN_MIN_DIST)));
            int x = int(anchor->x + std::cos(ang) * r), y = int(anchor->y + std::sin(ang) * r);
            if (clearingOk(x, y))
                return {x, y};
        }
    int ths = 0;
    reg.view<Info>().each([&](const Info &i)
    {
        if (i.kind == K_TownHall)
            ths++;
    });
    for (int pass = 0; pass < 3; pass++)
        for (int n = 0; n < 12000; n++)
        {
            float ang = float(world.rng() % 6283) / 1000.f,
                  r = (ths ? 90.f : 0.f) + float(n) * .35f + float(pass) * 400.f;
            int x = int(std::cos(ang) * r), y = int(std::sin(ang) * r);
            if (clearingOk(x, y))
                return {x, y};
        }
    return {0, 0};
}
// Where should a newcomer (or a player who lost their base) appear? Near Steam friends, then allies / clan mates, then
// any player.
bool SpawnSystem::spawnAnchor(int id, const std::vector<uint64_t> &friends, Pos &out)
{
    std::vector<Pos> friendsAt, alliesAt, anyAt;
    reg.view<Info, Building, Owner>().each([&](const Info &i, const Building &b, const Owner &o)
    {
        if (i.kind != K_TownHall || !isPlayer(o.id) || o.id == id)
            return;
        Pos c = footCenter(i.kind, b.tx, b.ty);
        bool isFriend = world.pl[o.id].steamId != 0 &&
                        std::find(friends.begin(), friends.end(), world.pl[o.id].steamId) != friends.end();
        (isFriend ? friendsAt : (world.allied(id, o.id) ? alliesAt : anyAt)).push_back(c);
    });
    auto &pick = !friendsAt.empty() ? friendsAt : (!alliesAt.empty() ? alliesAt : anyAt);
    if (pick.empty())
        return false;
    out = pick[world.rng() % pick.size()];
    return true;
}
void SpawnSystem::spawnBase(int id, const std::vector<uint64_t> &friends)
{
    Pos anchor;
    IV c = spawnAnchor(id, friends, anchor) ? findClearing(&anchor) : findClearing();
    testSpawn(c); // BW_SPAWN test hook
    auto th = world.makeBuilding(K_TownHall, uint8_t(id), c.x - 2, c.y - 1, true);
    Pos p = world.posOf(th);
    world.pl[id].cam = p;
    for (int i = 0; i < 2; i++)
        world.makeUnit(K_Pawn, uint8_t(id), p.x - 1.f + float(i) * 2.f, p.y + 2.6f);
    world.makeUnit(K_Warrior, uint8_t(id), p.x, p.y + 3.2f);
    world.fx(E_Dust, p.x, p.y);
}

// Test hook: BW_SPAWN=x,y puts the first new base at the clearing nearest to that point.
bool SpawnSystem::testSpawn(IV &c)
{
    static const char *s = std::getenv("BW_SPAWN");
    if (!s)
        return false;
    int x = 0, y = 0;
    if (std::sscanf(s, "%d,%d", &x, &y) != 2)
        return false;
    for (int r = 0; r < 60; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
            {
                if (std::max(std::abs(dx), std::abs(dy)) != r)
                    continue;
                if (clearingOk(x + dx, y + dy))
                {
                    c = {x + dx, y + dy};
                    return true;
                }
            }
    return false;
}

// test hook (BW_ARMY=<n>): a freshly spawned player also gets n extra warriors
void SpawnSystem::worldTestSpawn(int id, bool fresh)
{
    static const char *a = std::getenv("BW_ARMY");
    static const char *hurt = std::getenv("BW_HURT"); // test hook BW_HURT=<fraction>: the new Town Hall starts damaged
    auto th = world.townHallOf(id);
    if (!fresh || th == entt::null)
        return;
    if (hurt)
    {
        auto &h = reg.get<Health>(th);
        h.hp = h.maxhp * std::clamp(float(std::atof(hurt)), .01f, 1.f);
    }
    if (!a)
        return;
    Pos at;
    for (int i = 0; i < std::atoi(a); i++)
        if (world.spawnSpot(th, false, at))
        {
            world.makeUnit(K_Warrior, uint8_t(id), at.x + float(i) * .01f, at.y);
        }
}

void SpawnSystem::baseTestInit(int id)
{ // test hook BW_BASETEST: plenty of resources, level 3 troops
    static const bool on = std::getenv("BW_BASETEST") != nullptr;
    if (!on || world.bp[id].testDone)
        return;
    world.bp[id].testDone = true;
    for (float &r : world.pl[id].res)
        r = std::max(r, 6000.f);
    for (int k = K_Pawn; k <= K_Monk; k++)
        world.pl[id].ulevel[k] = 3;
}
