/**
 * @file FactionSystem.cpp
 * @brief NPC faction reputation, town self-defense, garrison AI and help for friendly players, donations and town
 * demand.
 */
#include "systems/FactionSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void FactionSystem::addRep(int player, int f, float d)
{
    if (!isPlayer(player))
        return;
    float &r = world.pl[player].rep[f];
    float old = r;
    r = std::clamp(r + d, -100.f, 100.f);

    if (old > float(HOSTILE_REP) && r <= float(HOSTILE_REP))
        world.say(player, std::string(FACTION_NAME[f]) + " now considers you hostile!");
    if (old < float(FRIENDLY_REP) && r >= float(FRIENDLY_REP))
        world.say(player, std::string(FACTION_NAME[f]) + " now counts you as a friend and will come to your aid.");
    if (old <= float(HOSTILE_REP) && r > float(HOSTILE_REP))
        world.say(player, std::string(FACTION_NAME[f]) + " no longer wants you dead.");
}

bool FactionSystem::donationReady(int id)
{
    double left = world.pl[id].donateReady - world.clock;
    if (left <= 0)
        return true;
    world.say(id, "You can donate or send resources again in " + std::to_string(int(std::ceil(left))) + " s.");
    return false;
}

void FactionSystem::donated(int id)
{
    world.pl[id].donateReady = world.clock + double(cfg().donationCooldown);
}

float FactionSystem::giftMul(int id, int f, int res) const
{
    float m = world.demand[f].res == res ? cfg().demandBonus : 1.f;
    return m / (1.f + world.pl[id].giftRecent[f][res] / cfg().donationDiminish);
}

void FactionSystem::donate(int id, int f, int res, float amount)
{
    Profile &p = world.pl[id];
    if (p.res[res] + 1e-3f < amount)
    {
        world.say(id, "Not enough resources.");
        return;
    }
    if (!donationReady(id))
        return;

    static const float worth[R_Count] = {1.f, 2.f, 1.5f, 1.f};
    float mul = giftMul(id, f, res);
    bool wanted = world.demand[f].res == res;

    p.res[res] -= amount;
    p.giftRecent[f][res] += amount;
    world.facDonated[f] += amount * worth[res];
    addRep(id, f, amount * worth[res] * .08f * mul);
    donated(id);

    std::string msg = std::string("You gave ") + std::to_string(int(amount)) + " " + RES_NAME[res] + " to the " +
                      FACTION_NAME[f] + (wanted ? " (just what they wanted!)" : "") + ". They now see you as " +
                      repTier(int(std::lround(p.rep[f]))) + ".";
    if (mul < .75f * (wanted ? cfg().demandBonus : 1.f))
        msg += " They have plenty of " + std::string(RES_NAME[res]) + " from you lately: gifts count less.";
    world.say(id, msg);

    if (wanted && (world.demand[f].filled += amount) >= cfg().demandSatisfy)
        world.demand[f].until = 0; // satisfied: the demand moves on
}

void FactionSystem::demandSecond()
{
    fadeGifts();
    rotateDemands();
    sendEcon();
}

void FactionSystem::fadeGifts()
{
    float fade = std::exp(-1.f / cfg().donationMemory);
    for (int id = 1; id <= MAX_PLAYERS; id++)
        if (world.pl[id].used)
            for (auto &row : world.pl[id].giftRecent)
                for (float &g : row)
                    g *= fade;
}

void FactionSystem::rotateDemands()
{
    for (int f = 0; f < FACTION_COUNT; f++)
    {
        Demand &d = world.demand[f];
        if (world.clock < d.until)
            continue;

        bool first = d.until == 0 && d.filled == 0;
        uint8_t old = d.res;
        d.res = uint8_t(world.rng() % R_Count);
        if (!first && d.res == old)
            d.res = uint8_t((d.res + 1 + world.rng() % (R_Count - 1)) % R_Count);
        d.filled = 0;

        float span = cfg().demandChangeMax - cfg().demandChangeMin;
        d.until =
            world.clock + double(cfg().demandChangeMin + (span > 0 ? float(world.rng() % 1000) / 1000.f * span : 0.f));
    }
}

// S_Econ: f32 donation cooldown left, then per faction: u8 wanted resource, u16 seconds until it changes, u8 gift
// multiplier % per resource.
void FactionSystem::sendEcon()
{
    for (int id = 1; id <= MAX_PLAYERS; id++)
    {
        if (!world.pl[id].online)
            continue;

        Writer w;
        w.put<uint8_t>(S_Econ).put<float>(float(std::max(0.0, world.pl[id].donateReady - world.clock)));
        for (int f = 0; f < FACTION_COUNT; f++)
        {
            w.put<uint8_t>(world.demand[f].res)
                .put<uint16_t>(uint16_t(std::clamp(world.demand[f].until - world.clock, 0.0, 65000.0)));
            for (int r = 0; r < R_Count; r++)
                w.put<uint8_t>(uint8_t(std::clamp(giftMul(id, f, r) * 100.f, 0.f, 255.f)));
        }
        world.net.send(world.pl[id].conn, w);
    }
}

void FactionSystem::alertTown(entt::entity victim, uint32_t by)
{
    if (!by)
        return;
    auto *tt = reg.try_get<TownTag>(victim);
    Town *t = tt ? world.townById(tt->town) : nullptr;
    auto atk = world.find(by);
    if (!t || atk == entt::null || world.clock - t->lastAlert < 3.0)
        return;
    t->lastAlert = world.clock;

    bool bld = isBuilding(reg.get<Info>(atk).kind);
    Pos ap = world.posOf(atk);

    std::vector<entt::entity> us;
    reg.view<TownTag, Unit, Info>().each([&](entt::entity e, const TownTag &g, const Unit &, const Info &i)
    {
        if (g.town == t->id && isArmy(i.kind))
            us.push_back(e);
    });

    for (auto e : us)
    {
        if (!world.order(e, ap, bld ? by : 0))
            continue;
        reg.emplace_or_replace<Attack>(e, Attack{by});
    }
}

void FactionSystem::factionAI(entt::entity e, Unit &u, Pos &p, float)
{
    auto *hm = reg.try_get<Home>(e);
    if (!hm)
        return;

    bool busy = reg.all_of<Attack>(e) || reg.all_of<AMove>(e);
    float dh = std::hypot(p.x - hm->x, p.y - hm->y);
    if (world.clock < hm->helpUntil)
        return;

    if (busy && dh > 24.f)
    {
        reg.remove<Attack>(e);
        reg.remove<AMove>(e);
        world.order(e, {hm->x, hm->y});
    }
    else if (!busy && u.path.empty() && dh > 5.f)
        world.order(e, {hm->x, hm->y});
}

void FactionSystem::callHelp()
{
    struct Req
    {
        int victim;
        uint32_t by;
        Pos at;
    };
    std::vector<Req> reqs;
    reg.view<Hit, Owner>().each([&](entt::entity e, const Hit &h, const Owner &o)
    {
        if (!isPlayer(o.id) || world.clock - h.t > 3.0 || reqs.size() >= 6)
            return;
        reqs.push_back({o.id, h.by, world.posOf(e)});
    });

    for (auto &rq : reqs)
    {
        auto atk = world.find(rq.by);
        if (atk == entt::null)
            continue;

        int aow = reg.get<Owner>(atk).id;
        if (!world.hostile(rq.victim, aow))
            continue;

        bool bld = isBuilding(reg.get<Info>(atk).kind);
        Pos ap = world.posOf(atk);
        for (int f = 0; f < FACTION_COUNT; f++)
        {
            if (world.pl[rq.victim].rep[f] < float(FRIENDLY_REP) || aow == FACTION_BASE + f ||
                world.clock < world.pl[rq.victim].helpCool[f])
                continue;
            sendAid(rq.victim, f, rq.by, rq.at, ap, bld);
        }
    }
}

// Sends the faction's nearest idle garrison troops (within 160 tiles of the victim) after the attacker.
void FactionSystem::sendAid(int victim, int f, uint32_t by, Pos victimAt, Pos attackerAt, bool attackerIsBuilding)
{
    std::vector<std::pair<float, entt::entity>> cand;
    reg.view<Owner, Unit, Info, Home>().each(
        [&](entt::entity e, const Owner &o, const Unit &, const Info &i, const Home &)
    {
        if (o.id != FACTION_BASE + f || !isArmy(i.kind) || reg.all_of<Attack>(e))
            return;
        Pos q = reg.get<Pos>(e);
        cand.push_back({std::hypot(q.x - victimAt.x, q.y - victimAt.y), e});
    });
    std::sort(cand.begin(), cand.end(), [](auto &a, auto &b) { return a.first < b.first; });

    int want = std::min(8, 2 + int(world.pl[victim].rep[f] / 15.f)), sent = 0;
    for (auto &[d, e] : cand)
    {
        if (sent >= want || d > 160.f)
            break;
        if (!world.order(e, attackerAt, attackerIsBuilding ? by : 0))
            continue;

        reg.emplace_or_replace<Attack>(e, Attack{by});
        reg.get<Home>(e).helpUntil = float(world.clock + 45);
        sent++;
    }

    world.pl[victim].helpCool[f] = world.clock + 15;
    if (sent)
        world.say(victim, std::string(FACTION_NAME[f]) + " sends " + std::to_string(sent) + " troops to your aid!");
}
