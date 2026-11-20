/**
 * @file Defeat.cpp
 * @brief Selftest: defeat leaves a burning ruin, resettlement offers (friend > clan > other), blocked commands, accept,
 * and the guaranteed exits (offer timeout, ruin expiry).
 */
#include "server/SelfTests.hpp"

#include <cmath>
#include <filesystem>

namespace selftest
{

namespace
{
double dist(Pos a, Pos b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

void send(Fixture &f, uint64_t conn, uint8_t type)
{
    Writer w;
    w.put<uint8_t>(type);
    f.send(conn, w);
}

// Destroys the Town Hall of `victim` as if `killer` had done it.
void forceDefeat(Fixture &f, int victim, int killer)
{
    auto th = f.hallOf(victim);
    f.world.toKill.push_back({th, killer});
    f.world.dying.insert(f.world.reg.get<Uid>(th).id);
    f.sys.combat.processKills();
}

entt::entity ruinOf(Fixture &f, int victim)
{
    entt::entity found = entt::null;
    f.world.reg.view<Ruin>().each([&](entt::entity e, const Ruin &r)
    {
        if (r.victim == victim)
            found = e;
    });
    return found;
}

// Defeat -> ruin and banner state; the ruin survives save / reload; priority of candidate sites.
void ruinAndOffers(Ctx &t)
{
    Fixture f;
    int a = f.join(1, 1), b = f.join(2, 2), c = f.join(3, 3);
    Pos hallB = f.world.posOf(f.hallOf(b)), hallC = f.world.posOf(f.hallOf(c));
    f.world.bp[a].friends = {f.world.pl[c].steamId}; // c is a Steam friend of a, b is a stranger
    f.run(1);

    Pos hallA = f.world.posOf(f.hallOf(a));
    forceDefeat(f, a, b);
    DefeatState &d = f.world.bp[a].defeat;
    t.check(f.hallOf(a) == entt::null && d.phase == DF_Defeated, "defeat removes the hall and starts the banner state");
    t.check(d.conqueror == b, "the last attacker is named");
    entt::entity ruin = ruinOf(f, a);
    t.check(ruin != entt::null && f.world.reg.get<Info>(ruin).kind == K_Ruin && f.world.reg.get<Owner>(ruin).id == 0,
            "a neutral burning ruin stands on the old site");
    t.check(dist(f.world.posOf(ruin), hallA) < 1.f && f.world.reg.get<Ruin>(ruin).burning,
            "the ruin is where the hall was");
    t.invariants(f, "after defeat");

    // The ruin and the defeated state persist.
    double expiry = f.world.reg.get<Ruin>(ruin).expiresAt;
    t.check(std::abs(expiry - double(nowSec()) - cfg().defeatRuinHours * 3600.0) < 5.0,
            "ruin expiry is wall-clock hours");
    f.sys.save.saveGame();
    {
        Fixture g;
        std::filesystem::copy_file(f.savePath, g.savePath, std::filesystem::copy_options::overwrite_existing);
        t.check(g.sys.save.loadGame(), "save with a ruin loads");
        entt::entity r2 = ruinOf(g, a);
        t.check(r2 != entt::null && std::abs(g.world.reg.get<Ruin>(r2).expiresAt - expiry) < 1e-6,
                "ruin and its expiry survive reload");
        t.check(g.world.bp[a].defeat.phase == DF_Defeated && g.world.bp[a].defeat.conqueror == b,
                "defeated state survives reload");
        t.invariants(g, "after reload");
    }

    // Commands are rejected while defeated; the first offer is near the friend (priority), not the stranger.
    int before = int(f.world.reg.view<Building>().size());
    {
        Writer w;
        w.put<uint8_t>(C_Build).put<uint8_t>(K_Lumber).put<int32_t>(int(hallA.x)).put<int32_t>(int(hallA.y));
        f.send(1, w);
    }
    t.check(int(f.world.reg.view<Building>().size()) == before, "build commands are rejected while defeated");

    send(f, 1, C_Resettle);
    t.check(d.phase == DF_Offered, "Resettle creates an offer");
    double toFriend = dist(d.site, hallC), toStranger = dist(d.site, hallB);
    t.check(toFriend >= SPAWN_MIN_DIST - 1 && toFriend <= SPAWN_RADIUS + 1, "first offer lies near the Steam friend");
    t.check(toFriend < toStranger || toStranger >= SPAWN_MIN_DIST - 1, "friend has priority over a stranger");
    Pos first = d.site;
    send(f, 1, C_Resettle);
    t.check(d.phase == DF_Offered && dist(d.site, hallB) <= SPAWN_RADIUS + 1,
            "Resettle again offers the next candidate");
    t.check(dist(first, d.site) > 1.f, "the next candidate is a different site");

    // Anything but Accept / Resettle is rejected during an offer.
    before = int(f.world.reg.view<Building>().size());
    {
        Writer w;
        w.put<uint8_t>(C_Build).put<uint8_t>(K_TownHall).put<int32_t>(int(d.site.x)).put<int32_t>(int(d.site.y));
        f.send(1, w);
    }
    t.check(int(f.world.reg.view<Building>().size()) == before && d.phase == DF_Offered,
            "commands (even a Town Hall build) are rejected while an offer is pending");

    Pos site = d.site;
    send(f, 1, C_ResettleAccept);
    entt::entity th = f.hallOf(a);
    t.check(th != entt::null && d.phase == DF_None, "Accept places the Town Hall and ends the state");
    t.check(th != entt::null && dist(f.world.posOf(th), site) < 3.f, "the hall stands at the offered site");
    t.check(f.world.pl[a].res[R_Wood] == START_RES[R_Wood], "a fresh start gives the starting resources");
    t.check(ruinOf(f, a) != entt::null, "the old ruin keeps burning after resettling");
    f.run(2);
    t.invariants(f, "after accept");
}

// Exits: no other player -> any free site; an unanswered offer is accepted; an expired ruin offers a site by itself.
void exits(Ctx &t)
{
    Fixture f;
    cfg().resettleOfferTimeoutS = 5.0;
    int a = f.join(1, 1);
    forceDefeat(f, a, 0);
    DefeatState &d = f.world.bp[a].defeat;
    t.check(d.phase == DF_Defeated && d.conqueror == 0, "an unknown attacker leaves no conqueror");

    send(f, 1, C_Resettle);
    t.check(d.phase == DF_Offered, "a lone player still gets an offer");
    f.run(7);
    t.check(f.hallOf(a) != entt::null && d.phase == DF_None, "an unanswered offer is accepted automatically");

    // Ruin expiry (wall clock) with a player who never clicks: the ruin disappears and an offer follows.
    int b = f.join(2, 2);
    forceDefeat(f, b, a);
    entt::entity ruin = ruinOf(f, b);
    t.check(ruin != entt::null, "second defeat leaves a second ruin");
    f.world.reg.get<Ruin>(ruin).expiresAt = double(nowSec()) - 1.0;
    f.run(2);
    t.check(ruinOf(f, b) == entt::null, "an expired ruin disappears");
    t.check(f.world.bp[b].defeat.phase == DF_Offered || f.hallOf(b) != entt::null, "expiry hands the player an offer");
    f.run(7);
    t.check(f.hallOf(b) != entt::null && f.world.bp[b].defeat.phase == DF_None, "the offer then resolves by timeout");

    // A defeated player who disconnects keeps the state and gets a fresh timeout on return.
    int c = f.join(3, 3);
    forceDefeat(f, c, a);
    send(f, 3, C_Resettle);
    f.leave(3);
    f.run(10);
    t.check(f.world.bp[c].defeat.phase == DF_Offered && f.hallOf(c) == entt::null,
            "an offline offer is not auto-accepted");
    f.join(3, 4);
    f.run(7);
    t.check(f.hallOf(c) != entt::null, "after rejoining the offer resolves by timeout");
    t.invariants(f, "after exits");
}
} // namespace

void defeatTest(Ctx &t)
{
    ruinAndOffers(t);
    exits(t);
}

} // namespace selftest
