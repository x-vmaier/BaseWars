/**
 * @file RaidSystem.cpp
 * @brief Faction raiding parties against hostile players (when and how strong: PacingSystem).
 */
#include "systems/RaidSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Nearest living town of faction `f` within 400 tiles.
const Town *RaidSystem::nearestTown(int f, Pos tp)
{
    const Town *best = nullptr;
    float bd = 400.f;

    for (auto &t : world.towns)
    {
        if (!t.alive || t.faction != f)
            continue;

        float d = std::hypot(float(t.cx) - tp.x, float(t.cy) - tp.y);
        if (d < bd)
        {
            bd = d;
            best = &t;
        }
    }

    return best;
}

bool RaidSystem::spawnRaid(int player, int f, int n)
{
    auto th = world.townHallOf(player);
    if (th == entt::null)
        return false;
    Pos tp = world.posOf(th);

    const Town *best = nearestTown(f, tp);
    if (!best)
        return false;

    int sent = 0;

    static const Kind mix[] = {K_Warrior, K_Archer, K_Lancer, K_Warrior, K_Archer};
    for (int i = 0; i < n; i++)
    {
        Kind k = mix[i % 5];
        float a = float(i) * 0.9f;
        float x = float(best->cx) + .5f + std::cos(a) * 3.2f, y = float(best->cy) + 3.5f + std::sin(a) * 1.2f;
        if (!world.walkable(int(std::floor(x)), int(std::floor(y)), false, 0))
        {
            x = float(best->cx) + .5f;
            y = float(best->cy) + 3.5f;
        }

        auto u = world.makeUnit(k, uint8_t(FACTION_BASE + f), x, y);
        reg.emplace<Raider>(u, Raider{uint8_t(player), world.clock + 600.0, 0});
        if (world.order(u, tp, 0, 40000))
        {
            reg.emplace_or_replace<AMove>(u, AMove{tp});
            sent++;
        }
        else
            world.destroyNow(u);
    }

    if (sent)
        world.say(player,
                  std::string("The raiding party of the ") + FACTION_NAME[f] + " is marching on your base! Defend it!");
    return sent > 0;
}

void RaidSystem::runRaider(entt::entity e, Unit &u, Raider &rd, float dt)
{
    if (world.clock > rd.until || !isPlayer(rd.target) || world.townHallOf(rd.target) == entt::null)
    {
        world.destroyNow(e);
        return;
    }

    Pos p = reg.get<Pos>(e);
    auto th = world.townHallOf(rd.target);
    Pos tp = world.posOf(th);

    rd.repath -= dt;
    if (!reg.all_of<Attack>(e) && u.path.empty() && rd.repath <= 0 && std::hypot(p.x - tp.x, p.y - tp.y) > 3.f)
    {
        rd.repath = 4.f;
        world.order(e, tp, 0, 40000);
        reg.emplace_or_replace<AMove>(e, AMove{tp});
    }
}
