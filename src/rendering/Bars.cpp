/**
 * @file Bars.cpp
 * @brief Health / progress bars and selection brackets.
 */
#include "rendering/Bars.hpp"

#include "rendering/Assets.hpp"

#include <algorithm>
#include <cmath>

Color healthColor(float f)
{
    f = std::clamp(f, 0.f, 1.f);
    auto mix = [](Color a, Color b, float t)
    {
        return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                     (unsigned char)(a.b + (b.b - a.b) * t), 255};
    };
    Color red{240, 50, 40, 255}, yellow{255, 215, 30, 255}, green{60, 225, 70, 255};
    return f < .5f ? mix(red, yellow, f / .5f) : mix(yellow, green, (f - .5f) / .5f);
}

void barRect(Rectangle r, float frac, Color fill)
{
    float x = std::round(r.x), y = std::round(r.y), w = std::round(r.width), h = std::max(3.f, std::round(r.height));
    DrawRectangleRec({x - 1, y - 1, w + 2, h + 2}, rgb(235, 235, 240, 235));
    DrawRectangleRec({x, y, w, h}, rgb(14, 12, 18));
    float fw = std::round(w * std::clamp(frac, 0.f, 1.f));
    if (fw <= 0)
        return;
    DrawRectangleRec({x, y, fw, h}, fill);
    DrawRectangleRec({x, y, fw, std::max(1.f, std::floor(h / 3))}, rgb(255, 255, 255, 90));
    DrawRectangleRec({x, y + h - 1, fw, 1}, rgb(0, 0, 0, 70));
}

void drawBar(Vector2 c, float w, float frac, Color fill)
{
    barRect({c.x - w / 2, c.y, w, std::clamp(w * .12f, 6.f, 10.f)}, frac, fill);
}

void brackets(Rectangle r, Color c)
{
    Texture2D t = tex("UI Elements/UI Elements/Cursors/Cursor_04.png");
    float s = .5f;
    auto q = [&](float sx, float sy, float dx, float dy)
    { DrawTexturePro(t, {sx, sy, 64, 64}, {dx, dy, 64 * s, 64 * s}, {0, 0}, 0, c); };
    q(0, 0, r.x - 2, r.y - 2);
    q(64, 0, r.x + r.width - 64 * s + 2, r.y - 2);
    q(0, 64, r.x - 2, r.y + r.height - 64 * s + 2);
    q(64, 64, r.x + r.width - 64 * s + 2, r.y + r.height - 64 * s + 2);
}

void clearBar(float cx, float by, float pct, bool working)
{
    drawBar({cx, by - 74.f}, 54.f, pct, working ? BAR_CLEAR : BAR_IDLE);
}
