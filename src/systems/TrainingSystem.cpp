#include "systems/TrainingSystem.hpp"

#include "common/Progression.hpp"
#include "systems/KindFactories.hpp"
#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static float trainTime(Kind k)
{
    return cfg().instantTrain ? .05f : KINDS[k].trainSeconds;
}

int TrainingSystem::queuedUnits(int id)
{
    int n = 0;
    reg.view<TrainQ, Owner>().each([&](const TrainQ &q, const Owner &o)
    {
        if (o.id == id)
            n += int(q.q.size());
    });
    return n;
}
// Creates the unit that a finished queue entry produces (the kind's onTrained hook may adjust it). Null if there is no
// room.
entt::entity TrainingSystem::spawnTrained(entt::entity be, Kind k)
{
    int id = reg.get<Owner>(be).id;
    Pos at;
    if (!world.spawnSpot(be, hasTag(k, KT_Naval), at))
        return entt::null;

    auto u = world.makeUnit(k, uint8_t(id), at.x, at.y);
    if (auto hook = kindFactory(k).onTrained)
        hook(s, world, u, be, at);
    else
        world.fx(E_Dust, at.x, at.y);

    // trained pawns stay free: they hunt sheep on their own instead of being bound to a building
    return u;
}

void TrainingSystem::enqueueTrain(int id, entt::entity be, Kind k)
{ // C_Train: pay now, produce over time
    if (reg.get<Owner>(be).id != id || !world.complete(be) || !canTrain(reg.get<Info>(be).kind, k))
        return;
    if (world.crippled(be))
    {
        world.say(id, "This building is too badly damaged to train: repair it first.");
        return;
    }
    if (world.inactive(be) || reg.all_of<Moving>(be))
    {
        world.say(id, "This building is not functional.");
        return;
    }
    if (world.thLevel(id) < unitThLevel(k))
    {
        world.say(id, std::string(KINFO[k].name) + "s require Town Hall level " + std::to_string(unitThLevel(k)) + ".");
        return;
    }
    if (reg.get<Info>(be).level < unitUnlockLevel(k))
    {
        world.say(id,
                  std::string(KINFO[k].name) + "s unlock at Archery level " + std::to_string(unitUnlockLevel(k)) + ".");
        return;
    }
    auto &tq = reg.get_or_emplace<TrainQ>(be);
    if (int(tq.q.size()) >= MAX_QUEUE)
    {
        world.say(id, "The training queue is full (" + std::to_string(MAX_QUEUE) + ").");
        return;
    }
    float cost[R_Count];
    unitCost(k, cost);
    if (!world.afford(id, cost))
    {
        world.say(id, cost[R_Meat] > world.pl[id].res[R_Meat] ? "Not enough food: hunt sheep or build a Harbour."
                                                              : "Not enough resources.");
        return;
    }
    if (world.unitCount(id) + queuedUnits(id) >= UNIT_CAP)
    {
        world.say(id, "Unit cap reached.");
        return;
    }
    world.spend(id, cost);
    tq.q.push_back(QItem{k, 0.f, trainTime(k), 0.f});
    if (world.siteBusy(be))
        world.say(id, "Queued: training starts once the building's upgrade / repair is finished.");
}
void TrainingSystem::dequeueTrain(int id, entt::entity be, int idx)
{
    auto *tq = reg.try_get<TrainQ>(be);
    if (!tq || reg.get<Owner>(be).id != id || idx < 0 || idx >= int(tq->q.size()))
        return;
    Kind k = tq->q[size_t(idx)].k;
    float cost[R_Count];
    unitCost(k, cost);
    for (int r = 0; r < R_Count; r++)
        world.pl[id].res[r] += cost[r];
    tq->q.erase(tq->q.begin() + idx);
    world.say(id, std::string("Training of ") + KINFO[k].name + " cancelled and refunded.");
}
void TrainingSystem::trainTick(entt::entity e, float dt)
{
    auto *tq = reg.try_get<TrainQ>(e);
    if (!tq || tq->q.empty())
        return;
    QItem &it = tq->q.front();
    int ow = reg.get<Owner>(e).id;
    if (it.t < it.total)
        it.t += dt * baseTestSpeed();
    if (it.t < it.total)
        return;
    it.wait -= dt;
    if (it.wait > 0)
        return;
    it.wait = 1.f;
    if (world.unitCount(ow) >= UNIT_CAP)
        return; // wait for room
    if (spawnTrained(e, it.k) != entt::null)
    {
        world.say(ow, std::string(KINFO[it.k].name) + " trained.");
        tq->q.erase(tq->q.begin());
    }
}
