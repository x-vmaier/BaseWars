/**
 * @file EffectsRenderer.cpp
 * @brief Transient effects: fx events, drops, ghosts, markers, building status and land clouds.
 */
#include "rendering/EffectsRenderer.hpp"

#include "client/App.hpp"
#include "rendering/FxDrawers.hpp"
#include "rendering/Palette.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/TerrainRenderer.hpp"
#include "rendering/Text.hpp"
#include "rendering/UnitAnims.hpp"

#include <cmath>
#include <string>

namespace
{
void drawMarker(const Marker &m)
{
    float a = float(GetTime() - m.t);
    DrawCircleLines(int(m.x), int(m.y), 6 + a * 40, rgb(255, 255, 255, int(255 * std::max(0.f, 1 - a * 2))));
}

void drawGhost(const Ghost &gh, float prog)
{
    float seedv = gh.x * 3.1f + gh.y;

    if (isBuilding(gh.kind))
    {
        const KindInfo &ki = KINFO[gh.kind];
        BSpr s = buildingSprite(gh.kind, 1);
        if (!s.file[0])
            return;

        float cx = gh.x * TILE, bottom = (gh.y + float(ki.fh) * .5f) * TILE;
        Texture2D bt =
            tex(std::string("Buildings/") + COLOR_NAME[colorIdx(gh.owner)] + " Buildings/" + s.file + ".png");
        float dw = s.artW * s.scale, dh = s.artH * s.scale;
        drawSprite(bt, {0, 0, s.artW, s.artH}, {cx - dw / 2, bottom - 6 - s.artBottom * s.scale, dw, dh}, WHITE, 2,
                   WHITE, prog, seedv);
    }
    else if (gh.kind != K_Ship)
    {
        Anim a = unitAnim(gh.kind, gh.owner, US_Idle, 0, 0, false, 1, 0);
        drawAnim(a, 0, gh.x * TILE, gh.y * TILE + 6, gh.left, 1.f, WHITE, 2, WHITE, prog, seedv);
    }

    Anim d = mkAnim("Particle FX/Dust_02.png", 64, 64, 8, 32, 60);
    drawAnim(d, int(prog * 9.f) % d.n, gh.x * TILE, gh.y * TILE - prog * 50.f, false, 2.2f,
             rgb(70, 70, 76, int(200 * (1 - prog))));
}

void drawDrop(const DropFx &f, double age)
{
    static const char *spr[R_Count] = {"Terrain/Resources/Wood/Wood Resource/Wood Resource.png",
                                       "Terrain/Resources/Gold/Gold Resource/Gold_Resource.png",
                                       "Terrain/Decorations/Rocks/Rock1.png",
                                       "Terrain/Resources/Meat/Meat Resource/Meat Resource.png"};
    static const Color txt[R_Count] = {rgb(205, 150, 90), rgb(255, 225, 80), rgb(200, 205, 215), rgb(255, 150, 140)};

    Texture2D tx = tex(spr[f.res]);
    int pieces = std::clamp(1 + f.amt / 5, 1, 4);
    float life = float(age) / 1.6f;

    for (int i = 0; i < pieces; i++)
    {
        float ang = float(i) * 2.4f + f.x * 1.7f, spread = std::min(1.f, float(age) * 4.f) * (14.f + 10.f * float(i));
        float ph = float(age) * 6.5f + float(i) * .9f;
        float hop = std::fabs(std::sin(ph)) * 30.f * std::max(0.f, 1.f - life * 1.1f);
        float px = f.x * TILE + std::cos(ang) * spread, py = f.y * TILE + std::sin(ang) * spread * .45f;
        int a = int(255 * std::clamp((1.f - life) * 3.f, 0.f, 1.f));

        DrawEllipse(int(px), int(py + 4), 10, 4, rgb(0, 0, 0, a / 4));
        if (tx.id)
            DrawTexturePro(tx, {0, 0, float(tx.width), float(tx.height)}, {px - 20, py - 34 - hop, 40, 40}, {0, 0}, 0,
                           rgb(255, 255, 255, a));
    }

    std::string s = "+" + std::to_string(f.amt);
    int fs = 20, w = MeasureText(s.c_str(), fs);
    float ty = f.y * TILE - 36.f - float(age) * 34.f;
    int a = int(255 * std::clamp((1.f - life) * 2.5f, 0.f, 1.f));
    Color c = txt[f.res];
    c.a = uint8_t(a);
    textShadow(s, int(f.x * TILE) - w / 2, int(ty), fs, c);
}

void drawBuildingFire(const BuildingView &v, int fw, int fh)
{
    float cx = v.cx, bottom = v.bottom, w = float(fw) * TILE, h = float(fh) * TILE;
    int id = int(v.id);
    Anim f = mkAnim("Particle FX/Fire_02.png", 64, 64, 12, 32, 62),
         f1 = mkAnim("Particle FX/Fire_01.png", 64, 64, 12, 32, 62);

    BeginBlendMode(BLEND_ADDITIVE);
    DrawCircleGradient({cx, bottom - h * .55f}, w * .55f, rgb(255, 120, 30, 90), rgb(255, 60, 0, 0));
    EndBlendMode();

    for (int k = 0; k < 4 + fw; k++)
    {
        float fx2 = cx - w * .38f + w * .76f * float(k) / float(3 + fw),
              fy = bottom - 20.f - h * .25f - float((k * 37 + id) % 40);
        drawAnim((k & 1) ? f : f1, frameFor(f, v.t, float(k) * .37f + float(id % 5)), fx2, fy, false,
                 1.5f + float(k % 3) * .35f);
    }

    Anim d = mkAnim("Particle FX/Dust_02.png", 64, 64, 9, 32, 60);
    for (int k = 0; k < 3; k++)
    {
        float ph = float(std::fmod(v.t * .6 + double(k) * .33 + double(id % 9) * .1, 1.0));
        drawAnim(d, int(ph * 9.f) % 9, cx - w * .25f + float(k) * w * .25f, bottom - h - 50.f - ph * 70.f, false, 2.1f,
                 rgb(70, 70, 76, int(180 * (1.f - ph))));
    }
}

// Red cross: this building does not work (outside any territory).
void drawNoTerritory(const BuildingView &v, int fh)
{
    float cx = v.cx, h = float(fh) * TILE, y = v.bottom - h * .5f - 40.f, r = 24.f + float(std::sin(v.t * 4.0)) * 1.5f;
    bool mov = v.moving;

    DrawCircleV({cx, y}, r + 3.f, rgb(255, 255, 255, mov ? 90 : 235));
    DrawCircleV({cx, y}, r, rgb(40, 20, 20, mov ? 90 : 210));
    DrawLineEx({cx - r * .62f, y - r * .62f}, {cx + r * .62f, y + r * .62f}, 9, rgb(230, 40, 40, mov ? 110 : 255));
    DrawLineEx({cx - r * .62f, y + r * .62f}, {cx + r * .62f, y - r * .62f}, 9, rgb(230, 40, 40, mov ? 110 : 255));
    if (!mov)
    {
        std::string s = "No territory";
        textShadow(s, int(cx) - MeasureText(s.c_str(), 12) / 2, int(y + r + 6), 12, rgb(255, 150, 130));
    }
}

void drawRelocating(const BuildingView &v, int fh)
{
    std::string s = v.i.kind == K_TownHall ? "Moving..." : "Relocating...";
    textShadow(s, int(v.cx) - MeasureText(s.c_str(), 14) / 2, int(v.bottom - float(fh) * TILE * .5f - 60.f), 14,
               rgb(210, 180, 255));
}
} // namespace

void EffectsRenderer::queueMarkers(DrawList &items)
{
    auto &markers = app.world.markers;
    for (auto &mk : markers)
    {
        Marker m = mk;
        items.push_back({-1e9f, [=] { drawMarker(m); }});
    }
    std::erase_if(markers, [](const Marker &m) { return GetTime() - m.t > .5; });
}

// destroyed things burn away into ash
void EffectsRenderer::queueGhosts(DrawList &items, double t)
{
    auto &ghosts = app.world.ghosts;
    for (auto &g : ghosts)
    {
        float prog = float((t - g.t) / 1.5);
        if (prog >= 1.f)
            continue;

        Ghost gh = g;
        items.push_back({gh.y * TILE + 2, [=] { drawGhost(gh, prog); }});
    }

    std::erase_if(ghosts, [&](const Ghost &g) { return t - g.t > 1.6; });
}

void EffectsRenderer::queueFx(DrawList &items, double t)
{
    auto &fxs = app.world.fxs;
    for (auto &f : fxs)
    {
        const FxDrawer *d = fxDrawer(f.type);
        double age = t - f.t;
        if (!d || (d->visible && !d->visible(f, age)))
            continue;

        Fx fx = f;
        float y = (d->sortAtTarget ? f.y2 : f.y) * TILE + d->sortDy;
        items.push_back({y, [=] { d->draw(fx, age, t); }});
    }

    std::erase_if(fxs, [&](const Fx &f) { return t - f.t > (f.type == E_Explosion ? 1.6 : 1.0); });
}

// bouncing resource drops
void EffectsRenderer::queueDrops(DrawList &items, double t)
{
    auto &drops = app.world.drops;
    for (auto &d : drops)
    {
        double age = t - d.t;
        if (age < 0 || age > 1.6)
            continue;

        DropFx f = d;
        items.push_back({f.y * TILE + 60.f, [=] { drawDrop(f, age); }});
    }

    std::erase_if(drops, [&](const DropFx &d) { return t - d.t > 1.7; });
}

// fire, red cross (outside territory), relocation marker
void EffectsRenderer::queueBuildingStatus(const BuildingView &v, DrawList &items)
{
    int fw = KINFO[v.i.kind].fw, fh = KINFO[v.i.kind].fh;
    bool burning = v.i.state & BLD_BURNING, dis = (v.i.state & BLD_DISABLED) != 0;

    if (burning)
        items.push_back({v.sortY() + 3.f, [=] { drawBuildingFire(v, fw, fh); }});
    if (dis)
        items.push_back({v.sortY() + 6.f, [=] { drawNoTerritory(v, fh); }});
    if (v.moving && !dis)
        items.push_back({v.sortY() + 6.f, [=] { drawRelocating(v, fh); }});
}

// drifting clouds over DISCOVERED land: sparse grid cells, hashed presence, continuous eastward drift, translucent
void EffectsRenderer::drawLandClouds(const TerrainView &v, float t)
{
    constexpr float CELL = 7.f, DRIFT = .42f;
    float shift = DRIFT * t;
    int gx0 = int(std::floor((float(v.x0) - 6.f - shift) / CELL)),
        gx1 = int(std::floor((float(v.x1) + 6.f - shift) / CELL));
    int gy0 = int(std::floor((float(v.y0) - 4.f) / CELL)), gy1 = int(std::floor((float(v.y1) + 4.f) / CELL));
    for (int gy = gy0; gy <= gy1; gy++)
        for (int gx = gx0; gx <= gx1; gx++)
        {
            uint32_t h = hash2(gx, gy, 7331);
            if (h % 100 >= 22)
                continue; // low density
            float cx = float(gx) * CELL + float(h >> 8 & 7) + shift, cy = float(gy) * CELL + float(h >> 12 & 7);
            int tx = int(std::floor(cx)), ty = int(std::floor(cy));
            if (!v.E(tx, ty) || !isLand(v.B(tx, ty)))
                continue; // only over land we have discovered
            Texture2D ct = tex(std::string("Terrain/Decorations/Clouds/Clouds_0") + char('1' + (h >> 16) % 8) + ".png");
            float sc = .8f + float((h >> 20) & 15) / 40.f, w = 576.f * sc, hh = 256.f * sc;
            int alpha = 60 + int((h >> 24) % 31); // 60..90
            DrawEllipse(int(cx * TILE + 40), int(cy * TILE + 120), w * .30f, hh * .16f,
                        rgb(15, 25, 45, 20)); // soft ground shadow
            DrawTexturePro(ct, {0, 0, 576, 256}, {cx * TILE - w / 2, cy * TILE - hh / 2, w, hh}, {0, 0}, 0,
                           rgb(255, 255, 255, alpha));
        }
}
