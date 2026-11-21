/**
 * @file ObjectiveHud.cpp
 * @brief HUD part of the objectives: tracker (short / medium / long goal), completion banner in the ribbon theme and
 * the pointer over the building a goal asks for. Reads ClientWorld::obj only (S_Objectives).
 */
#include "client/App.hpp"
#include "rendering/Palette.hpp"
#include "rendering/Text.hpp"
#include "ui/Hud.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
constexpr float TRACK_W = 330.f;
const char *const TIER_LABEL[OT_Count] = {"NOW", "SOON", "LONG"};
const Color TIER_COLOR[OT_Count] = {{255, 215, 100, 255}, {150, 205, 255, 255}, {200, 170, 255, 255}};

std::string progressText(const ClientWorld::ObjView &o, int tier)
{
    const ObjDef &d = OBJECTIVES[o.id[tier]];
    if (d.count <= 1)
        return "";
    return std::to_string(std::min<int>(o.prog[tier], d.count)) + "/" + std::to_string(d.count);
}
} // namespace

// Right of the minimap, below the top edge. Hidden while defeated or when the ribbon leaves no room.
void Hud::drawObjectives()
{
    const ClientWorld &W = app.world;
    if (!W.obj.seen || W.defeat.phase != DF_None || !app.session.welcomed)
        return;

    float right = app.minimap.frame().x - 10, x = right - TRACK_W, y = 10;
    if (x < hudRect().x + hudRect().width + 10)
        return;

    int shown = 0;
    float h = 12;
    const int titleFs = 15, hintFs = 12;
    for (int t = 0; t < OT_Count; t++)
        if (W.obj.id[t] != OBJ_None)
        {
            shown++;
            h += float(textHeight(titleFs)) + 4;
            if (t == OT_Short && app.settings.hints)
                h += float(textHeight(hintFs)) + 2;
        }
    if (shown)
    {
        Rectangle r{x, y, TRACK_W, h};
        DrawRectangleRounded(r, .15f, 6, rgb(30, 22, 16, 215));
        DrawRectangleRoundedLinesEx(r, .15f, 6, 1.5f, rgb(255, 225, 160, 160));
        float cy = y + 8;
        for (int t = 0; t < OT_Count; t++)
        {
            if (W.obj.id[t] == OBJ_None)
                continue;
            const ObjDef &d = OBJECTIVES[W.obj.id[t]];
            textShadow(TIER_LABEL[t], int(x) + 10, int(cy) + 2, 11, TIER_COLOR[t]);
            textShadow(d.title, int(x) + 54, int(cy), titleFs, rgb(250, 244, 228));
            std::string p = progressText(W.obj, t);
            if (!p.empty())
                textShadow(p, int(right) - 10 - measureText(p.c_str(), titleFs), int(cy), titleFs, TIER_COLOR[t]);
            cy += float(textHeight(titleFs)) + 4;
            if (t == OT_Short && app.settings.hints)
            {
                textShadow(d.hint, int(x) + 54, int(cy) - 2, hintFs, rgb(205, 200, 185));
                cy += float(textHeight(hintFs)) + 2;
            }
        }
    }
    drawObjectiveBanner();
}

// Ribbon with the finished goal; fades out over its last second.
void Hud::drawObjectiveBanner()
{
    const ClientWorld::ObjView &o = app.world.obj;
    double left = o.bannerUntil - GetTime();
    if (left <= 0 || o.bannerId < 0 || o.bannerId >= OBJ_Count)
        return;

    Vector2 s = ui::titleMap().size();
    float k = 380.f / s.x, a = float(std::min(1.0, left));
    Rectangle r{float(ui::screenW()) / 2 - 190, 76, 380, s.y * k};
    r.y += float(1.0 - std::min(1.0, (4.5 - left) * 6.0)) * -14.f; // slides in
    drawTMap(ui::titleMap(), r);

    const ObjDef &d = OBJECTIVES[o.bannerId];
    const char *head = o.bannerRewarded ? "Goal complete!" : "Goal skipped";
    float cy = r.y + r.height * .516f;
    int hh = textHeight(15), th = textHeight(13), top = int(cy) - (hh + th - 2) / 2;
    Color ink = Fade(rgb(250, 240, 210), a);
    textShadow(head, int(r.x + r.width / 2) - measureText(head, 15) / 2, top, 15, ink);
    textShadow(d.title, int(r.x + r.width / 2) - measureText(d.title, 13) / 2, top + hh - 2, 13, Fade(WHITE, a));
}

// A bobbing arrow over the nearest own building of the kind the goal names (kinds not in the build menu: Town Hall,
// Archery already built, ...). The build menu highlights buildable kinds itself.
void Hud::drawObjectivePointer()
{
    const ClientWorld &W = app.world;
    int pk = W.objectivePointKind();
    if (pk < 0 || !W.obj.seen || W.defeat.phase != DF_None)
        return;
    if (std::find(BuildMenu::LIST.begin(), BuildMenu::LIST.end(), Kind(pk)) != BuildMenu::LIST.end())
        return;

    Vector2 best{0, 0};
    float bd = 1e9f;
    Vector2 c = GetScreenToWorld2D({float(GetScreenWidth()) / 2, float(GetScreenHeight()) / 2}, app.cam);
    W.reg.view<Info, RPos>().each([&](const Info &i, const RPos &p)
    {
        if (i.owner != W.me || i.kind != Kind(pk) || !isBuilding(i.kind) || (i.state & BLD_MOVING))
            return;
        float d = std::hypot(p.x * TILE - c.x, p.y * TILE - c.y);
        if (d < bd)
        {
            bd = d;
            best = {p.x * TILE, p.y * TILE};
        }
    });
    if (bd > 1e8f)
        return;

    Vector2 s = GetWorldToScreen2D({best.x, best.y - float(KINFO[pk].fh) * TILE * .5f - 18}, app.cam);
    float sc = ui::scale();
    s = {s.x / sc, s.y / sc};
    if (s.x < 0 || s.y < 0 || s.x > float(ui::screenW()) || s.y > float(ui::screenH()))
        return;
    float bob = float(std::sin(GetTime() * 5.0)) * 5.f;
    Color col = rgb(255, 220, 90);
    Vector2 tip{s.x, s.y + bob}, a{s.x - 11, s.y - 18 + bob}, b{s.x + 11, s.y - 18 + bob};
    DrawTriangle(tip, b, a, col);
    DrawTriangle(tip, a, b, col);
    DrawTriangleLines(tip, b, a, Fade(BLACK, .7f));
}
