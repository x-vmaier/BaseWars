/**
 * @file ObjectiveSystem.cpp
 * @brief Objectives: event bus -> lifetime counters, chain advance (skipped steps resolve at once), one-time rewards,
 * timeout exit and the S_Objectives message.
 */
#include "systems/ObjectiveSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <string>

ObjectiveSystem::ObjectiveSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s)
{
    reg.ctx().emplace<ObjectiveBus>();
}

void ObjectiveSystem::drain()
{
    auto &bus = reg.ctx().get<ObjectiveBus>();
    for (const ObjEvent &e : bus.q)
    {
        if (!isPlayer(e.player))
            continue;
        ObjState &o = world.bp[e.player].obj;
        switch (e.type)
        {
        case OE_Gather:
            if (e.arg >= 0 && e.arg < R_Count)
                o.gathered[e.arg] += uint32_t(std::max(0.f, e.amount));
            break;
        case OE_Trained:
            if (e.arg >= 0 && e.arg < K_Count)
                o.trained[e.arg] = uint16_t(std::min(65535, o.trained[e.arg] + 1));
            break;
        case OE_Donate:
            o.donations = uint16_t(std::min(65535, o.donations + 1));
            break;
        default: // built / level / raid are read from the live state in evaluate()
            break;
        }
    }
    bus.q.clear();
}

int ObjectiveSystem::buildingsOf(int id, int kind) const
{
    int n = 0;
    reg.view<Info, Owner, Building>().each([&](const Info &i, const Owner &o, const Building &b)
    {
        if (o.id == id && int(i.kind) == kind && b.progress >= 1.f)
            n++;
    });
    return n;
}

int ObjectiveSystem::progressOf(int id, const ObjDef &d) const
{
    const ObjState &o = world.bp[id].obj;
    switch (d.cond)
    {
    case OC_Gather:
        return int(std::min<uint32_t>(o.gathered[d.arg], 100000));
    case OC_Build:
        return buildingsOf(id, d.arg);
    case OC_Train:
    {
        if (d.arg >= 0)
            return o.trained[d.arg];
        int n = 0;
        for (int k = 0; k < K_Count; k++)
            if (hasTag(Kind(k), KT_Army))
                n += o.trained[k];
        return n;
    }
    case OC_Survive:
        return world.bp[id].pacing.survived;
    case OC_HallLevel:
        return world.thLevel(id);
    case OC_Donate:
        return o.donations;
    }
    return 0;
}

// The objective ends: rewarded when its condition holds, skipped (no reward) when it timed out.
void ObjectiveSystem::finish(int id, int tier, bool rewarded)
{
    ObjState &o = world.bp[id].obj;
    const ObjDef &d = OBJECTIVES[o.cur[tier]];
    o.lastDone = o.cur[tier];
    o.lastRewarded = rewarded ? 1 : 0;
    o.doneSeq++;
    o.cur[tier] = d.next; // advance before paying: the reward can only be granted once
    o.active[tier] = 0;
    o.completed++;

    if (!rewarded)
    {
        world.say(id, std::string("Goal skipped: ") + d.title + ".");
        return;
    }
    std::string txt;
    for (int r = 0; r < R_Count; r++)
        if (d.reward[r] > 0)
        {
            world.pl[id].res[r] += d.reward[r];
            txt += (txt.empty() ? " " : ", ") + std::to_string(int(d.reward[r])) + " " + RES_NAME[r];
        }
    world.say(id, std::string("Goal complete: ") + d.title + "." + (txt.empty() ? "" : " Reward:" + txt));
}

void ObjectiveSystem::evaluate(int id)
{
    ObjState &o = world.bp[id].obj;
    const double limit = cfg().objectiveTimeoutMin * 60.0;
    for (int t = 0; t < OT_Count; t++)
    {
        if (world.pl[id].online && o.cur[t] != OBJ_None)
            o.active[t] += 1.f;
        // a finished objective may unlock one that is already satisfied (skipped steps resolve in one pass)
        for (int guard = 0; guard < OBJ_Count && o.cur[t] != OBJ_None; guard++)
        {
            const ObjDef &d = OBJECTIVES[o.cur[t]];
            bool done = progressOf(id, d) >= d.count;
            if (!done && o.active[t] < limit)
                break;
            finish(id, t, done);
        }
    }
}

void ObjectiveSystem::second()
{
    drain();
    for (int id = 1; id <= MAX_PLAYERS; id++)
        if (world.pl[id].used)
            evaluate(id);
}

void ObjectiveSystem::send(int id)
{
    const ObjState &o = world.bp[id].obj;
    const double limit = cfg().objectiveTimeoutMin * 60.0;
    Writer w;
    w.put<uint8_t>(S_Objectives).put<uint8_t>(o.doneSeq).put<uint8_t>(o.lastDone).put<uint8_t>(o.lastRewarded);
    for (int t = 0; t < OT_Count; t++)
    {
        int cur = o.cur[t], prog = 0;
        double left = 0;
        if (cur != OBJ_None)
        {
            prog = std::min(progressOf(id, OBJECTIVES[cur]), 65535);
            left = std::clamp(limit - double(o.active[t]), 0.0, 65000.0);
        }
        w.put<uint8_t>(uint8_t(cur)).put<uint16_t>(uint16_t(prog)).put<uint16_t>(uint16_t(left));
    }
    world.net.send(world.pl[id].conn, w);
}
