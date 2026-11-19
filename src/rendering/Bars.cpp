#include "rendering/Bars.hpp"

#include "rendering/Assets.hpp"

#include <algorithm>

void drawBar(Vector2 c, float w, float frac, Color fill) {
    Texture2D base = tex("UI Elements/UI Elements/Bars/SmallBar_Base.png"), fl = tex("UI Elements/UI Elements/Bars/SmallBar_Fill.png");
    if (base.id == 0) { DrawRectangle(int(c.x - w / 2), int(c.y), int(w), 5, BLACK); DrawRectangle(int(c.x - w / 2), int(c.y), int(w * frac), 5, fill); return; }
    float s = w / 222.f, h = 19.f * s, x = c.x - w / 2, y = c.y;
    float cap = 15.f * s;
    DrawTexturePro(base, {49, 22, 15, 19}, {x, y, cap, h}, {0, 0}, 0, WHITE);
    DrawTexturePro(base, {128, 22, 64, 19}, {x + cap, y, w - 2 * cap, h}, {0, 0}, 0, WHITE);
    DrawTexturePro(base, {256, 22, 15, 19}, {x + w - cap, y, cap, h}, {0, 0}, 0, WHITE);
    float ix = x + 4 * s * 1.6f, iw = (w - 8 * s * 1.6f) * std::clamp(frac, 0.f, 1.f);
    if (iw > 0) DrawTexturePro(fl, {0, 30, 64, 3}, {ix, y + 5 * s, iw, h - 10 * s}, {0, 0}, 0, fill);
}
void brackets(Rectangle r, Color c) {
    Texture2D t = tex("UI Elements/UI Elements/Cursors/Cursor_04.png");
    float s = .5f;
    auto q = [&](float sx, float sy, float dx, float dy) { DrawTexturePro(t, {sx, sy, 64, 64}, {dx, dy, 64 * s, 64 * s}, {0, 0}, 0, c); };
    q(0, 0, r.x - 2, r.y - 2);
    q(64, 0, r.x + r.width - 64 * s + 2, r.y - 2);
    q(0, 64, r.x - 2, r.y + r.height - 64 * s + 2);
    q(64, 64, r.x + r.width - 64 * s + 2, r.y + r.height - 64 * s + 2);
}
void clearBar(float cx, float by, float pct, bool working) {
    float w = 54.f, x = cx - w / 2, y = by - 74.f;
    DrawRectangle(int(x) - 2, int(y) - 2, int(w) + 4, 11, rgb(20, 14, 10, 200));
    DrawRectangle(int(x), int(y), int(w * std::clamp(pct, 0.f, 1.f)), 7, working ? rgb(250, 190, 60) : rgb(150, 150, 160));
    DrawRectangleLines(int(x) - 2, int(y) - 2, int(w) + 4, 11, rgb(255, 235, 170, 200));
}
