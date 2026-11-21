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

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
// The ribbon art (title_ribbon / hud_resources): straight band between the swallow tails, as fractions of the
// map's size, measured on the composed tilemap.
constexpr float BAND_L = .13f, BAND_R = .87f, BAND_CY = .516f;
constexpr float HUD_K = .85f;      // overall size of both top-left banners and their contents
constexpr float NAME_SCALE = 1.5f; // name banner = title ribbon (scale .5) enlarged to match the resource ribbon
} // namespace

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
    return {10, 10, s.x * HUD_K, s.y * HUD_K};
}
Rectangle Hud::nameRect() const
{
    Vector2 s = ui::titleMap().size();
    float k = NAME_SCALE * HUD_K;
    return {10, 10 + hudRect().height + 6, s.x * k, s.y * k};
}
Rectangle Hud::coordRect() const
{
    Rectangle f = app.minimap.frame();
    return {f.x + f.width / 2 - 100, f.y + f.height + 2, 200, 32};
}
bool Hud::over(Vector2 m) const
{
    return CheckCollisionPointRec(m, hudRect()) || CheckCollisionPointRec(m, nameRect()) ||
           CheckCollisionPointRec(m, coordRect()) || CheckCollisionPointRec(m, socialTabBtn());
}

void Hud::drawTop()
{
    drawResources();
    drawNameBanner();
    drawPacing();
    drawRaidArrow();
}

void Hud::drawResources()
{
    const ClientWorld &W = app.world;
    Rectangle h = hudRect();
    drawTMap(mHud, h);

    // Each resource is one group (icon + value over name) centred in its quarter of the ribbon's straight band.
    const float bandL = h.x + h.width * BAND_L, bandW = h.width * (BAND_R - BAND_L), cy = h.y + h.height * BAND_CY;
    const float slot = bandW / 4.f, iconPx = 32 * HUD_K, gap = 6 * HUD_K;
    const int valueFs = int(17 * HUD_K + .5f), nameFs = std::max(9, int(10 * HUD_K + .5f));
    for (int r = 0; r < R_Count; r++)
    {
        // food (meat) pays for every unit, building, upgrade and repair
        bool food = r == R_Meat, low = food && W.res[r] < 60.f;
        Color c = low ? (std::fmod(GetTime(), 1.0) < .5 ? rgb(255, 110, 90) : rgb(255, 200, 120)) : WHITE;
        std::string value = std::to_string(int(W.res[r]));
        const char *name = food ? "Food" : RES_NAME[r];

        int textW = std::max(measureText(value, valueFs), measureText(name, nameFs));
        int vh = textHeight(valueFs), nh = textHeight(nameFs);
        float groupW = iconPx + gap + float(textW), x = bandL + slot * (float(r) + .5f) - groupW / 2;
        ui::drawResourceIcon(r, x, cy - iconPx / 2, iconPx / 64.f);

        float top = cy - float(vh + nh - 4) / 2;
        textShadow(value, int(x + iconPx + gap), int(top), valueFs, c);
        textShadow(name, int(x + iconPx + gap), int(top) + vh - 4, nameFs,
                   food ? rgb(255, 200, 150) : rgb(230, 220, 200));
    }
    if (W.res[R_Meat] < 60.f && app.session.welcomed)
        textShadow(
            "Low food! Free pawns hunt sheep; Harbours fish. Food pays for units, buildings, upgrades and repairs.",
            int(h.x) + 8, int(h.y + h.height) + 2, 12, rgb(255, 160, 130));
}

void Hud::drawNameBanner()
{
    const ClientWorld &W = app.world;
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
    float cy = nr.y + nr.height * BAND_CY, avPx = 52 * HUD_K, ax = nr.x + nr.width * (BAND_L + .015f);
    DrawTexturePro(av, {0, 0, 256, 256}, {ax, cy - avPx / 2, avPx, avPx}, {0, 0}, 0, WHITE);
    int tx = int(ax + avPx + 8), nameFs = int(17 * HUD_K + .5f), statFs = std::max(9, int(11 * HUD_K + .5f)),
        nh = textHeight(nameFs), sh = textHeight(statFs);
    int top = int(cy) - (nh + sh - 2) / 2;
    textShadow(W.myName, tx, top, nameFs, teamColor(W.me));
    textShadow(TextFormat("Units %d/%d  Buildings %d  FPS %d", units, UNIT_CAP, bld, GetFPS()), tx, top + nh - 2,
               statFs, rgb(235, 235, 235));
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
    int helpLh = textHeight(11) + 1, logLh = textHeight(16) + 2;
    for (size_t i = 0; i < help.size(); i++)
        textShadow(help[i], 14, sh - 6 - helpLh * int(help.size() - i), 11, rgb(210, 214, 225, 200));
    int ly = sh - 8 - helpLh * int(help.size()) - logLh;
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
            ly -= logLh;
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
    int fs = 14, tw = measureText(name, fs);
    float h = 20, wdt = float(tw) + 34;
    Rectangle r{float(ui::screenW()) - wdt - 8, float(ui::screenH()) - h - 4, wdt, h};
    // the chat help line runs along the bottom: keep clear of it
    if (app.chat.isOpen)
    {
        float helpEnd = float(ui::screenW()) - std::min(388.f, float(ui::screenW()) - 16) - 8 +
                        float(measureText("Tab: channel   Enter: send   Esc: cancel", 10)) + 8;
        if (r.x < helpEnd)
            return;
    }
    DrawRectangleRounded(r, .4f, 6, rgb(30, 22, 16, 215));
    DrawRectangleRoundedLinesEx(r, .4f, 6, 1.5f, rgb(255, 225, 160, 160));
    DrawRectangleRec({r.x + 8, r.y + 5, 10, 10}, biomeColor(b));
    DrawRectangleLinesEx({r.x + 8, r.y + 5, 10, 10}, 1, rgb(255, 255, 255, 200));
    textShadow(name, int(r.x) + 24, int(r.y) + 3, fs, rgb(240, 235, 220));
}

// Camera coordinates, so players can tell each other where they are.
void Hud::drawCoords()
{
    Rectangle cr = coordRect();
    DrawRectangleRounded(cr, .3f, 6, rgb(40, 28, 20, 225));
    DrawRectangleRoundedLinesEx(cr, .3f, 6, 2, rgb(20, 12, 8));
    std::string cs =
        TextFormat("X %d  Y %d", int(std::floor(app.cam.target.x / TILE)), int(std::floor(app.cam.target.y / TILE)));
    drawText(cs, int(cr.x + cr.width / 2) - measureText(cs, 22, TextFace::Pixel) / 2,
             int(cr.y + cr.height / 2) - textHeight(22, TextFace::Pixel) / 2, 22, rgb(255, 240, 200), TextFace::Pixel);
}

void Hud::drawSocialTab()
{
    Rectangle t = socialTabBtn();
    DrawRectangleRounded(t, .3f, 6, rgb(70, 50, 34, 235));
    DrawRectangleRoundedLinesEx(t, .3f, 6, 2, rgb(30, 20, 14));
    ui::drawTabIcon(t, 6, "Icon_06.png");
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
    int w = measureText(s.c_str(), 14);
    DrawRectangleRounded({m.x + 16, m.y + 18, float(w) + 12, 22}, .3f, 4, rgb(30, 22, 16, 220));
    drawText(s.c_str(), int(m.x) + 22, int(m.y) + 22, 14, WHITE);
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

// Top centre: phase badge. Calm is a soft green, warning / siege urgent orange / red (pulsing); the bar is the
// phase's remaining time, the tension pips grow from calm to siege.
void Hud::drawPacing()
{
    const ClientWorld &W = app.world;
    if (!W.pacing.seen || W.homeless || W.defeat.phase != DF_None)
        return;
    const ClientWorld::PacingView &p = W.pacing;

    double left = std::max(0.0, p.endsAt - GetTime());
    bool urgent = p.phase == PH_Warning || p.phase == PH_Siege;
    float pulse = urgent ? .75f + .25f * float(std::sin(GetTime() * (p.phase == PH_Siege ? 8.0 : 4.0))) : 1.f;
    Color tint = p.phase == PH_Calm        ? rgb(140, 200, 140)
                 : p.phase == PH_Aftermath ? rgb(150, 190, 210)
                 : p.phase == PH_Warning   ? rgb(255, 170, 60)
                                           : rgb(235, 70, 60);
    tint = Fade(tint, pulse);

    std::string label = pacingName(p.phase);
    if (p.phase == PH_Warning)
        label += TextFormat(": %s raid, %d raiders", FACTION_NAME[p.faction], p.raiders);
    std::string timer = p.phase == PH_Siege ? "Defend!" : TextFormat("%d:%02d", int(left) / 60, int(left) % 60);

    const int fs = 15, pad = 10;
    int w = std::max(180, measureText(label, fs) + measureText(timer, fs) + pad * 4), h = 40;
    float x = float(ui::screenW()) / 2 - float(w) / 2, y = 8;
    DrawRectangleRounded({x, y, float(w), float(h)}, .3f, 8, Fade(rgb(30, 26, 22), .82f));
    DrawRectangleRoundedLines({x, y, float(w), float(h)}, .3f, 8, tint);

    textShadow(label, int(x) + pad, int(y) + 5, fs, tint);
    textShadow(timer, int(x) + w - pad - measureText(timer, fs), int(y) + 5, fs, WHITE);

    // remaining-time bar
    float frac = p.phase == PH_Siege ? 1.f : float(std::clamp(left / p.total, 0.0, 1.0));
    DrawRectangleRec({x + pad, y + h - 12, float(w - 2 * pad), 5}, Fade(BLACK, .5f));
    DrawRectangleRec({x + pad, y + h - 12, float(w - 2 * pad) * frac, 5}, tint);
}

// Screen-edge arrow towards the town the raid marches from (warning and siege).
void Hud::drawRaidArrow()
{
    const ClientWorld &W = app.world;
    const ClientWorld::PacingView &p = W.pacing;
    if (!p.seen || !p.hasOrigin || p.phase == PH_Calm || W.defeat.phase != DF_None)
        return;

    Vector2 s = GetWorldToScreen2D({p.origin.x * TILE, p.origin.y * TILE}, app.cam);
    float sc = ui::scale(), sw = float(ui::screenW()), sh = float(ui::screenH());
    s = {s.x / sc, s.y / sc};
    const float m = 46.f;
    if (s.x > m && s.x < sw - m && s.y > m && s.y < sh - m)
        return; // the origin is on screen already

    Vector2 c = {sw / 2, sh / 2}, d = {s.x - c.x, s.y - c.y};
    float len = std::hypot(d.x, d.y);
    if (len < 1.f)
        return;
    d = {d.x / len, d.y / len};
    float k = std::min((sw / 2 - m) / std::max(.001f, std::fabs(d.x)), (sh / 2 - m) / std::max(.001f, std::fabs(d.y)));
    Vector2 at = {c.x + d.x * k, c.y + d.y * k}, side = {-d.y, d.x};
    float pulse = .7f + .3f * float(std::sin(GetTime() * 5.0));
    Color col = Fade(p.phase == PH_Siege ? rgb(235, 70, 60) : rgb(255, 170, 60), pulse);
    Vector2 tip = {at.x + d.x * 18, at.y + d.y * 18},
            a = {at.x - d.x * 10 + side.x * 14, at.y - d.y * 10 + side.y * 14},
            b = {at.x - d.x * 10 - side.x * 14, at.y - d.y * 10 - side.y * 14};
    DrawTriangle(tip, b, a, col); // raylib culls clockwise triangles: draw both windings
    DrawTriangle(tip, a, b, col);
    DrawTriangleLines(tip, b, a, Fade(BLACK, .7f));
}
