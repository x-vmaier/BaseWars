/**
 * @file BuildingRenderer.cpp
 * @brief Building draw queue: body, site scaffold, damage fx, status labels and bars.
 */
#include "rendering/BuildingRenderer.hpp"

#include "client/App.hpp"
#include "rendering/Bars.hpp"
#include "rendering/Palette.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/Text.hpp"
#include "rendering/UnitAnims.hpp"
#include "ui/GameText.hpp"

#include <cmath>
#include <string>

BuildingView BuildingRenderer::makeView(const RPos &p, const Info &i, uint32_t id, bool selected, bool hover,
                                        bool remembered, double t) const
{
    const ClientWorld &W = app.world;
    const KindInfo &ki = KINFO[i.kind];

    BuildingView v{};
    v.i = i;
    v.id = id;
    v.tx = int(std::lround(p.x - float(ki.fw) * .5f));
    v.ty = int(std::lround(p.y - float(ki.fh) * .5f));
    v.cx = (float(v.tx) + float(ki.fw) * .5f) * TILE;
    v.bottom = float(v.ty + ki.fh) * TILE;
    v.prog = std::clamp(float(i.prog) / 100.f, 0.f, 1.f);
    v.hpFrac = i.hp / std::max(1.f, i.maxhp);
    v.selected = selected;
    v.hover = hover;
    v.remembered = remembered;
    v.moving = (i.state & BLD_MOVING) != 0;
    v.working = (i.state & BLD_WORK) != 0;
    v.crippled = (i.state & BLD_CRIPPLED) != 0;
    v.oc = i.owner != W.me && W.hostileTo(i.owner) ? rgb(255, 90, 90) : rgb(255, 255, 255);
    v.col = colorIdx(i.owner);
    v.t = t;

    const BJob *jp = W.jobOf(id);
    v.hasJob = jp != nullptr;
    v.job = jp ? *jp : BJob{};

    if (remembered)
        v.tint = rgb(150, 155, 175, 170);
    else if (v.moving)
        v.tint = rgb(255, 255, 255, 62);
    else if (v.prog < 1.f)
        v.tint = rgb(255, 255, 255, 190);
    else if (v.working)
        v.tint = rgb(175, 170, 160);
    else
        v.tint = v.crippled ? rgb(255, 140, 130) : WHITE;
    return v;
}

void BuildingRenderer::queue(const RPos &p, const Info &i, uint32_t id, bool selected, bool hover, bool remembered,
                             DrawList &items, double t)
{
    BuildingView v = makeView(p, i, id, selected, hover, remembered, t);
    const BuildingDrawer &d = drawerOf(i.kind);

    items.push_back({v.sortY() - 6.f, [=, this] { drawBody(v); }});
    if (remembered)
    {
        if (hover || selected)
            items.push_back({v.sortY() + 5.f, [=, this] { drawLastSeen(v); }});
        return;
    }

    queueBody(v, items);
    app.worldRenderer.effects.queueBuildingStatus(v, items); // fire, red cross, relocation marker
    if (v.prog >= 1.f && d.damageFx)
        queueDamageFx(v, items);
    if (d.extras)
        (this->*d.extras)(v, items);
    queueStatus(v, items);
}

// Scaffold while a pawn builds / repairs, and the critical-damage warning.
void BuildingRenderer::queueBody(const BuildingView &v, DrawList &items) const
{
    if ((v.prog < 1.f || v.working) && drawerOf(v.i.kind).siteFx)
        items.push_back({v.sortY() + 1.f, [=, this] { siteOverlay(v); }});

    if (v.crippled && v.prog >= 1.f)
        items.push_back({v.sortY() + 6.f, [=, this] { drawCrippledLabel(v); }});
}

void BuildingRenderer::queueDamageFx(const BuildingView &v, DrawList &items) const
{
    if (v.hpFrac < .6f)
        items.push_back({v.sortY() + 2.f, [=, this] { drawDamageSmoke(v); }});
    if (v.hpFrac < .3f)
        items.push_back({v.sortY() + 3.f, [=, this] { drawDamageFlames(v); }});
}

void BuildingRenderer::drawDamageSmoke(const BuildingView &v) const
{
    Anim d = mkAnim("Particle FX/Dust_02.png", 64, 64, 9, 32, 60);
    drawAnim(d, frameFor(d, v.t, float(v.id % 7)), v.cx - 20, v.bottom - 70, false, 1.8f, rgb(120, 120, 130, 210));
}

void BuildingRenderer::drawDamageFlames(const BuildingView &v) const
{
    Anim f = mkAnim("Particle FX/Fire_02.png", 64, 64, 12, 32, 62);
    drawAnim(f, frameFor(f, v.t, float(v.id % 5)), v.cx + 14, v.bottom - 46, false, 1.4f);
    drawAnim(f, frameFor(f, v.t, float(v.id % 3) + 4), v.cx - 22, v.bottom - 40, false, 1.1f);
}

void BuildingRenderer::queueStatus(const BuildingView &v, DrawList &items) const
{
    items.push_back({v.sortY() + 5.f, [=, this] { drawStatus(v); }});
}

void BuildingRenderer::drawBody(const BuildingView &v) const
{
    const BuildingDrawer &d = drawerOf(v.i.kind);

    if (d.underlay)
        (this->*d.underlay)(v);

    if (d.body)
        (this->*d.body)(v);
    else
        drawSpriteBody(v);
}

void BuildingRenderer::drawSpriteBody(const BuildingView &v) const
{
    BSpr s = buildingSprite(v.i.kind, v.i.aux);
    Texture2D bt = tex(std::string("Buildings/") + COLOR_NAME[v.col] + " Buildings/" + s.file + ".png");

    float dh = s.artH * s.scale * v.prog, sy = s.artH * (1.f - v.prog);
    float dw = s.artW * s.scale;
    Rectangle src{0, sy, s.artW, s.artH - sy};
    Rectangle dst{v.cx - dw / 2, v.bottom - 6 - s.artBottom * s.scale + (s.artH * s.scale - dh), dw, dh};

    drawSprite(bt, src, dst, v.tint, v.hover ? 1 : 0, v.oc);
}

// Last known state of an NPC town building outside vision.
void BuildingRenderer::drawLastSeen(const BuildingView &v) const
{
    textShadow("Last seen", int(v.cx) - MeasureText("Last seen", 14) / 2, int(v.bottom) + 6, 14, rgb(200, 205, 220));
}

void BuildingRenderer::drawCrippledLabel(const BuildingView &v) const
{
    float y = v.bottom - float(KINFO[v.i.kind].fh) * TILE - 46.f;
    DrawTexturePro(tex("UI Elements/UI Elements/Icons/Icon_10.png"), {0, 0, 64, 64}, {v.cx - 20, y - 20, 40, 40},
                   {0, 0}, 0, rgb(255, 110, 100));

    const char *s = "Critical damage: repair!";
    textShadow(s, int(v.cx) - MeasureText(s, 14) / 2, int(y + 22), 14, rgb(255, 140, 120));
}

void BuildingRenderer::drawStatus(const BuildingView &v) const
{
    const KindInfo &ki = KINFO[v.i.kind];
    Rectangle fr{float(v.tx) * TILE, float(v.ty) * TILE - 40, float(ki.fw) * TILE, float(ki.fh) * TILE + 40};
    if (v.selected)
        brackets(fr, rgb(255, 235, 90));

    drawProgressBars(v);

    if (v.prog >= 1.f && !(v.hasJob && v.job.kind != SJ_Build) && (v.hpFrac < .995f || v.selected || v.hover))
        drawBar({v.cx, v.bottom + 4}, 76, v.hpFrac, healthColor(v.hpFrac));

    if (const auto overlay = drawerOf(v.i.kind).overlay)
        (this->*overlay)(v);
}

// Construction percentage, or the upgrade / repair job progress.
void BuildingRenderer::drawProgressBars(const BuildingView &v) const
{
    bool waiting = v.hasJob && v.job.phase == SP_Waiting;

    if (v.prog < 1.f)
    {
        drawBar({v.cx, v.bottom + 4}, 70, v.prog, waiting ? BAR_IDLE : BAR_BUILD);
        std::string s = waiting ? "Waiting for a pawn" : TextFormat("Building %d%%", int(v.prog * 100.f));
        textShadow(s, int(v.cx) - MeasureText(s.c_str(), 14) / 2, int(v.bottom) + 18, 14,
                   waiting ? rgb(255, 170, 120) : rgb(200, 225, 255));
    }
    else if (v.hasJob && v.job.kind != SJ_Build)
    {
        const BJob &job = v.job;
        drawBar({v.cx, v.bottom + 4}, 76, float(job.pct) / 100.f,
                waiting ? BAR_IDLE : (job.kind == SJ_Upgrade ? BAR_UPGRADE : BAR_REPAIR));
        std::string s = waiting ? "Waiting for a pawn"
                                : TextFormat("%s %d%%", job.kind == SJ_Upgrade ? "Upgrading" : "Repairing", job.pct);
        textShadow(s, int(v.cx) - MeasureText(s.c_str(), 14) / 2, int(v.bottom) + 18, 14,
                   waiting ? rgb(255, 170, 120) : rgb(255, 225, 150));
    }
}

// Wooden scaffold in front of a building site, a bobbing hammer (construction / upgrade) or gear (repair) icon above
// it, pulsing while the job waits for a pawn, and dust while a pawn works.
void BuildingRenderer::siteOverlay(const BuildingView &v) const
{
    const KindInfo &ki = KINFO[v.i.kind];
    float cx = v.cx, bottom = v.bottom;
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

    bool waiting = v.hasJob && v.job.phase == SP_Waiting, repair = v.hasJob && v.job.kind == SJ_Repair;
    float bob = float(std::sin(v.t * 3.0 + double(v.id % 7))) * 4.f,
          s = waiting ? 34.f + float(std::sin(v.t * 6.0)) * 4.f : 34.f;
    Texture2D icon =
        tex(repair ? "UI Elements/UI Elements/Icons/Icon_10.png" : "UI Elements/UI Elements/Icons/Icon_01.png");
    DrawCircleV({cx, top - 26.f + bob}, s * .62f, waiting ? rgb(120, 40, 30, 200) : rgb(40, 30, 20, 190));
    DrawTexturePro(icon, {0, 0, 64, 64}, {cx - s / 2, top - 26.f + bob - s / 2, s, s}, {0, 0}, 0, WHITE);

    if (v.hasJob && v.job.phase == SP_Working)
    {
        Anim d = mkAnim("Particle FX/Dust_01.png", 64, 64, 8, 32, 60);
        drawAnim(d, frameFor(d, v.t, float(v.id % 4)), cx - float(ki.fw) * 20.f, bottom - 6, false, 1.2f,
                 rgb(255, 255, 255, 180));
    }
}
