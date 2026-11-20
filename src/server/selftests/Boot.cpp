/**
 * @file Boot.cpp
 * @brief Selftests: boot (worldgen + simulated minutes) and the invariant checker itself.
 */
#include "server/SelfTests.hpp"

namespace selftest
{

void bootTest(Ctx &t)
{
    Fixture f;
    int id = f.join("selftest-uuid-0001", "Alice");
    t.check(id == 1, "first player gets slot 1");
    t.check(f.hallOf(id) != entt::null, "new player owns a Town Hall");
    t.check(f.world.unitCount(id) == 3, "new player starts with 2 pawns and a warrior");
    t.invariants(f, "after join");

    f.run(300);
    t.check(f.world.tick == 300u * TICK_HZ, "5 minutes = 6000 ticks");
    t.check(f.hallOf(id) != entt::null, "Town Hall survives the quiet start");
    t.check(f.world.pl[id].online, "player stays online");
    t.invariants(f, "after 5 minutes");
}

// The checker must notice a job that points at a destroyed entity (the "orphan job" the gate relies on).
void invariantsTest(Ctx &t)
{
    Fixture f;
    f.join("selftest-uuid-0001", "Alice");
    t.invariants(f, "clean world");

    auto pawn = f.world.reg.view<Info>().front();
    f.world.reg.emplace<BuildTask>(pawn, BuildTask{987654u});
    t.check(!f.world.checkInvariants().empty(), "orphan BuildTask is reported");
    f.world.reg.remove<BuildTask>(pawn);
    t.check(f.world.checkInvariants().empty(), "removing the orphan clears the report");

    f.world.pl[1].res[R_Wood] = -5.f;
    t.check(!f.world.checkInvariants().empty(), "negative resources are reported");
}

} // namespace selftest
