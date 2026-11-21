/**
 * @file PacingSystem.cpp
 * @brief Pacing Director: per-player stress / relief rhythm (Calm -> Warning -> Siege -> Aftermath), raid timing and
 * size.
 */
#include "systems/PacingSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace
{
// Test hooks: BW_RAIDNOW = a raid comes within seconds, BW_PACING=<phase> starts every player in that phase.
bool fastRaids()
{
    static const bool on = std::getenv("BW_RAIDNOW") != nullptr;
    return on;
}
int forcedPhase()
{
    static const int phase = []
    {
        const char *e = std::getenv("BW_PACING");
        if (!e)
            return -1;
        std::string v = e;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        for (int i = 0; i < PH_Count; i++)
        {
            std::string n = pacingName(i);
            std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (n == v)
                return i;
        }
        return -1;
    }();
    return phase;
}
constexpr double SIEGE_MAX = 660.0; // raiders expire after 600 s; this is the director's own exit
constexpr float HEAVY_LOSS = .25f, FLAWLESS_LOSS = .05f;
} // namespace

// Uniform random factor in [lo, hi]: the director never repeats the same timings.
double PacingSystem::rnd(double lo, double hi)
{
    return lo + (hi - lo) * double(world.rng() % 10000) / 9999.0;
}

// Calm length: usually around the configured value, sometimes a short breather, sometimes a long quiet spell.
double PacingSystem::calmSeconds()
{
    if (fastRaids())
        return 6.0;
    double base = cfg().pacingCalmMin * 60.0, roll = rnd(0, 1);
    if (roll < .15)
        return base * rnd(.4, .65);
    if (roll > .85)
        return base * rnd(1.4, 2.2);
    return base * rnd(.75, 1.25);
}

void PacingSystem::enter(Pacing &p, uint8_t phase, double seconds)
{
    p.phase = phase;
    p.left = p.total = std::max(1.0, seconds);
}

int PacingSystem::raidersOf(int id) const
{
    int n = 0;
    reg.view<Raider>().each([&](const Raider &r) { n += r.target == id ? 1 : 0; });
    return n;
}

void PacingSystem::assetsOf(int id, int &level, int &buildings, int &troops)
{
    level = 1;
    buildings = troops = 0;
    reg.view<Info, Owner>().each([&](entt::entity e, const Info &i, const Owner &o)
    {
        if (o.id != id || hasTag(i.kind, KT_Ruin))
            return;
        if (isBuilding(i.kind))
        {
            const Building *b = reg.try_get<Building>(e);
            if (!b || b->progress < 1.f)
                return;
            buildings++;
            if (i.kind == K_TownHall)
                level = i.level;
        }
        else if (hasTag(i.kind, KT_Army))
            troops++;
    });
}

// A hostile faction (reputation) with a town in range; a random one if several qualify.
int PacingSystem::pickFaction(int id, const Town *&from)
{
    auto th = world.townHallOf(id);
    if (th == entt::null)
        return -1;
    Pos tp = world.posOf(th);

    int found[FACTION_COUNT], n = 0;
    const Town *towns[FACTION_COUNT];
    for (int f = 0; f < FACTION_COUNT; f++)
    {
        if (world.pl[id].rep[f] > float(HOSTILE_REP))
            continue; // peace: no raids
        if (const Town *t = s.raid.nearestTown(f, tp))
        {
            towns[n] = t;
            found[n++] = f;
        }
    }
    if (!n)
        return -1;
    int k = int(world.rng() % uint32_t(n));
    from = towns[k];
    return found[k];
}

void PacingSystem::startWarning(int id, Pacing &p, int faction, const Town &from)
{
    int level, buildings, troops;
    assetsOf(id, level, buildings, troops);
    p.faction = uint8_t(faction);
    // every raid is a little stronger or weaker than the plain formula; the caps in raidSize still hold
    p.raiders = uint8_t(raidSize(defenseScore(level, buildings, troops), p.factor * float(rnd(.8, 1.25)),
                                 buildings + troops, cfg().pacingMaxRaiders, cfg().pacingMaxBaseShare));
    p.origin = {float(from.cx), float(from.cy)};
    enter(p, PH_Warning, fastRaids() ? 4.0 : cfg().pacingWarningS * rnd(.8, 1.3));

    world.say(id, std::string("War horns! A raiding party of the ") + FACTION_NAME[faction] + " (" +
                      std::to_string(p.raiders) + " warriors) will arrive in " + std::to_string(int(p.total)) +
                      " seconds. Prepare your defenses!");
}

void PacingSystem::startSiege(int id, Pacing &p)
{
    int level, buildings, troops;
    assetsOf(id, level, buildings, troops);
    if (!s.raid.spawnRaid(id, p.faction, p.raiders))
    {
        enter(p, PH_Calm, calmSeconds()); // town gone or unreachable: no raid, no stuck warning
        return;
    }
    p.assetsBefore = uint16_t(std::min(buildings + troops, 65535));
    enter(p, PH_Siege, SIEGE_MAX);
}

void PacingSystem::endSiege(int id, Pacing &p)
{
    int level, buildings, troops;
    assetsOf(id, level, buildings, troops);
    float loss = p.assetsBefore ? 1.f - float(buildings + troops) / float(p.assetsBefore) : 0.f;
    loss = std::clamp(loss, 0.f, 1.f);

    if (loss > HEAVY_LOSS)
        p.factor *= .7f;
    else if (loss <= FLAWLESS_LOSS)
        p.factor *= 1.1f;
    p.factor = std::clamp(p.factor, .5f, cfg().pacingMaxRaidFactor);
    p.survived++;

    float reward = cfg().pacingRewardPerRaider * float(p.raiders);
    for (int r = 0; r < R_Count; r++)
        world.pl[id].res[r] += reward;
    world.say(id, "The raid is over. Survivor reward: " + std::to_string(int(reward)) +
                      " of every resource. Time to rebuild and rest.");
    enter(p, PH_Aftermath, fastRaids() ? 6.0 : cfg().pacingAftermathMin * 60.0 * rnd(.8, 1.5));
}

// First tick of a player's director: grace period for a new town (or a forced phase for tests).
void PacingSystem::begin(int id, Pacing &p)
{
    double grace =
        fastRaids() ? 6.0 : std::max(cfg().pacingFirstRaidGraceMin, cfg().pacingCalmMin) * 60.0 * rnd(1, 1.4);
    enter(p, PH_Calm, grace);

    int forced = forcedPhase();
    const Town *from = nullptr;
    if (forced == PH_Warning)
    {
        int f = pickFaction(id, from);
        if (f >= 0)
            startWarning(id, p, f, *from);
        else // nothing to raid from: still show the warning HUD
            enter(p, PH_Warning, cfg().pacingWarningS);
    }
    else if (forced == PH_Siege || forced == PH_Aftermath)
        enter(p, uint8_t(forced), cfg().pacingAftermathMin * 60.0);
}

void PacingSystem::tick(int id)
{
    Pacing &p = world.bp[id].pacing;
    if (p.left < 0)
        begin(id, p);

    p.left -= 1.0;
    switch (p.phase)
    {
    case PH_Calm:
    {
        if (p.left > 0)
            break;
        const Town *from = nullptr;
        int f = pickFaction(id, from);
        if (f >= 0)
            startWarning(id, p, f, *from);
        else
            p.left = 0; // nobody hostile: stay calm and look again next second
        break;
    }
    case PH_Warning:
        if (p.left <= 0)
            startSiege(id, p);
        break;
    case PH_Siege:
        if (raidersOf(id) == 0)
            endSiege(id, p);
        else if (p.left <= 0) // exit: recall a raid that outstays its welcome
        {
            reg.view<Raider>().each([&](entt::entity e, const Raider &r)
            {
                if (r.target == id)
                    world.destroyNow(e);
            });
            endSiege(id, p);
        }
        break;
    default:
        if (p.left <= 0)
        {
            enter(p, PH_Calm, calmSeconds());
        }
        break;
    }
}

void PacingSystem::second()
{
    for (int id = 1; id <= MAX_PLAYERS; id++)
    {
        if (!world.pl[id].used)
            continue;
        Pacing &p = world.bp[id].pacing;
        // Defeated / homeless: nothing to raid. A pending warning or siege is dropped (no stuck state).
        if (world.bp[id].defeat.phase != DF_None || world.townHallOf(id) == entt::null)
        {
            if (p.phase == PH_Warning || p.phase == PH_Siege)
                enter(p, PH_Calm, calmSeconds());
            continue;
        }
        if (world.pl[id].online) // offline owner: the director is paused
            tick(id);
    }
}

void PacingSystem::send(int id)
{
    const Pacing &p = world.bp[id].pacing;
    bool hasOrigin = p.phase == PH_Warning || p.phase == PH_Siege;
    Writer w;
    w.put<uint8_t>(S_Pacing)
        .put<uint8_t>(p.phase)
        .put<uint16_t>(uint16_t(std::clamp(p.left, 0.0, 65000.0)))
        .put<uint16_t>(uint16_t(std::clamp(p.total, 1.0, 65000.0)))
        .put<uint8_t>(p.raiders)
        .put<uint8_t>(p.faction)
        .put<uint8_t>(hasOrigin ? 1 : 0)
        .put<float>(p.origin.x)
        .put<float>(p.origin.y);
    world.net.send(world.pl[id].conn, w);
}
