/**
 * @file NetSession.cpp
 * @brief Connection lifecycle: hello (profile lookup / creation / respawn), disconnect.
 */
#include "systems/NetSession.hpp"

#include "common/NameToken.hpp"
#include "common/Snapshot.hpp"
#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void NetSession::bindConnection(uint64_t conn, int id, uint64_t steamId, const std::vector<uint64_t> &friends)
{
    Profile &p = world.pl[id];
    p.name = nameToken(steamId); // runtime only: clients resolve the persona name themselves
    p.online = true;
    p.conn = conn;
    p.steamId = steamId;
    p.lastSeen = nowSec();
    p.lastInputSeq = 0;
    p.sentFog.clear();
    p.sentTerr.clear();

    world.bp[id].friends = friends;
    world.connPlayer[conn] = id;
}

void NetSession::reject(uint64_t conn, const char *why)
{
    Writer w;
    w.put<uint8_t>(S_Text);
    w.str(why);
    world.net.send(conn, w);
    world.net.endAuth(conn);
    world.net.kick(conn, true);
}

void NetSession::onHello(uint64_t conn, Reader &r)
{
    uint16_t version = r.get<uint16_t>();
    if (r.ok && version != PROTOCOL_VERSION)
    {
        reject(conn, ("Version mismatch: this server speaks protocol " + std::to_string(PROTOCOL_VERSION) +
                      ", your game " + std::to_string(version) + ". Update the game.")
                         .c_str());
        return;
    }
    uint64_t steamId = r.get<uint64_t>();
    size_t ticketLen = r.get<uint16_t>();
    const uint8_t *t = r.bytes(ticketLen);
    std::vector<uint8_t> ticket;
    if (t && ticketLen <= 2048)
        ticket.assign(t, t + ticketLen);
    std::vector<uint64_t> friends;
    int nf = r.get<uint16_t>();
    for (int i = 0; i < nf && i < 200; i++)
        friends.push_back(r.get<uint64_t>());
    if (!r.ok || ticketLen > 2048)
    {
        world.net.kick(conn, false);
        return;
    }
    if (pending.count(conn))
        return; // one hello per connection

    switch (world.net.beginAuth(conn, steamId, ticket))
    {
    case AuthResult::Rejected:
        reject(conn, "Steam could not verify your login. Start Steam and try again.");
        break;
    case AuthResult::Pending:
        pending[conn] = {steamId, friends, world.clock};
        break;
    case AuthResult::Accepted:
        finishHello(conn, steamId, friends);
        break;
    }
}

void NetSession::onAuthResult(uint64_t conn, bool ok)
{
    auto it = pending.find(conn);
    if (it == pending.end())
        return; // late verdict for a connection that is already gone
    PendingHello h = std::move(it->second);
    pending.erase(it);
    if (ok)
        finishHello(conn, h.steamId, h.friends);
    else
        reject(conn, "Steam rejected your login ticket.");
}

void NetSession::authSecond()
{
    std::vector<uint64_t> stale;
    for (auto &[conn, h] : pending)
        if (world.clock - h.since > cfg().authTimeoutS)
            stale.push_back(conn);
    for (uint64_t conn : stale)
    {
        pending.erase(conn);
        reject(conn, "Steam did not answer in time. Try again.");
    }
}

// Ownership is keyed only by the verified SteamID: any machine that proves the id gets exactly that town.
void NetSession::finishHello(uint64_t conn, uint64_t steamId, const std::vector<uint64_t> &friends)
{
    int id = 0;
    for (int i = 1; i <= MAX_PLAYERS; i++)
        if (world.pl[i].used && world.pl[i].steamId == steamId)
            id = i;
    if (id && world.pl[id].online)
    {
        // The id is authenticated, so a new login replaces a stale or duplicate session of the same account.
        uint64_t old = world.pl[id].conn;
        world.net.kick(old, false);
        onDisconnect(old);
    }

    bool fresh = false;
    if (!id)
    {
        id = claimSlot();
        if (!id)
        {
            reject(conn, "The server is full.");
            return;
        }
        fresh = true;
        initNewProfile(id, steamId);
    }

    bindConnection(conn, id, steamId, friends);
    Profile &p = world.pl[id];

    if (fresh)
        s.spawn.spawnBase(id, friends);
    else
        respawnIfDestroyed(id, friends);

    if (const char *rv = std::getenv("BW_REVEAL"))
    {
        Pos c = p.cam;
        world.reveal(id, int(c.x), int(c.y), std::atoi(rv));
    }

    sendWelcome(conn, id);
    s.defeat.onLogin(id);
    s.repl.sendFog(id);
    s.clearing.sendClearedFull(id);
    s.spawn.worldTestSpawn(id, fresh);
    s.repl.sendRoster();
    if (fresh || world.bp[id].defeat.phase == DF_None)
        world.say(id, fresh ? "Welcome, " + p.name + "! Press B to open the building menu."
                            : "Welcome back, " + p.name + "! Your base is just as you left it.");
    std::printf("%llu (slot %d) %s\n", static_cast<unsigned long long>(steamId), id, fresh ? "joined" : "rejoined");
}

int NetSession::claimSlot()
{
    int id = 0;
    for (int i = 1; i <= MAX_PLAYERS && !id; i++)
        if (!world.pl[i].used)
            id = i;
    if (id)
        return id;

    // All slots are taken: recycle the longest-offline profile.
    uint64_t oldest = UINT64_MAX;
    for (int i = 1; i <= MAX_PLAYERS; i++)
        if (!world.pl[i].online && world.pl[i].lastSeen < oldest)
        {
            oldest = world.pl[i].lastSeen;
            id = i;
        }
    if (!id)
        return 0;

    s.clan.leaveClan(id);
    world.removeOwner(id);
    for (int a = 0; a <= MAX_PLAYERS; a++)
        world.pl[a].allied[id] = world.pl[a].pending[id] = false;
    world.pl[id] = Profile{};
    world.bp[id] = BaseProf{};
    return id;
}

void NetSession::initNewProfile(int id, uint64_t steamId)
{
    world.pl[id].used = true;
    world.pl[id].steamId = steamId;
    world.resetProfile(id);

    // A configurable share of the factions is aggressive towards each newcomer.
    for (int f = 0; f < FACTION_COUNT; f++)
        if (hash2(int((steamId ^ (steamId >> 32)) & 0x7fffffff), f, world.seed + 71) % 100 <
            uint32_t(cfg().hostileFactionPercent))
            world.pl[id].rep[f] = -50.f;

    // Test hook.
    if (const char *ag = std::getenv("BW_AGGRO"))
    {
        int v = std::atoi(ag);
        for (int f = 0; f < FACTION_COUNT; f++)
            if (v >= FACTION_COUNT || v == f)
                world.pl[id].rep[f] = -50.f;
    }
}

void NetSession::respawnIfDestroyed(int id, const std::vector<uint64_t> &)
{
    bool hasTH = false;
    reg.view<Info, Owner>().each([&](const Info &i, const Owner &o)
    {
        if (i.kind == K_TownHall && o.id == id)
            hasTH = true;
    });
    if (hasTH || world.bp[id].homeless || world.bp[id].defeat.phase != DF_None)
        return;

    s.defeat.beginWithoutRuin(id); // no town and no defeat state: old data, same resettle flow
}

void NetSession::sendWelcome(uint64_t conn, int id)
{
    const Profile &p = world.pl[id];
    Writer w;
    w.put<uint8_t>(S_Welcome)
        .put<uint16_t>(PROTOCOL_VERSION)
        .put<uint8_t>(uint8_t(id))
        .put<uint32_t>(world.seed)
        .put<float>(p.cam.x)
        .put<float>(p.cam.y);
    w.str(p.name); // token, see NameToken.hpp
    putRules(w);
    world.net.send(conn, w);
}

void NetSession::onDisconnect(uint64_t conn)
{
    pending.erase(conn);
    world.net.endAuth(conn);
    auto it = world.connPlayer.find(conn);
    int id = it == world.connPlayer.end() ? 0 : it->second;
    world.connPlayer.erase(conn);
    if (id < 1 || id > MAX_PLAYERS || world.pl[id].conn != conn)
        return;
    world.pl[id].online = false;
    world.pl[id].conn = 0;
    world.pl[id].lastSeen = nowSec();
    std::printf("%llu (slot %d) left\n", static_cast<unsigned long long>(world.pl[id].steamId), id);
    s.repl.sendRoster();
}
