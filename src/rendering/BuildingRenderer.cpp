#include "rendering/BuildingRenderer.hpp"

#include "client/App.hpp"
#include "rendering/Bars.hpp"
#include "rendering/Palette.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/Text.hpp"
#include "ui/CostRow.hpp"
#include "rendering/UnitAnims.hpp"
#include "ui/GameText.hpp"
#include "ui/UiScale.hpp"

#include <cmath>
#include <string>

void BuildingRenderer::queue(const RPos &p, const Info &i, uint32_t id, bool selected, bool hover, bool remembered, DrawList &items, double t)
{
    const ClientWorld &W = app.world;
    const KindInfo &ki = KINFO[i.kind];

    int tx = int(std::lround(p.x - float(ki.fw) * .5f)), ty = int(std::lround(p.y - float(ki.fh) * .5f));
    float cx = (float(tx) + float(ki.fw) * .5f) * TILE, bottom = float(ty + ki.fh) * TILE;
    float prog = std::clamp(float(i.prog) / 100.f, 0.f, 1.f);
    bool mine = i.owner == W.me;

    Color oc = !mine && W.hostileTo(i.owner) ? rgb(255, 90, 90) : rgb(255, 255, 255);
    int col = colorIdx(i.owner);
    float sortY = bottom;

    const BJob *jp = W.jobOf(id);
    BJob job = jp ? *jp : BJob{};
    bool hasJob = jp != nullptr, working = (i.state & BLD_WORK) != 0, crippled = (i.state & BLD_CRIPPLED) != 0;

    items.push_back({sortY - 6.f, [=, this]()
                     {
                         bool moving = (i.state & BLD_MOVING) != 0;
                         Color tint = remembered ? rgb(150, 155, 175, 170) : (moving ? rgb(255, 255, 255, 62) : (prog < 1.f ? rgb(255, 255, 255, 190) : (working ? rgb(175, 170, 160) : (crippled ? rgb(255, 140, 130) : WHITE))));
                         auto A = [&](Color c)
                         { if (moving) c.a = 62; return c; };
                         int md = hover ? 1 : 0;

                         if (i.kind == K_Harbour)
                         { // wooden pier over adjacent ocean tiles
                             for (int y = ty - 1; y <= ty + ki.fh; y++)
                                 for (int x = tx - 1; x <= tx + ki.fw; x++)
                                 {
                                     bool inside = x >= tx && x < tx + ki.fw && y >= ty && y < ty + ki.fh, corner = (x < tx || x >= tx + ki.fw) && (y < ty || y >= ty + ki.fh);
                                     if (inside || corner || app.world.biomeAt(x, y) != B_Ocean)
                                         continue;
                                     Rectangle pl{float(x) * TILE + 6, float(y) * TILE + 6, TILE - 12, TILE - 12};
                                     DrawRectangleRec(pl, rgb(150, 105, 62));
                                     for (int k = 0; k < 4; k++)
                                         DrawLineEx({pl.x, pl.y + 8.f + 16.f * float(k)}, {pl.x + pl.width, pl.y + 8.f + 16.f * float(k)}, 2, rgb(96, 62, 36));
                                     DrawRectangleLinesEx(pl, 2, rgb(70, 44, 26));
                                 }
                         }

                         if (i.kind == K_Campfire)
                         {
                             Anim f = mkAnim("Particle FX/Fire_03.png", 64, 64, 12, 32, 62);
                             for (int k = 0; k < 6; k++)
                             {
                                 float a = float(k) * 1.047f;
                                 DrawCircle(int(cx + std::cos(a) * 15), int(bottom - 12 + std::sin(a) * 7), 5, rgb(110, 108, 118));
                             }
                             if (prog >= 1.f)
                                 drawAnim(f, frameFor(f, t, float(id % 5)), cx, bottom - 12, false, 1.5f, WHITE, md, oc);
                             else
                                 DrawRectangle(int(cx - 10), int(bottom - 20), 20, 8, rgb(120, 80, 50));
                             return;
                         }

                         if (i.kind == K_Mine)
                         { // cave mouth, timber frame, ore pile
                             float w = float(ki.fw) * TILE - 12;
                             DrawEllipse(int(cx), int(bottom - 34), w * .42f, 40, A(rgb(36, 30, 30)));
                             DrawRectangle(int(cx - w * .42f - 4), int(bottom - 70), 8, 70, A(rgb(120, 82, 46)));
                             DrawRectangle(int(cx + w * .42f - 4), int(bottom - 70), 8, 70, A(rgb(120, 82, 46)));
                             DrawRectangle(int(cx - w * .42f - 8), int(bottom - 78), int(w * .84f + 16), 10, A(rgb(146, 100, 58)));
                             Texture2D r1 = tex("Terrain/Decorations/Rocks/Rock2.png"), r2 = tex("Terrain/Decorations/Rocks/Rock3.png"), g1 = tex("Terrain/Resources/Gold/Gold Stones/Gold Stone 2.png");
                             drawSprite(r1, {0, 0, 64, 64}, {cx - 62, bottom - 62, 64, 64}, tint, md, oc);
                             drawSprite(r2, {0, 0, 64, 64}, {cx + 14, bottom - 58, 64, 64}, tint, md, oc);
                             drawSprite(g1, {0, 0, 128, 128}, {cx - 4, bottom - 84, 96, 96}, tint, md, oc);
                             return;
                         }

                         BSpr s = buildingSprite(i.kind, i.aux);
                         Texture2D bt = tex(std::string("Buildings/") + COLOR_NAME[col] + " Buildings/" + s.file + ".png");
                         float dh = s.artH * s.scale * prog, sy = s.artH * (1.f - prog);
                         Rectangle src{0, sy, s.artW, s.artH - sy};
                         float dw = s.artW * s.scale;
                         Rectangle dst{cx - dw / 2, bottom - 6 - s.artBottom * s.scale + (s.artH * s.scale - dh), dw, dh};
                         drawSprite(bt, src, dst, tint, md, oc);
                     }});
    if (remembered)
    { // last known state of an NPC town building outside vision
        if (hover || selected)
            items.push_back({sortY + 5.f, [=]()
                             { textShadow("Last seen", int(cx) - MeasureText("Last seen", 14) / 2, int(bottom) + 6, 14, rgb(200, 205, 220)); }});
        return;
    }

    if ((prog < 1.f || working) && i.kind != K_Campfire) // site work: scaffold, tool icon, dust while a pawn works
        items.push_back({sortY + 1.f, [=, this]()
                         { siteOverlay(i.kind, cx, bottom, job, hasJob, id, t); }});

    if (crippled && prog >= 1.f)
        items.push_back({sortY + 6.f, [=]()
                         {
                             float y = bottom - float(ki.fh) * TILE - 46.f;
                             DrawTexturePro(tex("UI Elements/UI Elements/Icons/Icon_10.png"), {0, 0, 64, 64}, {cx - 20, y - 20, 40, 40}, {0, 0}, 0, rgb(255, 110, 100));
                             const char *s = "Critical damage: repair!";
                             textShadow(s, int(cx) - MeasureText(s, 14) / 2, int(y + 22), 14, rgb(255, 140, 120));
                         }});

    app.worldRenderer.effects.queueBuildingStatus(i, id, cx, bottom, ki.fw, ki.fh, sortY, items, t); // fire, red cross, relocation marker
    float hpFrac = i.hp / std::max(1.f, i.maxhp);

    if (prog >= 1.f && isBuilding(i.kind) && i.kind != K_Campfire)
    { // damage: smoke and flames
        if (hpFrac < .6f)
            items.push_back({sortY + 2.f, [=]()
                             { Anim d = mkAnim("Particle FX/Dust_02.png", 64, 64, 9, 32, 60); drawAnim(d, frameFor(d, t, float(id % 7)), cx - 20, bottom - 70, false, 1.8f, rgb(120, 120, 130, 210)); }});
        if (hpFrac < .3f)
            items.push_back({sortY + 3.f, [=]()
                             { Anim f = mkAnim("Particle FX/Fire_02.png", 64, 64, 12, 32, 62); drawAnim(f, frameFor(f, t, float(id % 5)), cx + 14, bottom - 46, false, 1.4f); drawAnim(f, frameFor(f, t, float(id % 3) + 4), cx - 22, bottom - 40, false, 1.1f); }});
    }

    if (i.kind == K_Campfire && prog >= 1.f)
        items.push_back({sortY + 2.f, [=]()
                         { Anim d = mkAnim("Particle FX/Dust_02.png", 64, 64, 6, 32, 60); drawAnim(d, frameFor(d, t, float(id % 3)), cx + 6, bottom - 60, false, 1.2f, rgb(200, 200, 205, 120)); }});

    if (i.kind == K_TownHall)
        items.push_back({sortY + 4.f, [=, this]()
                         {
                             std::string nm = app.world.nameOf(i.owner);
                             int hall = app.world.leaderClanOf(i.owner);
                             if (hall)
                                 nm += "  [Clan Hall]";
                             textShadow(nm, int(cx) - MeasureText(nm.c_str(), 16) / 2, int(bottom) - 150, 16, hall ? clanColor(hall) : teamColor(i.owner));
                         }});

    items.push_back({sortY + 5.f, [=]()
                     {
                         Rectangle fr{float(tx) * TILE, float(ty) * TILE - 40, float(ki.fw) * TILE, float(ki.fh) * TILE + 40};
                         if (selected)
                             brackets(fr, rgb(255, 235, 90));

                         if (prog < 1.f)
                         {
                             drawBar({cx, bottom + 4}, 70, prog, hasJob && job.phase == SP_Waiting ? BAR_IDLE : BAR_BUILD);
                             std::string s = hasJob && job.phase == SP_Waiting ? "Waiting for a pawn" : TextFormat("Building %d%%", int(prog * 100.f));
                             textShadow(s, int(cx) - MeasureText(s.c_str(), 14) / 2, int(bottom) + 18, 14, hasJob && job.phase == SP_Waiting ? rgb(255, 170, 120) : rgb(200, 225, 255));
                         }
                         else if (hasJob && job.kind != SJ_Build)
                         {
                             drawBar({cx, bottom + 4}, 76, float(job.pct) / 100.f, job.phase == SP_Waiting ? BAR_IDLE : (job.kind == SJ_Upgrade ? BAR_UPGRADE : BAR_REPAIR));
                             std::string s = job.phase == SP_Waiting ? "Waiting for a pawn" : TextFormat("%s %d%%", job.kind == SJ_Upgrade ? "Upgrading" : "Repairing", job.pct);
                             textShadow(s, int(cx) - MeasureText(s.c_str(), 14) / 2, int(bottom) + 18, 14, job.phase == SP_Waiting ? rgb(255, 170, 120) : rgb(255, 225, 150));
                         }

                         if (prog >= 1.f && !(hasJob && job.kind != SJ_Build) && (hpFrac < .995f || selected || hover))
                             drawBar({cx, bottom + 4}, 76, hpFrac, healthColor(hpFrac));

                         if (i.kind == K_Campfire && (hover || selected))
                             DrawCircleLinesV({cx, bottom - 16}, WARMTH_RADIUS * TILE, rgb(255, 170, 70, 160));
                     }});
}

// Wooden scaffold in front of a building site, a bobbing hammer (construction / upgrade) or gear (repair) icon above it,
// pulsing while the job waits for a pawn, and dust while a pawn works.
void BuildingRenderer::siteOverlay(Kind k, float cx, float bottom, BJob job, bool hasJob, uint32_t id, double t)
{
    const KindInfo &ki = KINFO[k];
    float w = float(ki.fw) * TILE - 10.f, h = float(ki.fh) * TILE + 30.f, x0 = cx - w / 2, top = bottom - h;
    Color pole = rgb(140, 95, 52, 230), dark = rgb(80, 52, 30, 230);

    int cols = ki.fw + 1;
    for (int c = 0; c < cols; c++)
    {
        float x = x0 + w * float(c) / float(cols - 1);
        DrawRectangleRec({x - 3, top, 6, h}, pole);
        DrawRectangleLinesEx({x - 3, top, 6, h}, 1, dark);
    }

    for (int r = 0; r < 3; r++)
    { // planks and cross braces
        float y = top + 14.f + (h - 24.f) * float(r) / 2.f;
        DrawRectangleRec({x0 - 6, y, w + 12, 7}, pole);
        DrawRectangleLinesEx({x0 - 6, y, w + 12, 7}, 1, dark);
    }

    for (int c = 0; c + 1 < cols; c++)
    {
        float xa = x0 + w * float(c) / float(cols - 1), xb = x0 + w * float(c + 1) / float(cols - 1);
        DrawLineEx({xa, top + 18.f}, {xb, bottom - 8.f}, 3, dark);
    }

    bool waiting = hasJob && job.phase == SP_Waiting, repair = hasJob && job.kind == SJ_Repair;
    float bob = float(std::sin(t * 3.0 + double(id % 7))) * 4.f, s = waiting ? 34.f + float(std::sin(t * 6.0)) * 4.f : 34.f;
    Texture2D icon = tex(repair ? "UI Elements/UI Elements/Icons/Icon_10.png" : "UI Elements/UI Elements/Icons/Icon_01.png");
    DrawCircleV({cx, top - 26.f + bob}, s * .62f, waiting ? rgb(120, 40, 30, 200) : rgb(40, 30, 20, 190));
    DrawTexturePro(icon, {0, 0, 64, 64}, {cx - s / 2, top - 26.f + bob - s / 2, s, s}, {0, 0}, 0, WHITE);

    if (hasJob && job.phase == SP_Working)
    {
        Anim d = mkAnim("Particle FX/Dust_01.png", 64, 64, 8, 32, 60);
        drawAnim(d, frameFor(d, t, float(id % 4)), cx - float(ki.fw) * 20.f, bottom - 6, false, 1.2f, rgb(255, 255, 255, 180));
    }
}

void BuildingRenderer::drawPlaceExtras()
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
        DrawRectangleLinesEx({(p.lx - r) * TILE, (p.ly - r) * TILE, 2 * r * TILE, 2 * r * TILE}, 4, rgb(255, 190, 90, 190));
    }
}

void BuildingRenderer::mineGhost(IV t, float cx, float bottom)
{
    Pos c = footCenter(K_Mine, t.x, t.y);
    int n = app.world.goldNear(c);
    DrawCircleLinesV({c.x * TILE, c.y * TILE}, GOLD_RADIUS * TILE, rgb(255, 215, 70, 170));
    std::string s = n ? TextFormat("Gold nearby: %d (+%d%% output)", n, int(std::lround(goldBonusOf(n) * 100.f))) : "No gold nearby";
    textShadow(s, int(cx) - MeasureText(s.c_str(), 16) / 2, int(bottom) + 34, 16, n ? rgb(255, 225, 90) : rgb(210, 210, 210));
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
    items.push_back({1e9f, [=, this]()
                     {
                         const ClientWorld &W = app.world;
                         drawPlaceExtras();
                         W.reg.view<Info, Goal>().each([&](const Info &i, const Goal &g) { // territory squares of own bases
                             if (i.owner != W.me || (i.kind != K_TownHall && i.kind != K_Outpost))
                                 return;
                             float r = float(anchorRadius(i.kind));
                             DrawRectangleRec({(g.x - r) * TILE, (g.y - r) * TILE, 2 * r * TILE, 2 * r * TILE}, rgb(90, 200, 255, 22));
                             DrawRectangleLinesEx({(g.x - r) * TILE, (g.y - r) * TILE, 2 * r * TILE, 2 * r * TILE}, 2, rgb(120, 220, 255, 150));
                         });

                         for (int y = tt.y; y < tt.y + KINFO[k].fh; y++)
                             for (int x = tt.x; x < tt.x + KINFO[k].fw; x++)
                             {
                                 bool tileOk = W.exploredAt(x, y) && isLand(W.biomeAt(x, y)) && !isCliffTile(W.biomeAt(x, y), W.biomeAt(x, y - 1)) && !W.occ.count(chunkKey(x, y));
                                 bool feat = tileOk && W.featBlocking(x, y); // uncleared tree / rock / gold vein: orange
                                 DrawRectangle(x * 64, y * 64, 64, 64, (ok && tileOk && !feat) ? rgb(80, 255, 90, 110) : (tileOk && (err != P_OK || feat) ? rgb(255, 150, 40, 130) : rgb(255, 60, 60, 120)));
                                 DrawRectangleLines(x * 64, y * 64, 64, 64, rgb(255, 255, 255, 140));
                             }

                         float cx = (float(tt.x) + float(KINFO[k].fw) * .5f) * TILE, bottom = float(tt.y + KINFO[k].fh) * TILE;
                         BSpr s = buildingSprite(k, 1);
                         if (s.file[0])
                         {
                             Texture2D bt = tex(std::string("Buildings/") + COLOR_NAME[colorIdx(W.me)] + " Buildings/" + s.file + ".png");
                             float dw = s.artW * s.scale, dh = s.artH * s.scale;
                             DrawTexturePro(bt, {0, 0, s.artW, s.artH}, {cx - dw / 2, bottom - 6 - s.artBottom * s.scale, dw, dh}, {0, 0}, 0, rgb(255, 255, 255, 150));
                         }

                         ghostLabel(k, tt, err, cx, bottom);

                         if (k == K_Campfire)
                             DrawCircleLinesV({cx, bottom - 30}, WARMTH_RADIUS * TILE, rgb(255, 170, 70, 160));

                         if (k == K_Mine)
                             mineGhost(tt, cx, bottom);
                     }});
}

void BuildingRenderer::queuePendingSites(DrawList &items)
{
    for (const PendingSite &ps : app.world.pendingSites)
    {
        const KindInfo &ki = KINFO[ps.kind];
        float cx = (float(ps.tx) + float(ki.fw) * .5f) * TILE, bottom = float(ps.ty + ki.fh) * TILE;
        PendingSite site = ps;
        items.push_back({bottom - 7.f, [=, this]()
                         {
                             DrawRectangleLinesEx({float(site.tx) * TILE, float(site.ty) * TILE, float(ki.fw) * TILE, float(ki.fh) * TILE}, 3, rgb(255, 210, 120, 170));
                             BSpr s = buildingSprite(site.kind, 1);
                             if (s.file[0])
                             {
                                 Texture2D bt = tex(std::string("Buildings/") + COLOR_NAME[colorIdx(app.world.me)] + " Buildings/" + s.file + ".png");
                                 float dw = s.artW * s.scale, dh = s.artH * s.scale;
                                 DrawTexturePro(bt, {0, 0, s.artW, s.artH}, {cx - dw / 2, bottom - 6 - s.artBottom * s.scale, dw, dh}, {0, 0}, 0, rgb(255, 255, 255, 80));
                             }

                             const char *lbl = "Waiting for the site to be cleared";
                             textShadow(lbl, int(cx) - MeasureText(lbl, 16) / 2, int(bottom) + 8, 16, rgb(255, 220, 150));
                         }});
    }
}

// under the placement ghost: the reason it cannot go here, or its cost row (red values = missing) and the clearing cost
void BuildingRenderer::ghostLabel(Kind k, IV tt, int err, float cx, float bottom)
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
