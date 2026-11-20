/**
 * @file Economy.cpp
 * @brief Selftest: building, producing and training change resources by the documented amounts.
 */
#include "common/Progression.hpp"
#include "server/SelfTests.hpp"

#include <algorithm>
#include <cmath>

namespace selftest
{

void economyTest(Ctx &t)
{
    Fixture f;
    cfg().instantBuild = true;
    cfg().instantTrain = true;
    int id = f.join("selftest-uuid-0001", "Alice", 0, 1000);
    World &w = f.world;

    int tx = 0, ty = 0;
    bool site = f.findSite(id, K_Lumber, tx, ty);
    t.check(site, "a Lumber Camp site exists near the Town Hall");
    if (!site)
        return;

    float cost[R_Count];
    buildCost(K_Lumber, cost);
    float start[R_Count];
    std::copy(w.pl[id].res, w.pl[id].res + R_Count, start);

    Writer b;
    b.put<uint8_t>(C_Build).put<uint8_t>(K_Lumber).put<int32_t>(tx).put<int32_t>(ty);
    f.send(1000, b);
    for (int r = 0; r < R_Count; r++)
        t.check(std::abs(w.pl[id].res[r] - (start[r] - cost[r])) < .01f,
                "build cost paid for resource " + std::to_string(r));
    t.check(w.countKind(id, K_Lumber) == 1, "construction site exists");

    f.run(10);
    auto e = w.find(w.occ[tkey(tx, ty)]);
    t.check(e != entt::null && w.complete(e), "instant build completes the camp");

    float woodAfterBuild = w.pl[id].res[R_Wood];
    f.run(180);
    t.check(w.pl[id].res[R_Wood] > woodAfterBuild + 1.f, "Lumber Camp produces wood over 3 minutes");
    t.invariants(f, "after production");

    // Training: pay now, unit appears.
    float tcost[R_Count];
    unitCost(K_Pawn, tcost);
    float before[R_Count];
    std::copy(w.pl[id].res, w.pl[id].res + R_Count, before);
    int units = w.unitCount(id);
    Writer train;
    train.put<uint8_t>(C_Train).put<uint32_t>(w.reg.get<Uid>(f.hallOf(id)).id).put<uint8_t>(K_Pawn);
    f.send(1000, train);
    t.check(std::abs(before[R_Meat] - w.pl[id].res[R_Meat] - tcost[R_Meat]) < .01f, "training cost paid in meat");
    f.run(5);
    t.check(w.unitCount(id) == units + 1, "trained pawn appears");

    // Unhappy path: no money, no unit.
    for (float &v : w.pl[id].res)
        v = 0;
    f.send(1000, train);
    f.run(5);
    t.check(w.unitCount(id) == units + 1, "cannot train without resources");
    t.invariants(f, "end");
}

} // namespace selftest
