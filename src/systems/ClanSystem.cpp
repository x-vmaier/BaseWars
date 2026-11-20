/**
 * @file ClanSystem.cpp
 * @brief Alliances, clans, and players who lost their town (homeless clan members / normal respawn).
 */
#include "systems/ClanSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void ClanSystem::breakAlliance(int a, int b)
{
    world.pl[a].allied[b] = world.pl[b].allied[a] = false;
    world.pl[a].pending[b] = world.pl[b].pending[a] = false;
    world.say(a, "Alliance with " + world.pl[b].name + " ended.");
    world.say(b, world.pl[a].name + " ended the alliance!");
    s.repl.sendRoster();
}

void ClanSystem::leaveClan(int id)
{
    int c = world.pl[id].clan;
    if (!c)
        return;
    world.pl[id].clan = 0;

    if (world.bp[id].homeless)
        respawnNormally(id); // a homeless member without a clan restarts normally
    world.notifyClan(c, world.pl[id].name + " left the clan.");
    world.say(id, "You left the clan.");

    int next = 0;
    for (int i = 1; i <= MAX_PLAYERS; i++)
        if (world.pl[i].used && world.pl[i].clan == c)
        {
            next = i;
            break;
        }

    if (!next)
        world.clans[c] = Clan{};
    else if (world.clans[c].leader == id)
    {
        world.clans[c].leader = uint8_t(next);
        world.say(next, "You are now the clan leader.");
    }

    s.repl.sendRoster();
}

// True: handled (homeless clan member); false: normal respawn.
bool ClanSystem::baseDefeat(int victim)
{
    auto mates = world.clanHalls(victim);
    if (mates.empty())
        return false;

    world.removeOwner(victim);
    for (int r = 0; r < R_Count; r++)
        world.pl[victim].res[r] = 0;

    emergencyDonations(victim, mates);
    world.bp[victim].homeless = true;

    Pos hall{0, 0};
    for (int m : mates)
    {
        Pos c = world.posOf(world.townHallOf(m));
        hall = c;
        world.reveal(victim, int(std::floor(c.x)), int(std::floor(c.y)), int(CLAN_TOWN_RADIUS));
    }
    world.pl[victim].cam = hall;
    world.pl[victim].sentFog.clear();
    world.pl[victim].sentTerr.clear();

    world.say(victim, "Your Town Hall fell! You are homeless: place a new Town Hall (free) within " +
                          std::to_string(int(CLAN_TOWN_RADIUS)) + " tiles of a clan Town Hall.");
    world.sendAlert(victim, 2, hall, "Your town was destroyed. Place a new Town Hall near your clan.");
    world.notifyClan(world.pl[victim].clan,
                     world.pl[victim].name + " lost their town and is homeless. Resources were donated.");
    std::printf("%s was defeated (clan: homeless)\n", world.pl[victim].name.c_str());
    return true;
}

// Every mate gives a share of its stock.
void ClanSystem::emergencyDonations(int victim, const std::vector<int> &mates)
{
    for (int m : mates)
    {
        std::string txt;
        for (int r = 0; r < R_Count; r++)
        {
            float give = std::min(std::floor(world.pl[m].res[r] * .15f), 200.f);
            if (give <= 0)
                continue;

            world.pl[m].res[r] -= give;
            world.pl[victim].res[r] += give;
            txt += std::to_string(int(give)) + " " + RES_NAME[r] + "  ";
        }
        if (txt.empty())
            continue;

        world.say(m, "You donated to " + world.pl[victim].name + " (homeless): " + txt);
        world.say(victim, world.pl[m].name + " donated " + txt);
    }
}

void ClanSystem::respawnNormally(int id)
{
    world.bp[id].homeless = false;
    world.removeOwner(id);
    world.resetProfile(id);
    s.spawn.spawnBase(id, world.bp[id].friends);

    world.pl[id].sentFog.clear();
    world.pl[id].sentTerr.clear();
    world.say(id, "Your clan has no town left: you start over near other players.");
}

void ClanSystem::homelessSecond()
{
    for (int id = 1; id <= MAX_PLAYERS; id++)
        if (world.pl[id].used && world.bp[id].homeless && world.clanHalls(id).empty())
            respawnNormally(id);
}

void ClanSystem::homelessBuild(int id, int tx, int ty)
{
    if (!world.bp[id].homeless)
    {
        world.say(id, "You can only have one Town Hall. Right-click it and choose Move town to relocate.");
        return;
    }

    PlaceWorld w{world, id};
    int err = checkPlace(w, K_TownHall, tx, ty, PM_HomelessHall);
    if (err)
    {
        world.say(id, std::string("Cannot build here: ") + PLACE_TEXT[err] + ".");
        return;
    }
    if (!s.clearing.worldBuildOk(id, K_TownHall, tx, ty))
        return; // trees / rocks / gold veins must be cleared first

    auto e = world.makeBuilding(K_TownHall, uint8_t(id), tx, ty, false);
    Pos c = world.posOf(e);
    world.bp[id].homeless = false;
    world.pl[id].cam = c;
    world.fx(E_Dust, c.x, c.y);

    world.say(id, "Your new hometown is being founded. Welcome back among your clan!");
    world.notifyClan(world.pl[id].clan, world.pl[id].name + " is founding a new town.");
}
