/**
 * @file Wind.cpp
 * @brief Selftest: ship speed follows the wind rose, the wind drifts slowly and survives save / reload, and ships fire
 * only with archers aboard or an upgraded hull (crossbow).
 */
#include "server/SelfTests.hpp"

#include <cmath>
#include <filesystem>

namespace selftest
{

namespace
{
constexpr uint32_t SEED = 1; // a world whose Town Hall lies next to the sea
constexpr float PI = 3.14159265f;

// Start of a row of `len` open water tiles near the Town Hall.
bool findWaterRow(World &w, Pos near_, int len, int &x0, int &y0)
{
    for (int r = 0; r < 60; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
            {
                if (std::max(std::abs(dx), std::abs(dy)) != r)
                    continue;
                int x = int(near_.x) + dx, y = int(near_.y) + dy;
                bool ok = true;
                for (int i = 0; i < len && ok; i++)
                    ok = w.walkable(x + i, y, true, 0);
                if (ok)
                {
                    x0 = x;
                    y0 = y;
                    return true;
                }
            }
    return false;
}

entt::entity makeShip(World &w, NavalSystem &naval, int id, Kind k, Pos p, int level, int crewArchers)
{
    auto ship = w.makeUnit(k, uint8_t(id), p.x, p.y);
    naval.initShip(ship);
    w.reg.get<Info>(ship).level = uint8_t(level);
    naval.embark(w.makeUnit(crewArchers ? K_Archer : K_Pawn, uint8_t(id), p.x, p.y), ship);
    return ship;
}

// Tiles sailed in one second towards +x with the wind blowing along `windDir`.
float sail(Fixture &f, int id, Pos start, float windDir)
{
    World &w = f.world;
    Wind &wd = f.sys.wind.wind();
    wd.dir = wd.target = windDir;
    auto ship = makeShip(w, f.sys.naval, id, K_Ship, start, 1, 0);
    w.reg.get<Unit>(ship).path = {{start.x + 12.f, start.y}};
    w.reg.get<Unit>(ship).i = 0;
    f.run(1);
    float d = w.reg.get<Pos>(ship).x - start.x;
    w.destroyNow(ship);
    return d;
}

void speeds(Ctx &t, Fixture &f, int id, Pos start)
{
    float with = sail(f, id, start, 0.f), across = sail(f, id, start, PI / 2), against = sail(f, id, start, PI);
    float k = cfg().windStrength;
    t.check(std::abs(with / across - (1.f + k)) < .08f, "a tailwind speeds the ship up by the configured strength");
    t.check(std::abs(against / across - (1.f - k)) < .08f, "a headwind slows it down by the configured strength");
    t.check(with > across && across > against, "speed differs by heading: with > across > against");
}

void drift(Ctx &t, Fixture &f)
{
    Wind &wd = f.sys.wind.wind();
    float maxStep = 0, total = 0, prev = wd.dir;
    bool inRange = true;
    for (int i = 0; i < 900; i++)
    {
        f.sys.wind.second();
        float s = std::abs(windAngleDelta(prev, wd.dir));
        maxStep = std::max(maxStep, s);
        total += s;
        prev = wd.dir;
        inRange = inRange && wd.dir >= 0.f && wd.dir < WIND_TAU;
    }
    t.check(maxStep <= cfg().windTurnRate + 1e-4f, "the wind turns slowly (never faster than wind_turn_rate)");
    t.check(total > .5f && inRange, "the wind changes direction over time and stays within 0..2pi");
    t.check(std::abs(windAngleDelta(windFromByte(windToByte(2.5f)), 2.5f)) < WIND_TAU / 256.f,
            "the direction byte keeps 1.4 degrees");
}

float hpOf(World &w, entt::entity e)
{
    return w.reg.get<Health>(e).hp;
}

void combat(Ctx &t, Fixture &f, int id, int other, Pos at)
{
    World &w = f.world;
    cfg().windStrength = 0; // keep the ships where they are
    auto prey = [&](float dx)
    {
        auto e = w.makeUnit(K_Pawn, uint8_t(other), at.x + dx, at.y);
        w.reg.get<Health>(e).hp = w.reg.get<Health>(e).maxhp = 5000.f; // survives the volleys, so hp can be compared
        return e;
    };

    auto bare = makeShip(w, f.sys.naval, id, K_Ship, at, 1, 0);
    auto target = prey(3.f);
    t.check(f.sys.naval.firepower(bare) == 0, "a level 1 ship with a pawn crew has no firepower");
    float hp0 = hpOf(w, target);
    f.run(3);
    t.check(hpOf(w, target) == hp0, "a ship without archers or crossbow never attacks");

    auto armed = makeShip(w, f.sys.naval, id, K_Ship, at, 1, 1);
    t.check(f.sys.naval.firepower(armed) == 1, "one archer aboard = one shooter");
    f.run(3);
    float afterArcher = hpOf(w, target);
    t.check(afterArcher < hp0, "a ship with an archer aboard shoots the hostile in range");

    w.destroyNow(armed);
    auto far = prey(cfg().shipRange + 6.f);
    float farHp = hpOf(w, far);
    auto cross = makeShip(w, f.sys.naval, id, K_Longship, at, cfg().shipCrossbowLevel, 0);
    t.check(f.sys.naval.firepower(cross) == 1, "an upgraded hull carries a crossbow");
    f.run(3);
    t.check(hpOf(w, target) < afterArcher, "the crossbow ship fires");
    t.check(hpOf(w, far) == farHp, "nothing outside the range is hit");
    t.invariants(f, "after ship combat");
}

void reload(Ctx &t, const std::string &copy, const Wind &want)
{
    Fixture b(SEED);
    std::filesystem::copy_file(copy, b.savePath, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(copy);
    t.check(b.sys.save.loadGame(), "saved world loads");
    const Wind &got = b.sys.wind.wind();
    t.check(got.dir == want.dir && got.target == want.target && got.shiftIn == want.shiftIn && got.rng == want.rng,
            "the wind survives save / reload");
    t.invariants(b, "after reload");
}
} // namespace

void windTest(Ctx &t)
{
    Wind want;
    std::string copy;
    {
        Fixture f(SEED);
        int id = f.join(1, 1000), other = f.join(2, 1001);
        Pos hall = f.world.posOf(f.hallOf(id));
        int x0 = 0, y0 = 0;
        if (!findWaterRow(f.world, hall, 14, x0, y0))
        {
            t.check(false, "open water exists near the Town Hall");
            return;
        }
        Pos start{float(x0) + .5f, float(y0) + .5f};

        speeds(t, f, id, start);
        drift(t, f);
        combat(t, f, id, other, start);

        Wind &wd = f.sys.wind.wind();
        wd.dir = 1.f;
        wd.target = 2.f;
        wd.shiftIn = 77.f;
        want = wd;
        f.sys.save.saveGame();
        copy = f.savePath + ".copy";
        std::filesystem::copy_file(f.savePath, copy, std::filesystem::copy_options::overwrite_existing);
    }
    reload(t, copy, want);
}

} // namespace selftest
