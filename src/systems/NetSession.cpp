#include "systems/NetSession.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void NetSession::onHello(uint64_t conn, Reader &r)
{
    std::string uuid = r.str(), name = r.str();
    uint64_t steamId = r.get<uint64_t>();
    std::vector<uint64_t> friends;
    int nf = r.get<uint16_t>();
    for (int i = 0; i < nf && i < 200; i++)
        friends.push_back(r.get<uint64_t>());
    if (!r.ok || uuid.size() < 8 || uuid.size() > 64)
    {
        world.net.kick(conn, false);
        return;
    }
    for (char &c : name)
        if (c < 32 || c == 127)
            c = '?';
    if (name.empty())
        name = "Player";
    if (name.size() > 20)
        name.resize(20);
    int id = 0;
    for (int i = 1; i <= MAX_PLAYERS; i++)
        if (world.pl[i].used && world.pl[i].uuid == uuid)
            id = i;
    if (id && world.pl[id].online)
    {
        Writer w;
        w.put<uint8_t>(S_Text);
        w.str("This profile is already connected.");
        world.net.send(conn, w);
        world.net.kick(conn, true);
        return;
    }
    bool fresh = false;
    if (!id)
    {
        for (int i = 1; i <= MAX_PLAYERS && !id; i++)
            if (!world.pl[i].used)
                id = i;
        if (!id)
        { // recycle the longest-offline profile
            uint64_t oldest = UINT64_MAX;
            for (int i = 1; i <= MAX_PLAYERS; i++)
                if (!world.pl[i].online && world.pl[i].lastSeen < oldest)
                {
                    oldest = world.pl[i].lastSeen;
                    id = i;
                }
            if (!id)
            {
                world.net.kick(conn, false);
                return;
            }
            s.clan.leaveClan(id);
            world.removeOwner(id);
            for (int a = 0; a <= MAX_PLAYERS; a++)
                world.pl[a].allied[id] = world.pl[a].pending[id] = false;
            world.pl[id] = Profile{};
            world.bp[id] = BaseProf{};
        }
        fresh = true;
        world.pl[id].used = true;
        world.pl[id].uuid = uuid;
        world.resetProfile(id);
        for (int f = 0; f < FACTION_COUNT; f++) // a configurable share of the factions are aggressive towards each newcomer
            if (hash2(int(std::hash<std::string>{}(uuid) & 0x7fffffff), f, world.seed + 71) % 100 < uint32_t(cfg().hostileFactionPercent))
                world.pl[id].rep[f] = -50.f;
        if (const char *ag = std::getenv("BW_AGGRO"))
        {
            int v = std::atoi(ag);
            for (int f = 0; f < FACTION_COUNT; f++)
                if (v >= FACTION_COUNT || v == f)
                    world.pl[id].rep[f] = -50.f;
        } // test hook
    }
    Profile &p = world.pl[id];
    p.name = name;
    p.online = true;
    p.conn = conn;
    p.steamId = steamId;
    world.bp[id].friends = friends;
    world.connPlayer[conn] = id;
    p.lastSeen = nowSec();
    p.sentFog.clear();
    p.sentTerr.clear();
    if (fresh)
        s.spawn.spawnBase(id, friends);
    else
    {
        bool hasTH = false;
        reg.view<Info, Owner>().each([&](const Info &i, const Owner &o)
                                     { if (i.kind == K_TownHall && o.id == id) hasTH = true; });
        if (!hasTH && !world.bp[id].homeless)
        {
            world.resetProfile(id);
            s.spawn.spawnBase(id, friends);
            world.say(id, "Your base was destroyed while you were away. You start over.");
        }
    }
    if (const char *rv = std::getenv("BW_REVEAL"))
    { // test hook: reveal a large disc around the base
        Pos c = p.cam;
        world.reveal(id, int(c.x), int(c.y), std::atoi(rv));
    }
    Writer w;
    w.put<uint8_t>(S_Welcome).put<uint8_t>(uint8_t(id)).put<uint32_t>(world.seed).put<float>(p.cam.x).put<float>(p.cam.y);
    w.str(p.name);
    putRules(w); // progression / food / donation tunables (common/Progression.hpp)
    world.net.send(conn, w);
    s.repl.sendFog(id);
    s.clearing.sendClearedFull(id); // [world] cleared surface features
    s.spawn.worldTestSpawn(id, fresh);
    s.repl.sendRoster();
    world.say(id, fresh ? "Welcome, " + p.name + "! Press B to open the building menu." : "Welcome back, " + p.name + "! Your base is just as you left it.");
    std::printf("%s (%d) %s\n", p.name.c_str(), id, fresh ? "joined" : "rejoined");
}
void NetSession::onDisconnect(uint64_t conn)
{
    auto it = world.connPlayer.find(conn);
    int id = it == world.connPlayer.end() ? 0 : it->second;
    world.connPlayer.erase(conn);
    if (id < 1 || id > MAX_PLAYERS || world.pl[id].conn != conn)
        return;
    world.pl[id].online = false;
    world.pl[id].conn = 0;
    world.pl[id].lastSeen = nowSec();
    std::printf("%s left\n", world.pl[id].name.c_str());
    s.repl.sendRoster();
}
