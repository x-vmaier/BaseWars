/**
 * @file UnitAnims.cpp
 * @brief Per-kind unit animation and building sprite tables (add a unit's or building's art here).
 */
#include "rendering/UnitAnims.hpp"

#include "rendering/Palette.hpp"

#include <array>
#include <cmath>
#include <string>

namespace
{
// Everything an animation function may look at.
struct AnimCtx
{
    std::string base; // "Units/<Colour> Units/"
    int state, aux, aim, biome;
    bool moving;
    double t;
};

Anim clip(const AnimCtx &c, const std::string &file, int w, int n, float ax, float ay)
{
    return mkAnim(c.base + file, w, w, float(n), ax, ay);
}

Anim pawnAnim(const AnimCtx &c)
{
    if (c.state == US_Build)
        return clip(c, "Pawn/Pawn_Interact Hammer.png", 192, 8, 96, 135);
    if (c.state == US_Work)
        return clip(c, c.biome == B_Forest ? "Pawn/Pawn_Interact Axe.png" : "Pawn/Pawn_Interact Pickaxe.png", 192, 9,
                    96, 135);
    if (c.state == US_Attack)
        return clip(c, "Pawn/Pawn_Interact Knife.png", 192, 10, 96, 135);

    static const char *carry[] = {"", " Wood", " Gold", " Gold", " Meat"};
    std::string sfx = c.aux >= 1 && c.aux <= 4 ? carry[c.aux] : "";
    return clip(c, (c.moving ? "Pawn/Pawn_Run" : "Pawn/Pawn_Idle") + sfx + ".png", 192, c.moving ? 10 : 7, 96, 135);
}

Anim warriorAnim(const AnimCtx &c)
{
    if (c.state == US_Attack)
        return clip(c, int(c.t * 2.5) % 2 ? "Warrior/Warrior_Attack2.png" : "Warrior/Warrior_Attack1.png", 192, 10, 96,
                    135);
    return clip(c, c.moving ? "Warrior/Warrior_Run.png" : "Warrior/Warrior_Idle.png", 192, c.moving ? 10 : 7, 96, 135);
}

Anim archerAnim(const AnimCtx &c)
{
    if (c.state == US_Attack)
        return clip(c, "Archer/Archer_Shoot.png", 192, 10, 96, 135);
    return clip(c, c.moving ? "Archer/Archer_Run.png" : "Archer/Archer_Idle.png", 192, c.moving ? 10 : 7, 96, 135);
}

// The lancer thrusts in one of five directions, chosen from the aim angle.
Anim lancerAnim(const AnimCtx &c)
{
    if (c.state == US_Attack)
    {
        float ang = float(c.aim) / 255.f * 6.28319f - 3.14159f;
        float dx = std::cos(ang), dy = std::sin(ang);
        const char *d = std::abs(dy) < .38f ? "Right"
                                            : (dy > 0 ? (std::abs(dx) < .38f ? "Down" : "DownRight")
                                                      : (std::abs(dx) < .38f ? "Up" : "UpRight"));
        return clip(c, std::string("Lancer/Lancer_") + d + "_Attack.png", 320, 8, 160, 198);
    }
    return clip(c, c.moving ? "Lancer/Lancer_Run.png" : "Lancer/Lancer_Idle.png", 320, c.moving ? 10 : 9, 160, 198);
}

Anim monkAnim(const AnimCtx &c)
{
    if (c.state == US_Heal)
        return clip(c, "Monk/Heal.png", 192, 10, 96, 135);
    return clip(c, c.moving ? "Monk/Run.png" : "Monk/Idle.png", 192, c.moving ? 9 : 7, 96, 135);
}

Anim sheepAnim(const AnimCtx &c)
{
    return mkAnim(std::string("Terrain/Resources/Meat/Sheep/") + (c.moving ? "Sheep_Move.png" : "Sheep_Idle.png"), 128,
                  128, 7, 64, 84);
}

using AnimFn = Anim (*)(const AnimCtx &);

// Units without an entry idle like a warrior.
const std::array<AnimFn, K_Count> &animTable()
{
    static const std::array<AnimFn, K_Count> t = []
    {
        std::array<AnimFn, K_Count> a{};
        a[K_Pawn] = pawnAnim;
        a[K_Warrior] = warriorAnim;
        a[K_Archer] = archerAnim;
        a[K_Lancer] = lancerAnim;
        a[K_Monk] = monkAnim;
        a[K_Sheep] = sheepAnim;
        return a;
    }();
    return t;
}

// Firefighting: soldiers stamp at the flames while running, everybody else (pawns) scrubs with the hammer.
Anim warriorFire(const AnimCtx &c)
{
    return clip(c, "Warrior/Warrior_Run.png", 192, 14, 96, 135);
}
Anim archerFire(const AnimCtx &c)
{
    return clip(c, "Archer/Archer_Run.png", 192, 14, 96, 135);
}
Anim lancerFire(const AnimCtx &c)
{
    return clip(c, "Lancer/Lancer_Run.png", 320, 14, 160, 198);
}
Anim monkFire(const AnimCtx &c)
{
    return clip(c, "Monk/Run.png", 192, 12, 96, 135);
}
Anim pawnFire(const AnimCtx &c)
{
    return clip(c, "Pawn/Pawn_Interact Hammer.png", 192, 8, 96, 135);
}

const std::array<AnimFn, K_Count> &fireTable()
{
    static const std::array<AnimFn, K_Count> t = []
    {
        std::array<AnimFn, K_Count> a{};
        a[K_Warrior] = warriorFire;
        a[K_Archer] = archerFire;
        a[K_Lancer] = lancerFire;
        a[K_Monk] = monkFire;
        return a;
    }();
    return t;
}

std::string unitBase(int owner)
{
    return std::string("Units/") + COLOR_NAME[colorIdx(owner)] + " Units/";
}
} // namespace

Anim extinguishAnim(Kind k, int owner)
{
    AnimCtx c{unitBase(owner), US_Extinguish, 0, 0, 0, false, 0};
    AnimFn fn = fireTable()[k];
    return fn ? fn(c) : pawnFire(c);
}

Anim unitAnim(Kind k, int owner, int state, int aux, int aim, bool moving, int biome, double t)
{
    if (state == US_Extinguish)
        return extinguishAnim(k, owner);

    AnimCtx c{unitBase(owner), state, aux, aim, biome, moving, t};
    AnimFn fn = animTable()[k];
    return fn ? fn(c) : clip(c, "Warrior/Warrior_Idle.png", 192, 7, 96, 135);
}

BSpr buildingSprite(Kind k, int)
{
    // file, scale, artBottom, artH, artW; no file = drawn procedurally (mine, campfire)
    static const std::array<BSpr, K_Count> table = []
    {
        std::array<BSpr, K_Count> t{};
        t.fill({"", 1, 0, 0, 0});
        t[K_TownHall] = {"Castle", .8f, 249, 256, 320};
        t[K_Outpost] = {"Tower", .8f, 230, 256, 128};
        t[K_Storage] = {"Barracks", .66f, 245, 256, 192};
        t[K_Archery] = {"Archery", 1.f, 240, 256, 192};
        t[K_Harbour] = {"House3", 1.f, 172, 192, 128};
        t[K_Lumber] = {"House1", .9f, 173, 192, 128}; // House2 is a rotated variant and is never used
        return t;
    }();
    return table[k];
}
