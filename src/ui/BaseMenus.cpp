/**
 * @file BaseMenus.cpp
 * @brief Base-mechanics HUD: clan alert toasts (click to jump), homeless banner, town-move progress, placement-mode
 * banner.
 */
#include "ui/BaseMenus.hpp"

#include "client/App.hpp"
#include "rendering/Text.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

#include <cmath>

void BaseMenus::load()
{
    mPaper = loadTMap("pause_menu");
}

void BaseMenus::push(const Toast &t)
{
    toasts.push_back(t);
    if (toasts.size() > 4)
        toasts.erase(toasts.begin());
}

Rectangle BaseMenus::toastRect(int i) const
{
    return {float(ui::screenW()) / 2 - 200, 52.f + 52.f * float(i), 400, 46};
}
Rectangle BaseMenus::homelessBanner() const
{
    return {float(ui::screenW()) / 2 - 230, float(ui::screenH()) - 128, 460, 56};
}

Rectangle BaseMenus::defeatPanel() const
{
    bool offer = app.world.defeat.phase == DF_Offered;
    float w = 440, h = offer ? 120.f : 190.f;
    return {float(ui::screenW()) / 2 - w / 2, offer ? float(ui::screenH()) - h - 24 : 72.f, w,
            h}; // top: the burning ruin stays visible below
}
Rectangle BaseMenus::defeatButton(int i) const
{
    Rectangle p = defeatPanel();
    bool offer = app.world.defeat.phase == DF_Offered;
    if (!offer)
        return {p.x + p.width / 2 - 90, p.y + p.height - 62, 180, 40};
    return {p.x + 30 + float(i == 0 ? 0 : 1) * 200, p.y + p.height - 56, 180, 40};
}

bool BaseMenus::over(Vector2 m) const
{
    if (app.world.defeat.phase != DF_None && CheckCollisionPointRec(m, defeatPanel()))
        return true;
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
    if (int ph = app.world.defeat.phase; ph != DF_None)
    {
        if (CheckCollisionPointRec(m, defeatButton(0)))
            app.cmd.resettle();
        else if (ph == DF_Offered && CheckCollisionPointRec(m, defeatButton(1)))
            app.cmd.resettleAccept();
        return CheckCollisionPointRec(m, defeatPanel());
    }
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
    std::erase_if(toasts, [&](const Toast &t) { return now - t.t > 14.0; });
    Vector2 m = ui::mouse();

    for (size_t i = 0; i < toasts.size(); i++)
        drawToast(int(i), m, now);
    if (W.defeat.phase == DF_Defeated)
        drawDefeat(m);
    else if (W.defeat.phase == DF_Offered)
        drawOffer(m);
    if (W.homeless)
        drawHomeless(m);
    if (W.townMoving)
    {
        Rectangle nr = app.hud.nameRect();
        textShadow(TextFormat("Moving town: %d%%", W.movePct), int(nr.x) + 8, int(nr.y + nr.height) + 6, 15,
                   rgb(255, 220, 120));
    }
    drawPlacementBanner();
}

void BaseMenus::drawToast(int i, Vector2 m, double now)
{
    const Toast &t = toasts[size_t(i)];
    Rectangle r = toastRect(i);
    float a = std::clamp(float(14.0 - (now - t.t)) / 2.f, 0.f, 1.f),
          pulse = t.type == 0 ? float(std::sin(now * 8) * .5 + .5) : 0.f;
    bool hov = CheckCollisionPointRec(m, r);
    Color base = t.type == 0 ? rgb(150, 36, 30, int(235 * a)) : rgb(60, 80, 140, int(235 * a));
    DrawRectangleRounded(r, .2f, 6, hov ? rgb(base.r + 30, base.g + 20, base.b + 20, base.a) : base);
    DrawRectangleRoundedLinesEx(r, .2f, 6, 2 + pulse * 2, rgb(255, 210, 120, int(255 * a)));

    std::string s = t.text;
    while (measureText(s.c_str(), 15) > int(r.width) - 20 && s.size() > 4)
        s.pop_back();
    drawText(s.c_str(), int(r.x) + 10, int(r.y) + 7, 15, rgb(255, 255, 255, int(255 * a)));
    std::string loc = TextFormat("(%d, %d)  click to jump", int(t.x), int(t.y));
    drawText(loc.c_str(), int(r.x) + 10, int(r.y) + 27, 12, rgb(255, 230, 170, int(255 * a)));
}

void BaseMenus::drawDefeat(Vector2 m)
{
    const ClientWorld::DefeatView &d = app.world.defeat;
    Rectangle r = defeatPanel();
    drawTMap(mPaper, r);
    ui::panelTitle(r, "Defeated", 22, .55f);

    std::string by = d.conqueror.empty() ? "Your Town Hall has fallen." : "Conquered by " + d.conqueror;
    if (!d.clan.empty())
        by += "  [" + d.clan + "]";
    int fs = 17;
    while (measureText(by.c_str(), fs) > int(r.width) - 50 && fs > 11)
        fs--;
    drawText(by.c_str(), int(r.x + r.width / 2) - measureText(by.c_str(), fs) / 2, int(r.y) + 62, fs, rgb(70, 45, 30));
    const char *hint = "Your town is burning. Found a new one elsewhere.";
    drawText(hint, int(r.x + r.width / 2) - measureText(hint, 14) / 2, int(r.y) + 92, 14, rgb(90, 62, 44));
    Rectangle b = defeatButton(0);
    ui::drawButton(b, "Resettle", true, false, CheckCollisionPointRec(m, b), false, 17);
}

void BaseMenus::drawOffer(Vector2 m)
{
    const ClientWorld::DefeatView &d = app.world.defeat;
    Rectangle r = defeatPanel();
    drawTMap(mPaper, r);
    int left = std::max(0, int(std::ceil(d.offerEndsAt - GetTime())));
    std::string t = "New town site offered - auto-accepted in " + std::to_string(left) + " s";
    drawText(t.c_str(), int(r.x + r.width / 2) - measureText(t.c_str(), 15) / 2, int(r.y) + 26, 15, rgb(70, 45, 30));
    Rectangle a = defeatButton(1), n = defeatButton(0);
    ui::drawButton(n, "Resettle (next site)", true, true, CheckCollisionPointRec(m, n), false, 15);
    ui::drawButton(a, "Accept", true, false, CheckCollisionPointRec(m, a), false, 17);
    drawOfferSite();
}

// Footprint outline of the offered Town Hall (4x3 tiles around the site centre).
void BaseMenus::drawOfferSite() const
{
    const ClientWorld::DefeatView &d = app.world.defeat;
    const KindInfo &ki = KINFO[K_TownHall];
    Vector2 a = GetWorldToScreen2D({(d.site.x - 2.f) * TILE, (d.site.y - 1.f) * TILE}, app.cam);
    float z = app.cam.zoom / ui::scale(), sc = ui::scale(); // the UI layer is drawn under the scale matrix
    a = {a.x / sc, a.y / sc};
    float pulse = float(std::sin(GetTime() * 4) * .5 + .5);
    Rectangle fp{a.x, a.y, float(ki.fw) * TILE * z, float(ki.fh) * TILE * z};
    DrawRectangleRec(fp, rgb(120, 220, 120, int(50 + 50 * pulse)));
    DrawRectangleLinesEx(fp, 3, rgb(200, 255, 190));
    const char *label = "New Town Hall";
    textShadow(label, int(fp.x + fp.width / 2) - measureText(label, 16) / 2, int(fp.y) - 24, 16, rgb(255, 245, 190));
}

void BaseMenus::drawHomeless(Vector2 m)
{
    Rectangle b = homelessBanner();
    bool hov = CheckCollisionPointRec(m, b);
    DrawRectangleRounded(b, .2f, 6, hov ? rgb(190, 100, 40, 240) : rgb(160, 80, 30, 235));
    DrawRectangleRoundedLinesEx(b, .2f, 6, 2, rgb(255, 220, 140));
    drawText("You are homeless", int(b.x) + 14, int(b.y) + 8, 18, WHITE);
    drawText("Click here, then place a new Town Hall near your clan (free).", int(b.x) + 14, int(b.y) + 32, 13,
             rgb(255, 235, 200));
}

void BaseMenus::drawPlacementBanner()
{
    int em = app.placement.effMode();
    if (em != Placement::HomelessHall && em != Placement::MoveTown && em != Placement::Relocate)
        return;

    std::string h = em == Placement::HomelessHall
                        ? "Place your new Town Hall"
                        : (em == Placement::MoveTown ? "Place the moved Town Hall" : "Place the relocated building");
    textShadow(h, ui::screenW() / 2 - measureText(h.c_str(), 20) / 2, 16, 20, rgb(255, 230, 120));
}
