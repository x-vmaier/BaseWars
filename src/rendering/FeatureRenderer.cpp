#include "rendering/FeatureRenderer.hpp"

#include "client/App.hpp"
#include "rendering/Bars.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/TerrainRenderer.hpp"
#include "rendering/Text.hpp"
#include "ui/CostRow.hpp"
#include "ui/UiScale.hpp"

#include <cmath>
#include <string>

void FeatureRenderer::frame()
{
    const ClientWorld &W = app.world;
    mineCtr.clear();
    W.reg.view<Info, Goal>().each([&](const Info &i, const Goal &g)
    {
        if (i.kind == K_Mine && i.owner == W.me)
            mineCtr.push_back({g.x, g.y});
    });

    if (app.placement.buildKind == int(K_Mine) && !app.overUI(ui::mouse()))
    {
        IV t = Placement::placeTile(K_Mine, GetScreenToWorld2D(::GetMousePosition(), app.cam));
        Pos c = footCenter(K_Mine, t.x, t.y);
        mineCtr.push_back({c.x, c.y});
    }
}

bool FeatureRenderer::goldNearMine(int x, int y) const
{
    for (auto &m : mineCtr)
    {
        float dx = float(x) + .5f - m.x, dy = float(y) + .5f - m.y;
        if (dx * dx + dy * dy <= GOLD_RADIUS * GOLD_RADIUS)
            return true;
    }
    return false;
}

void FeatureRenderer::goldGlints(float cx, float by, uint32_t h, double t, bool strong)
{
    if (strong)
    { // deposits that boost one of our mines glow clearly
        float pulse = .5f + .5f * std::sin(float(t) * 3.f + float(h % 9));
        DrawCircleGradient({cx, by - 30.f}, 34.f + 8.f * pulse, rgb(255, 225, 90, int(70 + 50 * pulse)),
                           rgb(255, 200, 40, 0));
    }

    int stars = strong ? 4 : 2;
    for (int i = 0; i < stars; i++)
    {
        float ph = float(t) * (strong ? 2.6f : 1.7f) + float((h >> (i * 3)) % 17) * .9f;
        float k = std::fmod(ph, 3.f) / 3.f; // each star blooms and fades on a loop
        float s = std::sin(k * 3.14159f);
        if (s <= .05f)
            continue;
        float ox = float(int((h >> (i * 4 + 2)) % 49) - 24), oy = -float(int((h >> (i * 5 + 1)) % 40)) - 6.f;
        float sz = (strong ? 13.f : 8.f) * s, x = cx + ox, y = by + oy;
        Color c = rgb(255, 250, 200, int(255 * s));
        DrawLineEx({x - sz, y}, {x + sz, y}, 2.f, c);
        DrawLineEx({x, y - sz}, {x, y + sz}, 2.f, c);
        DrawLineEx({x - sz * .55f, y - sz * .55f}, {x + sz * .55f, y + sz * .55f}, 1.5f, c);
        DrawLineEx({x - sz * .55f, y + sz * .55f}, {x + sz * .55f, y - sz * .55f}, 1.5f, c);
        DrawCircleV({x, y}, sz * .3f, rgb(255, 255, 255, int(220 * s)));
    }
}

namespace
{
// Sprite drawers per feature type; trees and bushes sway, rocks are static.
void drawTree(const FeatureView &fv, const char *a, const char *b, float w, float h, float anchorY)
{
    Anim a1 =
        mkAnim(std::string("Terrain/Resources/Wood/Trees/Tree") + (fv.h & 1 ? a : b) + ".png", w, h, 6, w / 2, anchorY);
    drawAnim(a1, frameFor(a1, fv.t, float(fv.h % 8)), fv.cx, fv.by, false, 1.f);
}

void drawPine(const FeatureView &fv)
{
    drawTree(fv, "1", "2", 192, 256, 238);
}

void drawOak(const FeatureView &fv)
{
    drawTree(fv, "3", "4", 192, 192, 168);
}

void drawBush(const FeatureView &fv)
{
    Anim a =
        mkAnim(std::string("Terrain/Decorations/Bushes/Bushe") + char('1' + fv.h % 4) + ".png", 128, 128, 5, 64, 78);
    drawAnim(a, frameFor(a, fv.t, float(fv.h % 8)), fv.cx, fv.by, false, 1.f);
}

void drawRock(const FeatureView &fv)
{
    Texture2D r = tex(std::string("Terrain/Decorations/Rocks/Rock") + char('1' + fv.h % 4) + ".png");
    DrawTexturePro(r, {0, 0, 64, 64}, {fv.cx - 32, fv.by - 50, 64, 64}, {0, 0}, 0, WHITE);
}

void drawGoldStone(const FeatureView &fv)
{
    Anim a =
        mkAnim(std::string("Terrain/Resources/Gold/Gold Stones/Gold Stone ") + char('1' + fv.h % 6) + "_Highlight.png",
               128, 128, 6, 64, 78);
    drawAnim(a, frameFor(a, fv.t, float(fv.h % 6)), fv.cx, fv.by, false, 1.f);
}

using FeatureDrawFn = void (*)(const FeatureView &);

FeatureDrawFn featureDrawer(uint8_t f)
{
    static const FeatureDrawFn table[F_Gold + 1] = {nullptr, drawPine, drawOak, drawBush, drawRock, drawGoldStone};
    return f <= F_Gold ? table[f] : nullptr;
}
} // namespace

void FeatureRenderer::drawFeature(const FeatureView &fv) const
{
    if (fv.hover)
    { // hover ring at the foot of the obstacle
        DrawEllipse(int(fv.cx), int(fv.by - 4), 30, 13, rgb(255, 240, 160, 60));
        DrawEllipseLines(int(fv.cx), int(fv.by - 4), 30, 13, rgb(255, 240, 160, 230));
    }

    if (FeatureDrawFn draw = featureDrawer(fv.f))
        draw(fv);

    if (fv.f == F_Gold)
        goldGlints(fv.cx, fv.by, fv.h, fv.t, fv.nearMine); // sparkling deposits (stronger near our mines)
    if (fv.jp >= 0.f)
        clearBar(fv.cx, fv.by, fv.jp, fv.js == 2);
}

void FeatureRenderer::queue(const TerrainView &v, int x0, int y0, int x1, int y1, DrawList &items, double t)
{
    const ClientWorld &W = app.world;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
        {
            int b = v.B(x, y);
            if (!isLand(b) || !W.exploredAt(x, y) || isCliffTile(b, v.B(x, y - 1)))
                continue;

            uint8_t f = featureAt(x, y, b, W.seed);
            if (!f || W.clearedT.count(chunkKey(x, y)))
                continue; // cleared features are gone

            if (W.occ.count(chunkKey(x, y)))
                continue; // only the building's own tiles hide features (neighbours must stay clickable/visible)

            FeatureView fv = makeView(f, x, y, t);
            if (fv.hover)
                queueTooltip(f, x, y, fv.cx, fv.by, items);

            items.push_back({fv.by, [=, this] { drawFeature(fv); }});
        }
}

FeatureView FeatureRenderer::makeView(uint8_t f, int x, int y, double t) const
{
    const ClientWorld &W = app.world;
    FeatureView fv{};
    fv.f = f;
    fv.t = t;
    fv.h = hash2(x, y, W.seed + 31);
    fv.cx = (float(x) + .5f) * TILE + float(int(fv.h & 15) - 8);
    fv.by = (float(y) + 1.f) * TILE - 6 - float((fv.h >> 4) & 7);
    fv.jp = -1.f; // clear job: progress bar, shake only while a pawn works on it

    if (auto jit = W.cjobs.find(chunkKey(x, y)); jit != W.cjobs.end())
    {
        fv.jp = jit->second.pct;
        fv.js = jit->second.state;
        if (fv.js == 2)
            fv.cx += std::sin(float(t) * 46.f + float(fv.h % 7)) * 3.f;
    }

    fv.nearMine = f == F_Gold && goldNearMine(x, y);
    fv.hover = app.input.hasFeat && app.input.hoverFeat.x == x && app.input.hoverFeat.y == y;
    return fv;
}

// name, cost, work time and yield of the hovered obstacle (+ the state of its clear job). Cost and yield are drawn as
// [icon][value] items after their label, sized with the zoom-compensated font.
void FeatureRenderer::queueTooltip(uint8_t f, int x, int y, float cx, float by, DrawList &items)
{
    const ClientWorld &W = app.world;

    std::vector<TLine> lines{{featName(f)}};
    if (auto it = W.cjobs.find(chunkKey(x, y)); it != W.cjobs.end())
        lines.push_back({it->second.state == 2
                             ? TextFormat("Being cleared: %d%%", int(it->second.pct * 100.f))
                             : (it->second.state == 1 ? "A pawn is on the way" : "Waiting for a free pawn")});
    else
    {
        lines.push_back({"Cost:", R_Wood, float(clearCostWood(f)), true});
        lines.push_back({TextFormat("~%.1f s of pawn work", double(clearBaseSeconds(f)))});
        lines.push_back({"Yields:", dropRes(f), float(dropAmount(f)), false});
        lines.push_back({featureBlocks(f) ? "Blocks building" : "Does not block building"});
        lines.push_back({"Right-click: selected pawns (or the nearest free pawn) clear it"});
    }

    float zoom = std::max(.2f, app.cam.zoom);
    int sz = std::clamp(int(16.f / zoom), 10, 64), lh = sz + sz / 2;
    TipBox tip{std::move(lines), sz, lh, zoom, cx, by, {}};
    std::copy(W.res, W.res + R_Count, tip.have);
    items.push_back({1e9f - 1.f, [=] { drawTooltipBox(tip); }});
}

void FeatureRenderer::drawTooltipBox(const TipBox &tip)
{
    const auto &lines = tip.lines;
    int sz = tip.sz, lh = tip.lh;
    float zoom = tip.zoom, cx = tip.cx, by = tip.by;
    const float *have = tip.have;
    ui::CostStyle st = ui::darkCost(float(sz) / 14.f);
    st.fs = sz;
    auto lineW = [&](const TLine &l)
    {
        float w = float(MeasureText(l.text.c_str(), sz));
        if (l.res >= 0)
        {
            float c[R_Count]{};
            c[l.res] = l.amt;
            w += float(sz) / 2 + ui::measureCosts(c, 1e9f, st).x;
        }
        return w;
    };
    float w = 0;
    for (auto &l : lines)
        w = std::max(w, lineW(l));
    int h = lh * int(lines.size()) + sz / 2, x0 = int(cx - w / 2) - sz / 2, y0 = int(by) - 100 - h - int(40.f / zoom);
    DrawRectangle(x0, y0, int(w) + sz, h, rgb(18, 16, 24, 215));
    DrawRectangleLines(x0, y0, int(w) + sz, h, rgb(255, 235, 170, 200));
    for (size_t i = 0; i < lines.size(); i++)
    {
        const TLine &l = lines[i];
        int ty = y0 + sz / 4 + lh * int(i);
        textShadow(l.text, x0 + sz / 2, ty, sz, i == 0 ? rgb(255, 230, 140) : rgb(235, 235, 240));
        if (l.res < 0)
            continue;
        float c[R_Count]{};
        c[l.res] = l.amt;
        ui::CostStyle ls = st;
        if (!l.cost)
            ls.ok = rgb(160, 255, 160); // a yield, not a price
        ui::drawCosts(c, float(x0 + sz / 2 + MeasureText(l.text.c_str(), sz)) + float(sz) / 2,
                      float(ty) - (std::max(ls.icon, float(sz)) - float(sz)) / 2, 1e9f, l.cost ? have : nullptr, ls);
    }
}
