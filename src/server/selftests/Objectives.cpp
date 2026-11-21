/**
 * @file Objectives.cpp
 * @brief Selftest: objective chains advance on events and world state, steps the player already satisfied resolve at
 * once, rewards are granted exactly once, a stuck objective times out, the offline owner's clock pauses and the state
 * survives save / reload.
 */
#include "server/SelfTests.hpp"

#include <filesystem>
#include <numeric>

namespace selftest
{

namespace
{
float totalRes(const World &w, int id)
{
    return std::accumulate(w.pl[id].res, w.pl[id].res + R_Count, 0.f);
}

void build(Fixture &f, Kind k)
{
    int tx = 0, ty = 0;
    if (f.findSite(1, k, tx, ty))
    {
        Writer b;
        b.put<uint8_t>(C_Build).put<uint8_t>(k).put<int32_t>(tx).put<int32_t>(ty);
        f.send(1000, b);
    }
}

// The short chain: build -> events -> already built steps are skipped in one pass, rewards come once.
void chain(Ctx &t)
{
    Fixture f;
    cfg().instantBuild = cfg().instantTrain = true;
    int id = f.join(1, 1000);
    World &w = f.world;
    const ObjState &o = w.bp[id].obj;
    t.check(o.cur[OT_Short] == OBJ_Lumber && o.cur[OT_Medium] == OBJ_Raid1 && o.cur[OT_Long] == OBJ_Hall5,
            "a new player starts every chain at its first objective");

    build(f, K_Mine); // satisfied before its turn
    f.run(2);
    t.check(o.cur[OT_Short] == OBJ_Lumber, "the chain waits for the Lumber Camp");

    build(f, K_Lumber);
    f.run(2);
    t.check(o.cur[OT_Short] == OBJ_Wood && o.completed == 1, "the Lumber Camp completes its objective");

    postObjective(w.reg, id, OE_Gather, R_Wood, 40.f); // 40 wood delivered, through the bus
    float before = totalRes(w, id);
    f.run(1);
    t.check(o.cur[OT_Short] == OBJ_Pawn && o.completed == 3, "Gather + the already built Mine resolve in one pass");
    t.check(totalRes(w, id) - before >= OBJECTIVES[OBJ_Wood].reward[R_Wood] + OBJECTIVES[OBJ_Mine].reward[R_Gold],
            "their rewards were paid");

    Writer tr;
    tr.put<uint8_t>(C_Train).put<uint32_t>(w.reg.get<Uid>(f.hallOf(id)).id).put<uint8_t>(K_Pawn);
    f.send(1000, tr);
    f.run(5);
    t.check(o.trained[K_Pawn] == 1 && o.cur[OT_Short] == OBJ_Campfire, "training a Pawn advances the chain");

    int done = o.completed;
    f.run(5);
    t.check(o.completed == done, "an objective is rewarded once");

    w.bp[id].pacing.survived = 1; // medium chain reads the live state
    w.reg.get<Info>(f.hallOf(id)).level = 5;
    f.run(2);
    t.check(o.cur[OT_Medium] == OBJ_Donate && o.cur[OT_Long] == OBJ_Hall10 && o.cur[OT_Short] == OBJ_Campfire,
            "survived raids and Town Hall level advance the medium and long chains");
    t.invariants(f, "objective chain");
}

// No objective can block forever; an offline owner's clock does not run.
void exits(Ctx &t)
{
    Fixture f;
    cfg().objectiveTimeoutMin = .05; // 3 s
    int id = f.join(1, 1000);
    const ObjState &o = f.world.bp[id].obj;
    f.run(1);
    f.leave(1000);
    f.run(10);
    t.check(o.cur[OT_Short] == OBJ_Lumber && o.active[OT_Short] < 3.f, "offline: the objective clock is paused");
    f.join(1, 1000);
    f.run(60);
    t.check(std::all_of(o.cur, o.cur + OT_Count, [](uint8_t c) { return c == OBJ_None; }),
            "every unreachable objective resolves by timeout");
    t.check(o.completed == OBJ_Count && o.lastRewarded == 0, "skipped goals give no reward");
    t.invariants(f, "objective timeout");
}

void persistence(Ctx &t)
{
    std::string copy;
    {
        Fixture a;
        int id = a.join(1, 1000);
        postObjective(a.world.reg, id, OE_Gather, R_Wood, 25.f);
        postObjective(a.world.reg, id, OE_Donate);
        a.run(3);
        a.world.bp[id].obj.cur[OT_Short] = OBJ_Pawn;
        a.sys.save.saveGame();
        copy = a.savePath + ".copy";
        std::filesystem::copy_file(a.savePath, copy, std::filesystem::copy_options::overwrite_existing);
    }
    Fixture b;
    std::filesystem::copy_file(copy, b.savePath, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(copy);
    t.check(b.sys.save.loadGame(), "saved world loads");
    const ObjState &o = b.world.bp[1].obj;
    t.check(o.cur[OT_Short] == OBJ_Pawn && o.gathered[R_Wood] == 25 && o.donations == 1 && o.active[OT_Short] > 1.f,
            "objectives and counters survive the reload");
    t.invariants(b, "objectives after reload");
}
} // namespace

void objectivesTest(Ctx &t)
{
    chain(t);
    exits(t);
    persistence(t);
}

} // namespace selftest
