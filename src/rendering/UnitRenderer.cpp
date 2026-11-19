/**
 * @file UnitRenderer.cpp
 * @brief Unit draw queue and the per-kind drawer table (ships, monks, sheep).
 */
#include "rendering/UnitRenderer.hpp"

#include "client/App.hpp"
#include "rendering/Bars.hpp"
#include "rendering/Palette.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/Text.hpp"
#include "rendering/UnitAnims.hpp"

#include <array>
#include <cmath>
#include <string>

UnitView UnitRenderer::makeView(const RPos &p, const Info &i, const Face &fc, uint32_t id, bool selected, bool hover,
                                double t, int biome) const
{
    const ClientWorld &W = app.world;

    UnitView v{};
    v.i = i;
    v.left = fc.left;
    v.selected = selected;
    v.hover = hover;
    v.px = p.x * TILE;
    v.py = p.y * TILE;
    v.moving = (i.state & 7) == US_Move;
    v.convoy = (i.state & UNIT_CONVOY_FLAG) != 0;
    v.carry = v.convoy && !i.aux ? 2 : i.aux;
    v.biome = biome;
    v.id = id;
    v.t = t;

    if (drawerOf(i.kind).fixedOutline)
        v.outline = rgb(255, 235, 140);
    else if (i.owner == W.me || W.alliedWith(i.owner))
        v.outline = rgb(255, 255, 255);
    else
        v.outline = W.hostileTo(i.owner) ? rgb(255, 90, 90) : rgb(255, 225, 130);
    return v;
}

void UnitRenderer::queue(const RPos &p, Info &i, Face &fc, uint32_t id, bool selected, bool hover, DrawList &items,
                         double t, int biome)
{
    UnitView v = makeView(p, i, fc, id, selected, hover, t, biome);
    const UnitDrawer &d = drawerOf(i.kind);

    items.push_back({v.py, [=, this] { d.body ? (this->*d.body)(v) : drawUnit(v); }});
    if (d.extras)
        (this->*d.extras)(v, items);
}

void UnitRenderer::drawUnit(const UnitView &v) const
{
    const Info &i = v.i;
    const UnitDrawer &d = drawerOf(i.kind);
    Anim a = unitAnim(i.kind, i.owner, i.state & 7, v.carry, i.aim, v.moving, v.biome, v.t);

    DrawEllipse(int(v.px), int(v.py + 2), 16, 6, rgb(0, 0, 0, 60));
    if (v.selected)
        DrawEllipseLines(int(v.px), int(v.py + 2), 24, 10, rgb(255, 235, 90));
    drawAnim(a, frameFor(a, v.t, float(v.id % 7)), v.px, v.py + 6, v.left, 1.f, WHITE, v.hover ? 1 : 0, v.outline);

    drawLabels(v);
    if ((i.state & 7) == US_Extinguish)
        extinguishSplash(v.px, v.py, v.left, v.t, v.id);
    if (d.overlay)
        (this->*d.overlay)(v);

    float fr = i.hp / std::max(1.f, i.maxhp);
    if ((fr < .995f || v.selected || v.hover) && d.healthBar)
        drawBar({v.px, v.py - 56}, 48, fr, healthColor(fr));
}

void UnitRenderer::drawLabels(const UnitView &v) const
{
    if (v.convoy)
        textShadow("Convoy", int(v.px) - 22, int(v.py) - 70, 14, rgb(120, 230, 255));
    if ((v.i.state & UNIT_SUPPORT_FLAG) && !v.convoy)
        textShadow("Patrol", int(v.px) - 20, int(v.py) - 70, 14,
                   app.world.alliedWith(v.i.owner) ? rgb(255, 190, 80) : rgb(255, 110, 100));
}

void UnitRenderer::drawShip(const UnitView &v) const
{
    ShipRenderer &ships = app.worldRenderer.ships;
    ships.drawShip(v.px, v.py, v.i.owner, v.left, v.hover, v.t, v.id, v.i.aux, v.i.prog);
    ships.drawLabel(v.px, v.py, v.i, v.selected, v.hover);
    if (v.selected)
        DrawEllipseLines(int(v.px), int(v.py + 4), 56, 18, rgb(255, 235, 90));
}

void UnitRenderer::queueShipWake(const UnitView &v, DrawList &items) const
{
    if (v.moving)
        items.push_back({v.py - 1, [=, this] { drawShipWake(v); }});
}

void UnitRenderer::drawShipWake(const UnitView &v) const
{
    Anim s = mkAnim("Particle FX/Water Splash.png", 192, 192, 8, 96, 150);
    drawAnim(s, frameFor(s, v.t, float(v.id % 3)), v.px - (v.left ? -30.f : 30.f), v.py + 6, false, .45f,
             rgb(255, 255, 255, 140));
}

void UnitRenderer::drawHealAura(const UnitView &v) const
{
    if ((v.i.state & 7) != US_Heal)
        return;

    Anim h = mkAnim(std::string("Units/") + COLOR_NAME[colorIdx(v.i.owner)] + " Units/Monk/Heal_Effect.png", 192, 192,
                    10, 96, 135);
    drawAnim(h, frameFor(h, v.t), v.px, v.py + 6, false, 1.f);
}

const UnitDrawer &UnitRenderer::drawerOf(Kind k)
{
    static const std::array<UnitDrawer, K_Count> table = []
    {
        std::array<UnitDrawer, K_Count> t{};

        t[K_Ship].body = &UnitRenderer::drawShip;
        t[K_Ship].extras = &UnitRenderer::queueShipWake;
        t[K_Monk].overlay = &UnitRenderer::drawHealAura;
        t[K_Sheep].healthBar = false;
        t[K_Sheep].fixedOutline = true;
        return t;
    }();
    return table[k];
}

void UnitRenderer::extinguishSplash(float px, float py, bool left, double t, uint32_t id)
{
    Anim s = mkAnim("Particle FX/Water Splash.png", 192, 192, 10, 96, 150);
    float dir = left ? -1.f : 1.f;
    drawAnim(s, frameFor(s, t, float(id % 5)), px + dir * 34.f, py + 8, false, .42f, rgb(255, 255, 255, 220));
    for (int k = 0; k < 4; k++)
    { // droplets arcing towards the fire
        float ph = float(std::fmod(t * 2.2 + double(k) * .25 + double(id % 7) * .1, 1.0));
        DrawCircleV({px + dir * (14.f + ph * 34.f), py - 38.f - std::sin(ph * 3.14159f) * 22.f + ph * 28.f}, 3.f,
                    rgb(120, 190, 255, int(230 * (1.f - ph * .5f))));
    }
}
