#include "ui/BaseMenus.hpp"

#include "client/App.hpp"
#include "rendering/Text.hpp"
#include "ui/UiScale.hpp"

#include <cmath>

void BaseMenus::push(const Toast &t)
{
    toasts.push_back(t);
    if (toasts.size() > 4)
        toasts.erase(toasts.begin());
}

Rectangle BaseMenus::toastRect(int i) const { return {float(ui::screenW()) / 2 - 200, 52.f + 52.f * float(i), 400, 46}; }
Rectangle BaseMenus::homelessBanner() const { return {float(ui::screenW()) / 2 - 230, float(ui::screenH()) - 128, 460, 56}; }

bool BaseMenus::over(Vector2 m) const
{
    for (size_t i = 0; i < toasts.size(); i++)
        if (CheckCollisionPointRec(m, toastRect(int(i))))
            return true;
    if (app.world.homeless && CheckCollisionPointRec(m, homelessBanner()))
        return true;
    return false;
}

bool BaseMenus::input(Vector2 m)
{
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return false;
    for (size_t i = 0; i < toasts.size(); i++)
        if (CheckCollisionPointRec(m, toastRect(int(i))))
        {
            app.cam.target = {toasts[i].x * TILE, toasts[i].y * TILE};
            toasts.erase(toasts.begin() + long(i));
            return true;
        }
    if (app.world.homeless && CheckCollisionPointRec(m, homelessBanner()))
    {
        app.placement.begin(Placement::HomelessHall, K_TownHall, 0);
        return true;
    }
    return false;
}

void BaseMenus::draw()
{
    const ClientWorld &W = app.world;
    double now = GetTime();
    std::erase_if(toasts, [&](const Toast &t)
                  { return now - t.t > 14.0; });
    Vector2 m = ui::mouse();
    for (size_t i = 0; i < toasts.size(); i++)
    {
        const Toast &t = toasts[i];
        Rectangle r = toastRect(int(i));
        float a = std::clamp(float(14.0 - (now - t.t)) / 2.f, 0.f, 1.f), pulse = t.type == 0 ? float(std::sin(now * 8) * .5 + .5) : 0.f;
        bool hov = CheckCollisionPointRec(m, r);
        Color base = t.type == 0 ? rgb(150, 36, 30, int(235 * a)) : rgb(60, 80, 140, int(235 * a));
        DrawRectangleRounded(r, .2f, 6, hov ? rgb(base.r + 30, base.g + 20, base.b + 20, base.a) : base);
        DrawRectangleRoundedLinesEx(r, .2f, 6, 2 + pulse * 2, rgb(255, 210, 120, int(255 * a)));
        std::string s = t.text;
        while (MeasureText(s.c_str(), 15) > int(r.width) - 20 && s.size() > 4)
            s.pop_back();
        DrawText(s.c_str(), int(r.x) + 10, int(r.y) + 7, 15, rgb(255, 255, 255, int(255 * a)));
        std::string loc = TextFormat("(%d, %d)  click to jump", int(t.x), int(t.y));
        DrawText(loc.c_str(), int(r.x) + 10, int(r.y) + 27, 12, rgb(255, 230, 170, int(255 * a)));
    }
    if (W.homeless)
    {
        Rectangle b = homelessBanner();
        bool hov = CheckCollisionPointRec(m, b);
        DrawRectangleRounded(b, .2f, 6, hov ? rgb(190, 100, 40, 240) : rgb(160, 80, 30, 235));
        DrawRectangleRoundedLinesEx(b, .2f, 6, 2, rgb(255, 220, 140));
        DrawText("You are homeless", int(b.x) + 14, int(b.y) + 8, 18, WHITE);
        DrawText("Click here, then place a new Town Hall near your clan (free).", int(b.x) + 14, int(b.y) + 32, 13, rgb(255, 235, 200));
    }
    if (W.townMoving)
    {
        Rectangle nr = app.hud.nameRect();
        textShadow(TextFormat("Moving town: %d%%", W.movePct), int(nr.x) + 8, int(nr.y + nr.height) + 6, 15, rgb(255, 220, 120));
    }
    int em = app.placement.effMode();
    if (em == Placement::HomelessHall || em == Placement::MoveTown || em == Placement::Relocate)
    {
        std::string h = em == Placement::HomelessHall ? "Place your new Town Hall" : (em == Placement::MoveTown ? "Place the moved Town Hall" : "Place the relocated building");
        textShadow(h, ui::screenW() / 2 - MeasureText(h.c_str(), 20) / 2, 16, 20, rgb(255, 230, 120));
    }
}
