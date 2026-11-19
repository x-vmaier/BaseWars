/**
 * @file FxDrawers.cpp
 * @brief One-shot effect drawers and their table (add an effect type here).
 */
#include "rendering/FxDrawers.hpp"

#include "rendering/Palette.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/UnitAnims.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
float arrowDuration(const Fx &f)
{
    return std::max(.12f, std::hypot(f.x2 - f.x, f.y2 - f.y) / 16.f);
}

// Plays frames of a sprite sheet once, at the age of the effect.
void playOnce(const Anim &a, double age, float fps, float x, float y, float scale, Color tint = WHITE)
{
    int fr = int(age * fps);
    if (fr < a.n)
        drawAnim(a, fr, x, y, false, scale, tint);
}

void drawExplosion(const Fx &fx, double age, double)
{
    playOnce(mkAnim("Particle FX/Explosion_02.png", 192, 192, 14, 96, 120), age, 14, fx.x * TILE, fx.y * TILE + 40,
             1.6f);
    playOnce(mkAnim("Particle FX/Dust_02.png", 64, 64, 8, 32, 60), age, 8, fx.x * TILE - 30, fx.y * TILE, 3.f,
             rgb(110, 110, 110, 200));
}

void drawDust(const Fx &fx, double age, double)
{
    playOnce(mkAnim("Particle FX/Dust_01.png", 64, 64, 14, 32, 60), age, 14, fx.x * TILE, fx.y * TILE + 20, 2.f);
}

void drawSplash(const Fx &fx, double age, double)
{
    playOnce(mkAnim("Particle FX/Water Splash.png", 192, 192, 14, 96, 130), age, 14, fx.x * TILE, fx.y * TILE + 20,
             1.f);
}

void drawHeal(const Fx &fx, double age, double)
{
    playOnce(mkAnim("Units/Blue Units/Monk/Heal_Effect.png", 192, 192, 14, 96, 135), age, 14, fx.x * TILE,
             fx.y * TILE + 6, .8f);
}

void drawExtinguish(const Fx &fx, double age, double)
{
    playOnce(mkAnim("Particle FX/Water Splash.png", 192, 192, 12, 96, 130), age, 12, fx.x * TILE, fx.y * TILE + 4.f,
             .75f);
    playOnce(mkAnim("Particle FX/Dust_02.png", 64, 64, 9, 32, 60), age, 9, fx.x * TILE,
             fx.y * TILE - 30.f - float(age) * 30.f, 1.6f, rgb(215, 220, 230, 150));
}

// Arrow position along its arc; shared by plain and fire arrows.
Vector2 arrowPos(const Fx &fx, float k, float &rot)
{
    Vector2 a{fx.x * TILE, fx.y * TILE - 30}, b{fx.x2 * TILE, fx.y2 * TILE - 24};
    rot = std::atan2(b.y - a.y, b.x - a.x) * 57.2958f;
    return {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k - std::sin(k * 3.14159f) * 22.f};
}

bool arrowVisible(const Fx &fx, double age)
{
    return age < arrowDuration(fx);
}

void drawArrow(const Fx &fx, double age, double)
{
    float rot;
    Vector2 p = arrowPos(fx, float(age) / arrowDuration(fx), rot);
    DrawTexturePro(tex("Units/Blue Units/Archer/Arrow.png"), {0, 0, 64, 64}, {p.x, p.y, 48, 48}, {24, 24}, rot, WHITE);
}

bool fireArrowVisible(const Fx &fx, double age)
{
    return age < arrowDuration(fx) + .7;
}

void drawFireArrow(const Fx &fx, double age, double t)
{
    float dur = arrowDuration(fx);

    if (age >= dur)
    { // the target burns on for a moment
        Anim fl = mkAnim("Particle FX/Fire_02.png", 64, 64, 12, 32, 62);
        drawAnim(fl, frameFor(fl, t), fx.x2 * TILE, fx.y2 * TILE + 6.f, false, 1.2f,
                 rgb(255, 255, 255, int(255 * (1.0 - (age - dur) / .7))));
        return;
    }

    float rot;
    Vector2 p = arrowPos(fx, float(age) / dur, rot);
    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient({p.x, p.y}, 22.f, rgb(255, 150, 40, 170), rgb(255, 80, 0, 0));
    EndBlendMode();
    DrawTexturePro(tex("Units/Blue Units/Archer/Arrow.png"), {0, 0, 64, 64}, {p.x, p.y, 48, 48}, {24, 24}, rot,
                   rgb(255, 170, 90));
    Anim fl = mkAnim("Particle FX/Fire_03.png", 64, 64, 12, 32, 62);
    drawAnim(fl, frameFor(fl, t), p.x + std::cos(rot / 57.2958f) * 16.f, p.y + 14.f, false, .7f);
}
} // namespace

const FxDrawer *fxDrawer(uint8_t type)
{
    static const std::array<FxDrawer, E_Extinguish + 1> table = []
    {
        std::array<FxDrawer, E_Extinguish + 1> t{};
        t[E_Explosion] = {30, false, nullptr, drawExplosion};
        t[E_Dust] = {30, false, nullptr, drawDust};
        t[E_Splash] = {30, false, nullptr, drawSplash};
        t[E_Heal] = {40, false, nullptr, drawHeal};
        t[E_Arrow] = {50, true, arrowVisible, drawArrow};
        t[E_FireArrow] = {50, true, fireArrowVisible, drawFireArrow};
        t[E_Extinguish] = {60, false, nullptr, drawExtinguish};
        return t;
    }();

    // E_Fire has no entry: building fire is drawn from the building state.
    return type < table.size() && table[type].draw ? &table[type] : nullptr;
}
