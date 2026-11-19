/**
 * @file BuildingDrawers.cpp
 * @brief Per-kind building drawers and the table that lists them (add a building's special look here).
 */
#include "client/App.hpp"
#include "rendering/BuildingRenderer.hpp"
#include "rendering/Palette.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/Text.hpp"
#include "rendering/UnitAnims.hpp"

#include <array>
#include <cmath>

// Wooden pier over the ocean tiles next to the harbour.
void BuildingRenderer::drawPier(const BuildingView &v) const
{
    const KindInfo &ki = KINFO[v.i.kind];

    for (int y = v.ty - 1; y <= v.ty + ki.fh; y++)
        for (int x = v.tx - 1; x <= v.tx + ki.fw; x++)
        {
            bool inside = x >= v.tx && x < v.tx + ki.fw && y >= v.ty && y < v.ty + ki.fh;
            bool corner = (x < v.tx || x >= v.tx + ki.fw) && (y < v.ty || y >= v.ty + ki.fh);
            if (inside || corner || app.world.biomeAt(x, y) != B_Ocean)
                continue;

            Rectangle pl{float(x) * TILE + 6, float(y) * TILE + 6, TILE - 12, TILE - 12};
            DrawRectangleRec(pl, rgb(150, 105, 62));
            for (int k = 0; k < 4; k++)
                DrawLineEx({pl.x, pl.y + 8.f + 16.f * float(k)}, {pl.x + pl.width, pl.y + 8.f + 16.f * float(k)}, 2,
                           rgb(96, 62, 36));
            DrawRectangleLinesEx(pl, 2, rgb(70, 44, 26));
        }
}

void BuildingRenderer::drawCampfire(const BuildingView &v) const
{
    Anim f = mkAnim("Particle FX/Fire_03.png", 64, 64, 12, 32, 62);

    for (int k = 0; k < 6; k++)
    {
        float a = float(k) * 1.047f;
        DrawCircle(int(v.cx + std::cos(a) * 15), int(v.bottom - 12 + std::sin(a) * 7), 5, rgb(110, 108, 118));
    }

    if (v.prog >= 1.f)
        drawAnim(f, frameFor(f, v.t, float(v.id % 5)), v.cx, v.bottom - 12, false, 1.5f, WHITE, v.hover ? 1 : 0, v.oc);
    else
        DrawRectangle(int(v.cx - 10), int(v.bottom - 20), 20, 8, rgb(120, 80, 50));
}

// Cave mouth, timber frame and ore pile.
void BuildingRenderer::drawMine(const BuildingView &v) const
{
    auto fade = [&](Color c)
    {
        if (v.moving)
            c.a = 62;
        return c;
    };
    float cx = v.cx, bottom = v.bottom;
    float w = float(KINFO[v.i.kind].fw) * TILE - 12;
    int md = v.hover ? 1 : 0;

    DrawEllipse(int(cx), int(bottom - 34), w * .42f, 40, fade(rgb(36, 30, 30)));
    DrawRectangle(int(cx - w * .42f - 4), int(bottom - 70), 8, 70, fade(rgb(120, 82, 46)));
    DrawRectangle(int(cx + w * .42f - 4), int(bottom - 70), 8, 70, fade(rgb(120, 82, 46)));
    DrawRectangle(int(cx - w * .42f - 8), int(bottom - 78), int(w * .84f + 16), 10, fade(rgb(146, 100, 58)));

    Texture2D r1 = tex("Terrain/Decorations/Rocks/Rock2.png"), r2 = tex("Terrain/Decorations/Rocks/Rock3.png"),
              g1 = tex("Terrain/Resources/Gold/Gold Stones/Gold Stone 2.png");
    drawSprite(r1, {0, 0, 64, 64}, {cx - 62, bottom - 62, 64, 64}, v.tint, md, v.oc);
    drawSprite(r2, {0, 0, 64, 64}, {cx + 14, bottom - 58, 64, 64}, v.tint, md, v.oc);
    drawSprite(g1, {0, 0, 128, 128}, {cx - 4, bottom - 84, 96, 96}, v.tint, md, v.oc);
}

void BuildingRenderer::queueCampfireSmoke(const BuildingView &v, DrawList &items) const
{
    if (v.prog >= 1.f)
        items.push_back({v.sortY() + 2.f, [=, this] { drawCampfireSmoke(v); }});
}

void BuildingRenderer::drawCampfireSmoke(const BuildingView &v) const
{
    Anim d = mkAnim("Particle FX/Dust_02.png", 64, 64, 6, 32, 60);
    drawAnim(d, frameFor(d, v.t, float(v.id % 3)), v.cx + 6, v.bottom - 60, false, 1.2f, rgb(200, 200, 205, 120));
}

void BuildingRenderer::queueTownHallName(const BuildingView &v, DrawList &items) const
{
    items.push_back({v.sortY() + 4.f, [=, this] { drawTownHallName(v); }});
}

void BuildingRenderer::drawTownHallName(const BuildingView &v) const
{
    std::string nm = app.world.nameOf(v.i.owner);
    int hall = app.world.leaderClanOf(v.i.owner);
    if (hall)
        nm += "  [Clan Hall]";
    textShadow(nm, int(v.cx) - MeasureText(nm.c_str(), 16) / 2, int(v.bottom) - 150, 16,
               hall ? clanColor(hall) : teamColor(v.i.owner));
}

void BuildingRenderer::drawWarmthRing(const BuildingView &v) const
{
    if (v.hover || v.selected)
        DrawCircleLinesV({v.cx, v.bottom - 16}, WARMTH_RADIUS * TILE, rgb(255, 170, 70, 160));
}

const BuildingDrawer &BuildingRenderer::drawerOf(Kind k)
{
    static const std::array<BuildingDrawer, K_Count> table = []
    {
        std::array<BuildingDrawer, K_Count> t{};

        t[K_Harbour].underlay = &BuildingRenderer::drawPier;
        t[K_Mine].body = &BuildingRenderer::drawMine;
        t[K_TownHall].extras = &BuildingRenderer::queueTownHallName;

        BuildingDrawer &fire = t[K_Campfire];
        fire.body = &BuildingRenderer::drawCampfire;
        fire.extras = &BuildingRenderer::queueCampfireSmoke;
        fire.overlay = &BuildingRenderer::drawWarmthRing;
        fire.siteFx = false;
        fire.damageFx = false;
        return t;
    }();
    return table[k];
}
