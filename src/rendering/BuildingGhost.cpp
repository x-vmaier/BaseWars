/**
 * @file BuildingGhost.cpp
 * @brief Placement preview (footprint, territory, cost label) and pending clear-first sites.
 */
#include "client/App.hpp"
#include "rendering/BuildingRenderer.hpp"
#include "rendering/Palette.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/Text.hpp"
#include "rendering/UnitAnims.hpp"
#include "ui/CostRow.hpp"
#include "ui/GameText.hpp"
#include "ui/UiScale.hpp"

#include <cmath>
#include <string>

void BuildingRenderer::drawPlaceExtras() const
{
    const ClientWorld &W = app.world;
    if (app.placement.effMode() != Placement::HomelessHall)
        return;

    for (auto &p : W.roster)
    {
        if (p.id == W.me || !p.hasLoc || !p.clan || p.clan != W.myClan())
            continue;

        float r = CLAN_TOWN_RADIUS;
        DrawRectangleRec({(p.lx - r) * TILE, (p.ly - r) * TILE, 2 * r * TILE, 2 * r * TILE}, rgb(255, 170, 60, 14));
        DrawRectangleLinesEx({(p.lx - r) * TILE, (p.ly - r) * TILE, 2 * r * TILE, 2 * r * TILE}, 4,
                             rgb(255, 190, 90, 190));
    }
}

void BuildingRenderer::mineGhost(IV t, float cx, float bottom) const
{
    Pos c = footCenter(K_Mine, t.x, t.y);
    int n = app.world.goldNear(c);
    DrawCircleLinesV({c.x * TILE, c.y * TILE}, GOLD_RADIUS * TILE, rgb(255, 215, 70, 170));

    std::string s = n ? TextFormat("Gold nearby: %d (+%d%% output)", n, int(std::lround(goldBonusOf(n) * 100.f)))
                      : "No gold nearby";
    textShadow(s, int(cx) - MeasureText(s.c_str(), 16) / 2, int(bottom) + 34, 16,
               n ? rgb(255, 225, 90) : rgb(210, 210, 210));
}

void BuildingRenderer::queuePlacementGhost(DrawList &items)
{
    Placement &pl = app.placement;
    if (!pl.active() || app.overUI(ui::mouse()))
        return;

    Kind k = Kind(pl.buildKind);
    Vector2 w = GetScreenToWorld2D(::GetMousePosition(), app.cam);
    IV tt = Placement::placeTile(k, w);
    int err = pl.placeCheck(k, tt);
    bool ok = err == P_OK && app.world.affordable(pl.placeCost(k));

    items.push_back({1e9f, [=, this] { drawGhost(k, tt, err, ok); }});
}

void BuildingRenderer::drawGhost(Kind k, IV tt, int err, bool ok) const
{
    float cx = (float(tt.x) + float(KINFO[k].fw) * .5f) * TILE, bottom = float(tt.y + KINFO[k].fh) * TILE;

    drawPlaceExtras();
    drawTerritory();
    drawGhostFootprint(k, tt, err, ok);
    drawGhostSprite(k, tt, cx, bottom);
    ghostLabel(k, tt, err, cx, bottom);

    if (k == K_Campfire)
        DrawCircleLinesV({cx, bottom - 30}, WARMTH_RADIUS * TILE, rgb(255, 170, 70, 160));

    if (k == K_Mine)
        mineGhost(tt, cx, bottom);
}

// Territory squares of the own bases.
void BuildingRenderer::drawTerritory() const
{
    const ClientWorld &W = app.world;

    W.reg.view<Info, Goal>().each([&](const Info &i, const Goal &g)
    {
        if (i.owner != W.me || (i.kind != K_TownHall && i.kind != K_Outpost))
            return;

        float r = float(anchorRadius(i.kind));
        DrawRectangleRec({(g.x - r) * TILE, (g.y - r) * TILE, 2 * r * TILE, 2 * r * TILE}, rgb(90, 200, 255, 22));
        DrawRectangleLinesEx({(g.x - r) * TILE, (g.y - r) * TILE, 2 * r * TILE, 2 * r * TILE}, 2,
                             rgb(120, 220, 255, 150));
    });
}

void BuildingRenderer::drawGhostFootprint(Kind k, IV tt, int err, bool ok) const
{
    const ClientWorld &W = app.world;

    for (int y = tt.y; y < tt.y + KINFO[k].fh; y++)
        for (int x = tt.x; x < tt.x + KINFO[k].fw; x++)
        {
            bool tileOk = W.exploredAt(x, y) && isLand(W.biomeAt(x, y)) &&
                          !isCliffTile(W.biomeAt(x, y), W.biomeAt(x, y - 1)) && !W.occ.count(chunkKey(x, y));
            bool feat = tileOk && W.featBlocking(x, y); // uncleared tree / rock / gold vein: orange

            DrawRectangle(x * 64, y * 64, 64, 64,
                          (ok && tileOk && !feat)
                              ? rgb(80, 255, 90, 110)
                              : (tileOk && (err != P_OK || feat) ? rgb(255, 150, 40, 130) : rgb(255, 60, 60, 120)));
            DrawRectangleLines(x * 64, y * 64, 64, 64, rgb(255, 255, 255, 140));
        }
}

void BuildingRenderer::drawGhostSprite(Kind k, IV, float cx, float bottom) const
{
    BSpr s = buildingSprite(k, 1);
    if (!s.file[0])
        return;

    Texture2D bt =
        tex(std::string("Buildings/") + COLOR_NAME[colorIdx(app.world.me)] + " Buildings/" + s.file + ".png");
    float dw = s.artW * s.scale, dh = s.artH * s.scale;
    DrawTexturePro(bt, {0, 0, s.artW, s.artH}, {cx - dw / 2, bottom - 6 - s.artBottom * s.scale, dw, dh}, {0, 0}, 0,
                   rgb(255, 255, 255, 150));
}

void BuildingRenderer::queuePendingSites(DrawList &items)
{
    for (const PendingSite &ps : app.world.pendingSites)
    {
        const KindInfo &ki = KINFO[ps.kind];
        float bottom = float(ps.ty + ki.fh) * TILE;
        PendingSite site = ps;
        items.push_back({bottom - 7.f, [=, this] { drawPendingSite(site); }});
    }
}

void BuildingRenderer::drawPendingSite(const PendingSite &site) const
{
    const KindInfo &ki = KINFO[site.kind];
    float cx = (float(site.tx) + float(ki.fw) * .5f) * TILE, bottom = float(site.ty + ki.fh) * TILE;

    DrawRectangleLinesEx({float(site.tx) * TILE, float(site.ty) * TILE, float(ki.fw) * TILE, float(ki.fh) * TILE}, 3,
                         rgb(255, 210, 120, 170));

    BSpr s = buildingSprite(site.kind, 1);
    if (s.file[0])
    {
        Texture2D bt =
            tex(std::string("Buildings/") + COLOR_NAME[colorIdx(app.world.me)] + " Buildings/" + s.file + ".png");
        float dw = s.artW * s.scale, dh = s.artH * s.scale;
        DrawTexturePro(bt, {0, 0, s.artW, s.artH}, {cx - dw / 2, bottom - 6 - s.artBottom * s.scale, dw, dh}, {0, 0}, 0,
                       rgb(255, 255, 255, 80));
    }

    const char *lbl = "Waiting for the site to be cleared";
    textShadow(lbl, int(cx) - MeasureText(lbl, 16) / 2, int(bottom) + 8, 16, rgb(255, 220, 150));
}

// Under the placement ghost: the reason it cannot go here, or its cost row (red values = missing) and clearing cost.
void BuildingRenderer::ghostLabel(Kind k, IV tt, int err, float cx, float bottom) const
{
    const ClientWorld &W = app.world;
    float y = bottom + 10;

    if (err)
    {
        std::string s = PLACE_TEXT[err];
        textShadow(s, int(cx) - MeasureText(s.c_str(), 18) / 2, int(y), 18, rgb(255, 150, 130));
        return;
    }

    ui::CostStyle st = ui::darkCost(1.3f);
    st.fs = 18;
    const float *cost = app.placement.placeCost(k);
    Vector2 cs = ui::measureCosts(cost, 1e9f, st);
    ui::drawCosts(cost, cx - cs.x / 2, y, 1e9f, W.res, st);
    y += cs.y + 6;

    if (!W.affordable(cost))
    {
        const char *s = "Not enough resources";
        textShadow(s, int(cx) - MeasureText(s, 16) / 2, int(y), 16, rgb(255, 150, 130));
        y += 22;
    }

    std::string wl = app.placement.ghostLabel(k, tt);
    if (wl.empty())
        return;

    float wood[R_Count]{};
    wood[R_Wood] = float(W.clearWoodCost(W.blockedUnder(k, tt)));
    Vector2 ws = ui::measureCosts(wood, 1e9f, st);
    float tw = float(MeasureText(wl.c_str(), 18)), x0 = cx - (tw + 8 + ws.x) / 2;
    textShadow(wl, int(x0), int(y + (ws.y - 18) / 2), 18, rgb(255, 210, 140));
    ui::drawCosts(wood, x0 + tw + 8, y, 1e9f, W.res, st);
}
