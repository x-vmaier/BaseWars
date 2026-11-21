/**
 * @file Raid.cpp
 * @brief Selftest: a forced raid arrives, and the raiding party always expires or dies (no stuck state).
 */
#include "server/SelfTests.hpp"

namespace selftest
{

namespace
{
int raiders(World &w)
{
    int n = 0;
    w.reg.view<Raider>().each([&](const Raider &) { n++; });
    return n;
}
} // namespace

void raidTest(Ctx &t)
{
    Fixture f;
    cfg().pacingFirstRaidGraceMin = cfg().pacingCalmMin = .05; // the director sends the raid within seconds ...
    cfg().pacingWarningS = 2;
    cfg().pacingAftermathMin = 1e4; // ... and exactly one
    int id = f.join(1);
    World &w = f.world;
    for (int fac = 0; fac < FACTION_COUNT; fac++)
        w.pl[id].rep[fac] = -50.f;

    f.run(10);
    int spawned = raiders(w);
    t.log(std::to_string(spawned) + " raiders spawned, " + std::to_string(w.towns.size()) + " NPC towns");
    t.check(spawned > 0, "a hostile faction sends a raiding party");
    t.invariants(f, "raid spawned");

    for (int s = 0; s < 70; s++) // 700 s: longer than the raid lifetime (600 s)
    {
        f.run(10);
        if (s % 10 == 0)
            t.invariants(f, "during raid");
    }
    t.check(raiders(w) == 0, "raiders are gone after their time limit (stuck-state exit)");
    t.invariants(f, "after raid");
    bool alive = f.hallOf(id) != entt::null;
    t.log(alive ? "Town Hall survived" : "Town Hall was destroyed");
    if (!alive)
        t.check(w.pl[id].used, "defeated player's profile is kept");
}

} // namespace selftest
