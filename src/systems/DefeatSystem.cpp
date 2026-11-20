/**
 * @file DefeatSystem.cpp
 * @brief Losing the last Town Hall: burning ruin, defeat banner state, resettlement offers (S_Defeat).
 */
#include "systems/DefeatSystem.hpp"

#include "common/NameToken.hpp"
#include "systems/Systems.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
constexpr int OFFER_REVEAL_RADIUS = 8; // so the client can draw terrain at the offered site
}

void DefeatSystem::defeat(int victim, int killer, IV hall)
{
    DefeatState &d = world.bp[victim].defeat;
    world.removeOwner(victim);
    for (float &r : world.pl[victim].res)
        r = 0;

    d = DefeatState{};
    d.phase = DF_Defeated;
    d.conqueror = (isPlayer(killer) && killer != victim) || isFactionOwner(killer) ? uint8_t(killer) : uint8_t(0);
    d.ruin = footCenter(K_TownHall, hall.x, hall.y);

    auto ruin = world.makeBuilding(K_Ruin, 0, hall.x, hall.y, true);
    reg.emplace<Ruin>(ruin, Ruin{double(nowSec()) + cfg().defeatRuinHours * 3600.0, uint8_t(victim), true});
    world.fx(E_Explosion, d.ruin.x, d.ruin.y);

    world.pl[victim].cam = d.ruin;
    world.reveal(victim, int(d.ruin.x), int(d.ruin.y), OFFER_REVEAL_RADIUS + 4);
    world.say(victim, "Your Town Hall fell! Your town is burning. Click Resettle to found a new one.");
    std::printf("%s was defeated\n", world.pl[victim].name.c_str());
    sendState(victim);
}

void DefeatSystem::beginWithoutRuin(int id)
{
    world.removeOwner(id);
    DefeatState &d = world.bp[id].defeat;
    d = DefeatState{};
    d.phase = DF_Defeated;
    d.ruin = world.pl[id].cam;
    world.say(id, "Your town was destroyed while you were away. Click Resettle to found a new one.");
}

void DefeatSystem::onResettle(int id)
{
    DefeatState &d = world.bp[id].defeat;
    if (d.phase == DF_None)
        return;
    d.tries = d.phase == DF_Offered ? uint8_t(std::min(250, d.tries + 1)) : uint8_t(0);
    makeOffer(id);
}

void DefeatSystem::makeOffer(int id)
{
    DefeatState &d = world.bp[id].defeat;
    IV c = s.spawn.offerSite(id, world.bp[id].friends, d.tries);
    d.phase = DF_Offered;
    d.site = {float(c.x), float(c.y)};
    d.offerAt = world.clock;

    world.pl[id].cam = d.site;
    world.reveal(id, c.x, c.y, OFFER_REVEAL_RADIUS);
    sendState(id);
}

void DefeatSystem::onAccept(int id)
{
    if (world.bp[id].defeat.phase == DF_Offered)
        accept(id);
}

void DefeatSystem::accept(int id)
{
    DefeatState &d = world.bp[id].defeat;
    IV c{int(d.site.x), int(d.site.y)};

    // Someone may have built on the offered site meanwhile: offer the next one instead.
    for (int y = c.y - 1; y <= c.y + 1; y++)
        for (int x = c.x - 2; x <= c.x + 1; x++)
            if (world.occ.count(tkey(x, y)) || !isLand(world.biome(x, y)))
            {
                d.tries = uint8_t(std::min(250, d.tries + 1));
                makeOffer(id);
                return;
            }

    world.resetProfile(id);
    s.spawn.foundBase(id, c);
    d = DefeatState{};
    world.say(id, "You resettled. Welcome to your new town!");
    sendState(id);
    s.repl.sendRoster();
}

void DefeatSystem::onLogin(int id)
{
    DefeatState &d = world.bp[id].defeat;
    if (d.phase == DF_Offered)
        d.offerAt = world.clock; // the timeout only counts while the player is online
    if (d.phase != DF_None)
        sendState(id);
}

void DefeatSystem::expireRuins(double now)
{
    std::vector<entt::entity> gone;
    reg.view<Ruin>().each([&](entt::entity e, const Ruin &r)
    {
        if (r.expiresAt <= now)
            gone.push_back(e);
    });
    for (auto e : gone)
    {
        Pos p = world.posOf(e);
        world.fx(E_Dust, p.x, p.y);
        world.destroyNow(e);
    }
}

void DefeatSystem::second()
{
    double now = double(nowSec());
    expireRuins(now);
    testDefeat();

    for (int id = 1; id <= MAX_PLAYERS; id++)
    {
        DefeatState &d = world.bp[id].defeat;
        if (!world.pl[id].used || d.phase == DF_None || !world.pl[id].online)
            continue;

        if (d.phase == DF_Offered && world.clock - d.offerAt >= cfg().resettleOfferTimeoutS)
            accept(id); // stuck-state rule: an unanswered offer is taken
        else if (d.phase == DF_Defeated)
        {
            bool ruinAlive = false;
            reg.view<Ruin>().each([&](const Ruin &r) { ruinAlive = ruinAlive || r.victim == id; });
            if (!ruinAlive)
            {
                d.tries = 0;
                makeOffer(id); // the ruin burnt down: the player is offered a site without clicking
            }
        }
        if (d.phase != DF_None)
            sendState(id);
    }
}

// Test hook BW_DEFEAT=<seconds>[,offer]: once the world clock passes it, every online player with a Town Hall is
// defeated (once); ",offer" also asks for the first resettlement offer.
void DefeatSystem::testDefeat()
{
    static const char *hook = std::getenv("BW_DEFEAT");
    if (!hook || world.clock < std::atof(hook))
        return;
    for (int id = 1; id <= MAX_PLAYERS; id++)
    {
        auto th = world.townHallOf(id);
        if (!world.pl[id].online || th == entt::null || world.bp[id].defeat.phase != DF_None)
            continue;
        const Building &b = reg.get<Building>(th);
        defeat(id, 0, {b.tx, b.ty});
        if (std::strstr(hook, ",offer"))
            onResettle(id);
    }
}

void DefeatSystem::sendState(int id)
{
    const Profile &p = world.pl[id];
    if (!p.online)
        return;
    const DefeatState &d = world.bp[id].defeat;
    Writer w;
    w.put<uint8_t>(S_Defeat).put<uint8_t>(d.phase);
    if (d.phase != DF_None)
    {
        std::string who, clan;
        if (isFactionOwner(d.conqueror))
            who = FACTION_NAME[d.conqueror - FACTION_BASE];
        else if (isPlayer(d.conqueror) && world.pl[d.conqueror].used)
        {
            who = nameToken(world.pl[d.conqueror].steamId);
            if (uint8_t c = world.pl[d.conqueror].clan)
                clan = world.clans[c].name;
        }
        double left =
            d.phase == DF_Offered ? std::max(0.0, cfg().resettleOfferTimeoutS - (world.clock - d.offerAt)) : 0.0;
        w.str(who);
        w.str(clan);
        w.put<float>(d.ruin.x).put<float>(d.ruin.y).put<float>(d.site.x).put<float>(d.site.y);
        w.put<uint16_t>(uint16_t(std::min(65000.0, left)));
    }
    world.net.send(p.conn, w);
}
