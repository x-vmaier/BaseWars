/**
 * @file Identity.cpp
 * @brief Selftest: towns are owned by the authenticated SteamID only; forged or unanswered logins never create state.
 */
#include "common/NameToken.hpp"
#include "server/SelfTests.hpp"

#include <filesystem>
#include <string>

namespace selftest
{

namespace
{
using Ticket = std::vector<uint8_t>;

Ticket ticketOf(const std::string &s)
{
    return Ticket(s.begin(), s.end());
}

int usedSlots(World &w)
{
    int n = 0;
    for (int i = 1; i <= MAX_PLAYERS; i++)
        n += w.pl[i].used ? 1 : 0;
    return n;
}

// Two accounts get separate towns; a reconnect (also from "another machine") loads exactly that town, also after a
// restart.
void separateTowns(Ctx &t)
{
    constexpr uint64_t A = 101, B = 202;
    std::string copy;
    Pos hallA{}, hallB{};
    {
        Fixture f;
        int a = f.join(A, 1), b = f.join(B, 2);
        t.check(a >= 1 && b >= 1 && a != b, "two ids get two slots");
        t.check(f.hallOf(a) != entt::null && f.hallOf(b) != entt::null && f.hallOf(a) != f.hallOf(b),
                "each id has its own Town Hall");
        hallA = f.world.posOf(f.hallOf(a));
        hallB = f.world.posOf(f.hallOf(b));
        t.check(f.world.pl[a].steamId == A && f.world.pl[b].steamId == B, "slots are keyed by the SteamID");
        t.check(f.world.pl[a].name == nameToken(A), "the server holds a name token, not a name");

        f.run(20);
        f.leave(1);
        t.check(!f.world.pl[a].online, "leaving marks the profile offline");
        t.check(f.join(A, 3) == a, "same id on another connection gets its own town back");
        t.check(f.world.pl[a].conn == 3 && f.world.connPlayer.count(1) == 0, "old connection is unbound");

        // Same account logging in while still online replaces the old session instead of creating a second town.
        int again = f.join(A, 4);
        t.check(again == a && f.world.pl[a].conn == 4 && f.world.connPlayer.count(3) == 0,
                "a second login of an online id takes over the same town");
        t.check(usedSlots(f.world) == 2, "no extra profile was created");
        f.run(5);
        t.invariants(f, "after reconnects");
        f.sys.save.saveGame();
        copy = f.savePath + ".copy";
        std::filesystem::copy_file(f.savePath, copy, std::filesystem::copy_options::overwrite_existing);
    }

    Fixture g;
    std::filesystem::copy_file(copy, g.savePath, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(copy);
    t.check(g.sys.save.loadGame(), "save with SteamID profiles loads");
    int b2 = g.join(B, 9);
    t.check(b2 == 2, "id B gets slot 2 back after a restart");
    Pos nb = g.world.posOf(g.hallOf(b2));
    t.check(nb.x == hallB.x && nb.y == hallB.y && (hallA.x != hallB.x || hallA.y != hallB.y),
            "id B owns its original Town Hall");
    t.check(g.join(A, 10) == 1, "id A gets slot 1 back after a restart");
    t.invariants(g, "after reload");
}

// A forged id (no or wrong proof) is dropped without creating a profile or binding the connection.
void forgedLogins(Ctx &t)
{
    Fixture f;
    f.join(1, 1);
    int before = usedSlots(f.world);
    t.check(f.joinWithTicket(2, {}, 50) == 0, "no ticket is rejected");
    t.check(f.joinWithTicket(2, ticketOf("hello"), 51) == 0, "a wrong ticket is rejected");
    t.check(f.joinWithTicket(0, ticketOf(DEV_TICKET), 52) == 0, "id 0 is rejected");
    t.check(f.joinWithTicket(76561198000000001ull, ticketOf(DEV_TICKET), 53) == 0,
            "the dev ticket never proves a real-looking SteamID");
    t.check(usedSlots(f.world) == before, "rejected logins create no profile");

    // A verifier that checks the proof against the claimed id (like Steam does).
    f.world.net.setTicketVerifier([](uint64_t id, const Ticket &tk)
    {
        return std::string(tk.begin(), tk.end()) == "sig:" + std::to_string(id) ? AuthResult::Accepted
                                                                                : AuthResult::Rejected;
    });
    t.check(f.joinWithTicket(7, ticketOf("sig:7"), 60) != 0, "a ticket matching the claimed id is accepted");
    int slots = usedSlots(f.world);
    t.check(f.joinWithTicket(8, ticketOf("sig:7"), 61) == 0, "a valid ticket of another id cannot be replayed");
    t.check(usedSlots(f.world) == slots, "the replay created no profile");
    t.invariants(f, "after forged logins");
}

// Verdict arrives later: accepted, rejected, or never (timeout). Every path leaves no pending hello behind.
void asyncVerdicts(Ctx &t)
{
    Fixture f;
    f.world.net.setTicketVerifier([](uint64_t, const Ticket &) { return AuthResult::Pending; });
    int none = usedSlots(f.world);

    t.check(f.joinWithTicket(31, ticketOf("x"), 70) == 0 && f.sys.session.pendingCount() == 1, "hello waits for Steam");
    t.check(usedSlots(f.world) == none, "nothing is created before the verdict");
    {
        Writer w; // a second hello on the same connection is ignored
        w.put<uint8_t>(C_Hello).put<uint64_t>(31).put<uint16_t>(1).put<uint8_t>('x').put<uint16_t>(0);
        f.send(70, w);
    }
    t.check(f.sys.session.pendingCount() == 1, "one pending hello per connection");
    f.sys.session.onAuthResult(70, true);
    t.check(f.world.connPlayer.count(70) == 1 && f.sys.session.pendingCount() == 0, "positive verdict logs in");

    f.joinWithTicket(32, ticketOf("x"), 71);
    f.sys.session.onAuthResult(71, false);
    t.check(f.world.connPlayer.count(71) == 0 && f.sys.session.pendingCount() == 0, "negative verdict drops the hello");

    f.joinWithTicket(33, ticketOf("x"), 72);
    f.run(cfg().authTimeoutS + 3);
    t.check(f.sys.session.pendingCount() == 0 && f.world.connPlayer.count(72) == 0, "unanswered hello times out");
    f.sys.session.onAuthResult(72, true); // a late verdict after the timeout is harmless
    t.check(f.world.connPlayer.count(72) == 0, "late verdict is ignored");

    f.joinWithTicket(34, ticketOf("x"), 73);
    f.leave(73);
    t.check(f.sys.session.pendingCount() == 0, "disconnect clears a pending hello");
    t.invariants(f, "after async verdicts");
}
} // namespace

void identityTest(Ctx &t)
{
    separateTowns(t);
    forgedLogins(t);
    asyncVerdicts(t);
}

} // namespace selftest
