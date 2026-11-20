/**
 * @file CostRow.cpp
 * @brief Cost row widget: every resource requirement drawn as [resource icon][numeric value], with a fixed icon size,
 * padding and margin.
 */
#include "ui/CostRow.hpp"

#include "rendering/Assets.hpp"
#include "rendering/Text.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace ui
{
namespace
{
std::string valueText(float v)
{
    return std::to_string(int(std::ceil(v - 1e-3f)));
}
float itemW(float v, const CostStyle &st)
{
    return st.icon + st.gap + float(measureText(valueText(v).c_str(), st.fs));
}
float lineH(const CostStyle &st)
{
    return std::max(st.icon, float(st.fs));
}
} // namespace

CostStyle paperCost(float s)
{
    CostStyle st;
    st.icon = std::round(18 * s);
    st.gap = std::round(3 * s);
    st.margin = std::round(10 * s);
    st.lineGap = std::round(3 * s);
    st.fs = std::max(10, int(std::lround(14 * s)));
    return st;
}
CostStyle darkCost(float s)
{
    CostStyle st = paperCost(s);
    st.ok = rgb(255, 232, 150);
    st.bad = rgb(255, 115, 100);
    st.shadow = true;
    return st;
}

void resourceIcon(int r, Rectangle dst, Color tint)
{
    // source rectangles trimmed to the opaque art of each 64x64 icon, fitted into dst keeping the aspect ratio
    static const Rectangle src[R_Count] = {{1, 7, 62, 50}, {4, 4, 55, 52}, {7, 17, 50, 36}, {2, 9, 60, 48}};
    if (r < 0 || r >= R_Count)
        return;
    Texture2D t = r == R_Stone ? tex("Terrain/Decorations/Rocks/Rock2.png")
                               : tex(std::string("UI Elements/UI Elements/Icons/") +
                                     (r == R_Wood ? "Icon_02" : (r == R_Gold ? "Icon_03" : "Icon_04")) + ".png");
    Rectangle s = src[r];
    float k = std::min(dst.width / s.width, dst.height / s.height), w = s.width * k, h = s.height * k;
    if (t.id == 0)
    {
        DrawRectangleRec(dst, tint);
        return;
    }
    DrawTexturePro(t, s, {dst.x + (dst.width - w) / 2, dst.y + (dst.height - h) / 2, w, h}, {0, 0}, 0, tint);
}

Vector2 measureCosts(const float *cost, float maxW, const CostStyle &st)
{
    float x = 0, w = 0, lines = 1;
    bool any = false;
    for (int r = 0; r < R_Count; r++)
    {
        if (cost[r] <= 0)
            continue;
        float iw = itemW(cost[r], st);
        if (any && x + st.margin + iw > maxW)
        {
            lines++;
            x = 0;
            any = false;
        }
        x += (any ? st.margin : 0) + iw;
        any = true;
        w = std::max(w, x);
    }
    if (w == 0)
        w = float(measureText("Free", st.fs));
    return {w, lines * lineH(st) + (lines - 1) * st.lineGap};
}

Vector2 drawCosts(const float *cost, float x0, float y0, float maxW, const float *have, const CostStyle &st)
{
    float x = 0, y = 0, w = 0, lh = lineH(st);
    bool any = false, drew = false;
    for (int r = 0; r < R_Count; r++)
    {
        if (cost[r] <= 0)
            continue;
        std::string v = valueText(cost[r]);
        float iw = st.icon + st.gap + float(measureText(v.c_str(), st.fs));
        if (any && x + st.margin + iw > maxW)
        {
            y += lh + st.lineGap;
            x = 0;
            any = false;
        }
        x += any ? st.margin : 0;
        bool miss = have && have[r] + 1e-3f < cost[r];
        resourceIcon(r, {x0 + x, y0 + y + (lh - st.icon) / 2, st.icon, st.icon}, miss ? rgb(255, 170, 170) : WHITE);
        int tx = int(x0 + x + st.icon + st.gap), ty = int(y0 + y + (lh - float(st.fs)) / 2 + 1);
        Color c = miss ? st.bad : st.ok;
        if (st.shadow)
            textShadow(v, tx, ty, st.fs, c);
        else
            drawText(v.c_str(), tx, ty, st.fs, c);
        x += iw;
        w = std::max(w, x);
        any = drew = true;
    }
    if (!drew)
    {
        int ty = int(y0 + (lh - float(st.fs)) / 2 + 1);
        if (st.shadow)
            textShadow("Free", int(x0), ty, st.fs, st.ok);
        else
            drawText("Free", int(x0), ty, st.fs, st.ok);
        return {float(measureText("Free", st.fs)), lh};
    }
    return {w, y + lh};
}

Vector2 drawCost(int r, float amount, float x, float y, const float *have, const CostStyle &st)
{
    float c[R_Count]{};
    if (r >= 0 && r < R_Count)
        c[r] = amount;
    return drawCosts(c, x, y, 1e9f, have, st);
}

} // namespace ui
