/**
 * @file Pacing.cpp
 * @brief Selftest: the Pacing Director cycles Calm -> Warning -> Siege -> Aftermath, never raids inside the grace or
 * aftermath, caps and adapts raid strength, survives save / reload and pauses while the owner is offline.
 */
#include "server/SelfTests.hpp"

#include <filesystem>

namespace selftest
{

namespace
{
void fastTimers()
{
    cfg().pacingCalmMin = .2; // 12 s
    cfg().pacingWarningS = 5;
    cfg().pacingAftermathMin = .2;
    cfg().pacingFirstRaidGraceMin = .1;
}

void hostileAll(World &w, int id)
{
    for (int f = 0; f < FACTION_COUNT; f++)
        w.pl[id].rep[f] = -50.f;
}

void killRaiders(World &w)
{
    std::vector<entt::entity> rs;
    w.reg.view<Raider>().each([&](entt::entity e, const Raider &) { rs.push_back(e); });
    for (auto e : rs)
        w.destroyNow(e);
}

// Phases cycle in order; no raiders outside the siege; the first warning waits for the grace period.
void cycle(Ctx &t)
{
    Fixture f;
    fastTimers();
    int id = f.join(1, 1000);
    hostileAll(f.world, id);
    const Pacing &p = f.world.bp[id].pacing;

    std::vector<int> seen{PH_Calm};
    double firstWarning = -1;
    int maxRaiders = 0;
    for (int s = 0; s < 120; s++)
    {
        f.run(1);
        int raiders = f.sys.pacing.raidersOf(id);
        maxRaiders = std::max(maxRaiders, raiders);
        if (p.phase != seen.back())
            seen.push_back(p.phase);
        if (p.phase == PH_Warning && firstWarning < 0)
            firstWarning = f.world.clock;
        if (p.phase != PH_Siege)
            t.check(raiders == 0, "no raiders outside the siege");
        if (p.phase == PH_Siege && p.left < p.total - 3.0)
            killRaiders(f.world); // a flawless defense
        if (s % 20 == 0)
            t.invariants(f, "pacing cycle");
        // Hostile patrols roam the map: this scenario tests the director, not the defense.
        if (auto th = f.hallOf(id); th != entt::null)
            f.world.reg.get<Health>(th).hp = f.world.reg.get<Health>(th).maxhp;
    }
    std::vector<int> want{PH_Calm, PH_Warning, PH_Siege, PH_Aftermath, PH_Calm, PH_Warning};
    t.check(seen.size() >= want.size() && std::equal(want.begin(), want.end(), seen.begin()),
            "phases cycle Calm -> Warning -> Siege -> Aftermath -> Calm -> Warning");
    t.check(firstWarning >= 11.0, "no warning before the grace period is over");
    t.check(maxRaiders >= 2 && maxRaiders <= cfg().pacingMaxRaiders, "a siege brings 2..cap raiders");
    t.check(p.survived >= 1 && p.factor > 1.f && p.factor <= cfg().pacingMaxRaidFactor,
            "a flawless defense raises the next raid a little, within the cap");
    t.check(f.world.pl[id].res[R_Wood] > 0, "survivors get a reward");
}

void strength(Ctx &t)
{
    t.check(raidSize(1000.f, 4.f, 1000, 14, .5f) == 14, "the hard cap holds");
    t.check(raidSize(1000.f, 4.f, 6, 14, .5f) == 3, "a small base never faces more than its share");
    t.check(raidSize(2.f, .5f, 100, 14, .5f) >= 2, "a raid is never smaller than 2");
    t.check(raidSize(10.f, 1.f, 100, 14, .5f) < raidSize(30.f, 1.f, 100, 14, .5f), "stronger bases get bigger raids");

    Fixture f; // adaptive rule: heavy losses lower the next raid
    fastTimers();
    int id = f.join(1, 1000);
    hostileAll(f.world, id);
    Pacing &p = f.world.bp[id].pacing;
    f.run(1);
    p.phase = PH_Siege;
    p.left = p.total = 600;
    p.assetsBefore = 400; // the base lost almost everything during this siege
    p.raiders = 5;
    float before = p.factor;
    f.run(2);
    t.check(p.phase == PH_Aftermath && p.factor < before, "heavy losses lower the next raid");
    t.invariants(f, "after heavy losses");
}

// State survives save / reload, an offline owner pauses the director.
void persistence(Ctx &t)
{
    std::string copy;
    double left = 0;
    float factor = 0;
    {
        Fixture a;
        fastTimers();
        int id = a.join(1, 1000);
        hostileAll(a.world, id);
        for (int i = 0; i < 40 && a.world.bp[id].pacing.phase != PH_Warning; i++)
            a.run(1); // timings are random: wait for the warning
        Pacing &p = a.world.bp[id].pacing;
        t.check(p.phase == PH_Warning && p.raiders >= 2, "a warning with a planned raid size is running");
        p.factor = 1.3f;
        left = p.left;
        factor = p.factor;
        a.sys.save.saveGame();
        copy = a.savePath + ".copy";
        std::filesystem::copy_file(a.savePath, copy, std::filesystem::copy_options::overwrite_existing);
    }
    Fixture b;
    fastTimers();
    std::filesystem::copy_file(copy, b.savePath, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(copy);
    t.check(b.sys.save.loadGame(), "saved world loads");
    const Pacing &p = b.world.bp[1].pacing;
    t.check(p.phase == PH_Warning && std::abs(p.left - left) < .01 && std::abs(p.factor - factor) < .001,
            "phase, timer and factor survive the reload");
    b.run(30);
    t.check(std::abs(p.left - left) < .01 && p.phase == PH_Warning, "offline owner: the director is paused");
    b.join(1, 1000);
    b.run(2);
    t.check(p.left < left - 1.0, "the timer runs again once the owner is back");
    t.invariants(b, "after reload");
}
} // namespace

void pacingTest(Ctx &t)
{
    cycle(t);
    strength(t);
    persistence(t);
}

} // namespace selftest
