/**
 * @file Hud.cpp
 * @brief HUD: resource ribbon, banners, message log, help line, drag selection and the cursor.
 */
#include "ui/Hud.hpp"

#include "client/App.hpp"
#include "rendering/Palette.hpp"
#include "rendering/Text.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

#include <cmath>

void Hud::load()
{
    mHud = loadTMap("hud_resources");
    mRoster = loadTMap("roster_panel");
}

void Hud::onLog(const LogEvent &e)
{
    logs.push_back({e.text, GetTime()});
    if (logs.size() > 7)
        logs.erase(logs.begin());
}

Rectangle Hud::hudRect() const
{
    Vector2 s = mHud.size();
    return {10, 10, s.x, s.y};
}
Rectangle Hud::nameRect() const
{
    Vector2 s = ui::titleMap().size();
    return {10, 78, s.x, s.y};
}
Rectangle Hud::coordRect() const
{
    Rectangle f = app.minimap.frame();
    return {f.x + f.width / 2 - 78, f.y + f.height + 2, 156, 24};
}
bool Hud::over(Vector2 m) const
{
    return CheckCollisionPointRec(m, hudRect()) || CheckCollisionPointRec(m, nameRect()) ||
           CheckCollisionPointRec(m, coordRect()) || CheckCollisionPointRec(m, socialTabBtn());
}

void Hud::drawTop()
{
    const ClientWorld &W = app.world;
    // resource ribbon
    Rectangle h = hudRect();
    drawTMap(mHud, h);
    float slot = (h.width - 2 * float(mHud.padding) - 60) / 4.f;
    for (int r = 0; r < R_Count; r++)
    {
        float x = h.x + float(mHud.padding) + 22 + float(r) * slot;
        ui::drawResourceIcon(r, x, h.y + h.height * .5f - 14, .45f);
        bool food = r == R_Meat,
             low = food && W.res[r] < 60.f; // food (meat) pays for every unit, building, upgrade and repair
        Color c = low ? (std::fmod(GetTime(), 1.0) < .5 ? rgb(255, 110, 90) : rgb(255, 200, 120)) : WHITE;
        textShadow(std::to_string(int(W.res[r])), int(x) + 30, int(h.y + h.height * .5f) - 13, 18, c);
        textShadow(food ? "Food" : RES_NAME[r], int(x) + 30, int(h.y + h.height * .5f) + 5, 10,
                   food ? rgb(255, 200, 150) : rgb(230, 220, 200));
    }
    if (W.res[R_Meat] < 60.f && app.session.welcomed)
        textShadow(
            "Low food! Free pawns hunt sheep; Harbours fish. Food pays for units, buildings, upgrades and repairs.",
            int(h.x) + 8, int(h.y + h.height) + 2, 12, rgb(255, 160, 130));
    // name banner
    Rectangle nr = nameRect();
    drawTMap(ui::titleMap(), nr);
    int units = 0, bld = 0;
    W.reg.view<Info>().each([&](const Info &i)
    {
        if (i.owner == W.me)
            (isBuilding(i.kind) ? bld : units)++;
    });
    Texture2D av = tex(std::string("UI Elements/UI Elements/Human Avatars/Avatars_") + (W.me % 25 + 1 < 10 ? "0" : "") +
                       std::to_string(W.me % 25 + 1) + ".png");
    DrawTexturePro(av, {0, 0, 256, 256}, {nr.x + 22, nr.y + 8, 44, 44}, {0, 0}, 0, WHITE);
    textShadow(W.myName, int(nr.x) + 76, int(nr.y) + 12, 18, teamColor(W.me));
    textShadow(TextFormat("Units %d/%d  Buildings %d  FPS %d", units, UNIT_CAP, bld, GetFPS()), int(nr.x) + 76,
               int(nr.y) + 34, 12, rgb(235, 235, 235));
}

// Bottom left: message log above the help line. Both wrap at the chat column (bottom right) so nothing overlaps it.
void Hud::drawLog()
{
    int sw = ui::screenW(), sh = ui::screenH();
    double now = GetTime();
    float chatW = std::min(388.f, float(sw) - 16);
    int maxW = std::max(160, int(float(sw) - chatW - 8 - 14 - 16));
    auto help = wrapText(
        "LMB select/attack  RMB command/menu  M move  A attack  B build  P players  H home  Arrows/MMB pan  Wheel zoom",
        maxW, 11);
    for (size_t i = 0; i < help.size(); i++)
        textShadow(help[i], 14, sh - 14 - 13 * int(help.size() - 1 - i), 11, rgb(210, 214, 225, 200));
    int ly = sh - 30 - 13 * int(help.size() - 1);
    for (int i = int(logs.size()) - 1; i >= 0; i--)
    {
        double age = now - logs[i].t;
        if (age > 9)
            continue;
        auto ls = wrapText(logs[i].text, maxW, 16);
        Color c = rgb(255, 255, 255, int(255 * std::min(1.0, (9 - age) / 2)));
        for (size_t k = ls.size(); k-- > 0;)
        {
            textShadow(ls[k], 14, ly, 16, c);
            ly -= 20;
        }
    }
    Input::Mode mode = app.input.mode;
    if (mode != Input::M_None)
        textShadow(mode == Input::M_Move ? "MOVE: click a destination"
                                         : "ATTACK: click a target or ground (attack-move)",
                   sw / 2 - 150, 16, 20, rgb(255, 230, 120));
}

// Bottom right, under the chat bar: name of the biome under the mouse (skipped while the open chat's help line would
// touch it).
void Hud::drawBiome()
{
    const ClientWorld &W = app.world;
    Vector2 m = ui::mouse();
    if (app.overUI(m))
        return;
    Vector2 w = GetScreenToWorld2D(::GetMousePosition(), app.cam);
    int tx = int(std::floor(w.x / TILE)), ty = int(std::floor(w.y / TILE));
    if (!W.exploredAt(tx, ty))
        return;
    int b = W.biomeAt(tx, ty);
    if (b < 0 || b >= B_Count)
        return;
    const char *name = BIOME_NAME[b];
    int fs = 14, tw = MeasureText(name, fs);
    float h = 20, wdt = float(tw) + 34;
    Rectangle r{float(ui::screenW()) - wdt - 8, float(ui::screenH()) - h - 4, wdt, h};
    if (app.chat.isOpen)
    { // the chat help line runs along the bottom: keep clear of it
        float helpEnd = float(ui::screenW()) - std::min(388.f, float(ui::screenW()) - 16) - 8 +
                        float(MeasureText("Tab: channel   Enter: send   Esc: cancel", 10)) + 8;
        if (r.x < helpEnd)
            return;
    }
    DrawRectangleRounded(r, .4f, 6, rgb(30, 22, 16, 215));
    DrawRectangleRoundedLinesEx(r, .4f, 6, 1.5f, rgb(255, 225, 160, 160));
    DrawRectangleRec({r.x + 8, r.y + 5, 10, 10}, biomeColor(b));
    DrawRectangleLinesEx({r.x + 8, r.y + 5, 10, 10}, 1, rgb(255, 255, 255, 200));
    textShadow(name, int(r.x) + 24, int(r.y) + 3, fs, rgb(240, 235, 220));
}

// camera coordinates (so players can tell each other where they are)
void Hud::drawCoords()
{
    Rectangle cr = coordRect();
    DrawRectangleRounded(cr, .3f, 6, rgb(40, 28, 20, 225));
    DrawRectangleRoundedLinesEx(cr, .3f, 6, 2, rgb(20, 12, 8));
    std::string cs =
        TextFormat("X %d    Y %d", int(std::floor(app.cam.target.x / TILE)), int(std::floor(app.cam.target.y / TILE)));
    DrawText(cs.c_str(), int(cr.x + cr.width / 2) - MeasureText(cs.c_str(), 16) / 2, int(cr.y) + 4, 16,
             rgb(255, 240, 200));
}

// social menu button under the build tab
void Hud::drawSocialTab()
{
    Rectangle t = socialTabBtn();
    DrawRectangleRounded(t, .3f, 6, rgb(70, 50, 34, 235));
    DrawRectangleRoundedLinesEx(t, .3f, 6, 2, rgb(30, 20, 14));
    DrawTexturePro(tex("UI Elements/UI Elements/Icons/Icon_06.png"), {0, 0, 64, 64}, {t.x + 4, t.y + 8, 40, 40}, {0, 0},
                   0, WHITE);
}

void Hud::drawDragRect()
{
    if (!app.input.dragging)
        return;
    Vector2 m = ui::mouse();
    Vector2 ds{app.input.dragStart.x / ui::scale(), app.input.dragStart.y / ui::scale()};
    Rectangle R{std::min(ds.x, m.x), std::min(ds.y, m.y), std::abs(m.x - ds.x), std::abs(m.y - ds.y)};
    if (R.width > 6 || R.height > 6)
    {
        DrawRectangleRec(R, rgb(255, 235, 90, 36));
        DrawRectangleLinesEx(R, 1, rgb(255, 235, 90));
    }
}

void Hud::drawHoverLabel()
{
    ClientWorld &W = app.world;
    uint32_t hovered = app.input.hovered;
    if (!hovered || !W.ents.count(hovered))
        return;
    Vector2 m = ui::mouse();
    const Info &in = W.reg.get<Info>(W.ents[hovered]);
    std::string s =
        std::string(KINFO[in.kind].name) + (in.owner ? " - " + W.nameOf(in.owner) : "") + app.shipPanel.tooltip(in);
    int w = MeasureText(s.c_str(), 14);
    DrawRectangleRounded({m.x + 16, m.y + 18, float(w) + 12, 22}, .3f, 4, rgb(30, 22, 16, 220));
    DrawText(s.c_str(), int(m.x) + 22, int(m.y) + 22, 14, WHITE);
}

void Hud::drawCursor()
{
    Vector2 m = ui::mouse();
    const char *file = "Cursor_01.png";
    bool ui = app.overUI(m);
    if (app.input.hovered || ui)
        file = "Cursor_02.png";
    Placement &pl = app.placement;
    if (pl.active() && !ui)
    {
        IV t = Placement::placeTile(Kind(pl.buildKind), GetScreenToWorld2D(::GetMousePosition(), app.cam));
        if (pl.placeCheck(Kind(pl.buildKind), t) != P_OK)
            file = "Cursor_03.png";
    }
    Texture2D c = tex(std::string("UI Elements/UI Elements/Cursors/") + file);
    DrawTexturePro(c, {0, 0, 64, 64}, {m.x - 22, m.y - 17, 64, 64}, {0, 0}, 0, WHITE);
}
