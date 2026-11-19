#include "ui/BigMap.hpp"

#include "client/App.hpp"
#include "rendering/Palette.hpp"
#include "rendering/Text.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

#include <cmath>

void BigMap::load()
{
    mMapFrame = loadTMap("map_frame");
}

float BigMap::tpp() const
{
    static const float z[7] = {.125f, .25f, .5f, 1.f, 2.f, 4.f, 8.f};
    return z[std::clamp(zoom, 0, 6)];
}
void BigMap::open()
{
    isOpen = true;
    center = {app.cam.target.x / TILE, app.cam.target.y / TILE};
    last = 0;
    dragging = false;
}

Rectangle BigMap::frameRect() const
{
    Vector2 s = mMapFrame.size();
    float sw = float(ui::screenW()), sh = float(ui::screenH());
    float f = std::min({1.f, (sw - 30) / s.x, (sh - 30) / s.y});
    return {(sw - s.x * f) / 2, (sh - s.y * f) / 2, s.x * f, s.y * f};
}
Rectangle BigMap::inner() const
{
    Rectangle r = frameRect();
    float f = r.width / mMapFrame.size().x, in = 46.f * (mMapFrame.scale / .4f) * f;
    return {r.x + in, r.y + in, r.width - 2 * in, r.height - 2 * in};
}
Vector2 BigMap::mapToTile(Vector2 p) const
{
    Rectangle in = inner();
    return {center.x + (p.x - in.x - in.width / 2) * tpp(), center.y + (p.y - in.y - in.height / 2) * tpp()};
}
Vector2 BigMap::tileToMap(float tx, float ty) const
{
    Rectangle in = inner();
    return {in.x + in.width / 2 + (tx - center.x) / tpp(), in.y + in.height / 2 + (ty - center.y) / tpp()};
}

// input
void BigMap::input(Vector2 m)
{
    Rectangle in = inner(), fr = frameRect();
    if (menuInput(m))
        return;
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_N))
    {
        isOpen = false;
        return;
    }
    if (IsKeyPressed(KEY_P))
    {
        isOpen = false;
        app.social.isOpen = true;
        return;
    }
    float wheel = GetMouseWheelMove();
    if (wheel != 0 && CheckCollisionPointRec(m, in))
    {
        Vector2 before = mapToTile(m);
        zoom = std::clamp(zoom - (wheel > 0 ? 1 : -1), 0, 6);
        Vector2 after = mapToTile(m);
        center.x += before.x - after.x;
        center.y += before.y - after.y;
        last = 0;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (!CheckCollisionPointRec(m, fr))
        {
            isOpen = false;
            return;
        }
        if (CheckCollisionPointRec(m, in))
        {
            dragging = true;
            dragMouse = m;
            dragMoved = 0;
        }
    }
    if (dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        Vector2 d = {m.x - dragMouse.x, m.y - dragMouse.y};
        dragMoved += std::abs(d.x) + std::abs(d.y);
        center.x -= d.x * tpp();
        center.y -= d.y * tpp();
        dragMouse = m;
        last = 0;
    }
    if (dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        dragging = false;
        if (dragMoved < 5)
        {
            Vector2 t = mapToTile(m);
            app.travelTo(t.x, t.y);
        }
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !rightClick(m))
        isOpen = false;
}

// drawing
void BigMap::refresh()
{
    const ClientWorld &W = app.world;
    Rectangle in = inner();
    int nw = std::max(8, int(in.width / 2)), nh = std::max(8, int(in.height / 2));
    if (!texture.id || nw != w || nh != h)
    {
        if (texture.id)
            UnloadTexture(texture);
        Image im = GenImageColor(nw, nh, BLACK);
        texture = LoadTextureFromImage(im);
        UnloadImage(im);
        w = nw;
        h = nh;
        pix.assign(size_t(nw) * size_t(nh), BLACK);
        last = 0;
    }
    if (GetTime() - last < .35)
        return;
    last = GetTime();
    float k = tpp();
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
        {
            int tx = int(std::floor(center.x + (float(i * 2 + 1) - in.width / 2) * k)),
                ty = int(std::floor(center.y + (float(j * 2 + 1) - in.height / 2) * k));
            bool ex = W.exploredAt(tx, ty);
            Color c = ex ? biomeColor(W.biomeAt(tx, ty)) : rgb(20, 26, 38);
            if (!ex && ((i / 4 + j / 4) & 1))
                c = rgb(24, 30, 44);
            pix[size_t(j * w + i)] = c;
        }
    UpdateTexture(texture, pix.data());
}

void BigMap::draw()
{
    const ClientWorld &W = app.world;
    Vector2 m = ui::mouse();
    DrawRectangle(0, 0, ui::screenW(), ui::screenH(), rgb(0, 0, 0, 150));
    Rectangle fr = frameRect(), in = inner();
    drawTMap(mMapFrame, fr);
    refresh();
    DrawRectangleRec(in, rgb(16, 20, 30));
    if (texture.id)
        DrawTexturePro(texture, {0, 0, float(w), float(h)}, {in.x, in.y, float(w) * 2, float(h) * 2}, {0, 0}, 0, WHITE);
    auto inside = [&](Vector2 p, float pad)
    { return p.x > in.x + pad && p.y > in.y + pad && p.x < in.x + in.width - pad && p.y < in.y + in.height - pad; };
    app.mapOverlay.draw(in, center, tpp(), false);
    // far-away friends: an arrow on the map border pointing towards their town
    for (auto &p : W.roster)
    {
        if (p.id == W.me || !p.hasLoc || !(p.flags & 2 || (p.clan && p.clan == W.myClan())))
            continue;
        Vector2 sp = tileToMap(p.lx, p.ly);
        if (inside(sp, 0))
            continue;
        Vector2 c{in.x + in.width / 2, in.y + in.height / 2}, d{sp.x - c.x, sp.y - c.y};
        float k = std::min((in.width / 2 - 20) / std::max(1.f, std::abs(d.x)),
                           (in.height / 2 - 20) / std::max(1.f, std::abs(d.y)));
        Vector2 e{c.x + d.x * k, c.y + d.y * k};
        float ang = std::atan2(d.y, d.x);
        Color col = p.clan && p.clan == W.myClan() ? clanColor(p.clan) : rgb(150, 255, 170);
        Vector2 a1{e.x + std::cos(ang) * 12, e.y + std::sin(ang) * 12},
            a2{e.x + std::cos(ang + 2.5f) * 10, e.y + std::sin(ang + 2.5f) * 10},
            a3{e.x + std::cos(ang - 2.5f) * 10, e.y + std::sin(ang - 2.5f) * 10};
        DrawTriangle(a1, a3, a2, col);
        DrawTriangleLines(a1, a3, a2, BLACK);
        float dist = std::hypot(p.lx - center.x, p.ly - center.y);
        std::string lb = p.name + " " + std::to_string(int(dist)) + "m";
        float tx = std::clamp(e.x - float(MeasureText(lb.c_str(), 11)) / 2 - std::cos(ang) * 22, in.x + 4,
                              in.x + in.width - float(MeasureText(lb.c_str(), 11)) - 4);
        float ty = std::clamp(e.y - 6 - std::sin(ang) * 18, in.y + 4, in.y + in.height - 16);
        textShadow(lb, int(tx), int(ty), 11, col);
    }
    { // where the game camera currently looks
        Vector2 tl = GetScreenToWorld2D({0, 0}, app.cam),
                br = GetScreenToWorld2D({float(::GetScreenWidth()), float(::GetScreenHeight())}, app.cam);
        Vector2 a = tileToMap(tl.x / TILE, tl.y / TILE), b = tileToMap(br.x / TILE, br.y / TILE);
        float x0 = std::max(a.x, in.x), y0 = std::max(a.y, in.y), x1 = std::min(b.x, in.x + in.width),
              y1 = std::min(b.y, in.y + in.height);
        if (x1 > x0 && y1 > y0)
            DrawRectangleLinesEx({x0, y0, x1 - x0, y1 - y0}, 1, rgb(255, 255, 255, 220));
    }
    DrawRectangleLinesEx(in, 2, rgb(60, 40, 26));
    textShadow("WORLD MAP", int(fr.x + fr.width / 2) - 50, int(fr.y) + 10, 18, rgb(255, 240, 200));
    std::string hint = "Click: travel   Right-click ally: support patrol   Drag: pan   Wheel: zoom   N/Esc: close";
    textShadow(hint, int(fr.x + fr.width / 2) - MeasureText(hint.c_str(), 12) / 2, int(fr.y + fr.height) - 24, 12,
               rgb(255, 240, 200));
    if (CheckCollisionPointRec(m, in))
    {
        Vector2 t = mapToTile(m);
        std::string cs = TextFormat("%d, %d", int(std::floor(t.x)), int(std::floor(t.y)));
        DrawRectangleRounded({m.x + 14, m.y + 16, float(MeasureText(cs.c_str(), 14)) + 12, 22}, .3f, 4,
                             rgb(30, 22, 16, 225));
        DrawText(cs.c_str(), int(m.x) + 20, int(m.y) + 20, 14, WHITE);
    }
    drawMenu();
}

// support patrols (right-click an allied / clan town)
std::vector<BigMap::Cand> BigMap::candidates()
{
    const ClientWorld &W = app.world;
    std::vector<Cand> v;
    W.reg.view<Info, Goal>().each([&](const Info &i, const Goal &g)
    {
        if (!hasTag(i.kind, KT_Anchor) || i.owner == W.me || isFactionOwner(i.owner) || !W.alliedWith(i.owner) ||
            i.prog < 100)
            return;
        v.push_back({W.nameOf(i.owner) + (i.kind == K_TownHall ? " (town)" : " (outpost)"), g.x, g.y});
    });
    for (auto &p : W.roster)
    {
        if (p.id == W.me || !p.hasLoc || !(p.flags & 2 || (p.clan && p.clan == W.myClan())))
            continue;
        bool dup = false;
        for (auto &c : v)
            if (std::hypot(c.x - p.lx, c.y - p.ly) < 4.f)
                dup = true;
        if (!dup)
            v.push_back({p.name + " (town)", p.lx, p.ly});
    }
    return v;
}
Rectangle BigMap::menuRect() const
{
    float mw = 250, mh = 112;
    return {std::clamp(menu.at.x, 4.f, float(ui::screenW()) - mw - 4),
            std::clamp(menu.at.y, 4.f, float(ui::screenH()) - mh - 4), mw, mh};
}
Rectangle BigMap::minusBtn() const
{
    Rectangle r = menuRect();
    return {r.x + 130, r.y + 36, 28, 26};
}
Rectangle BigMap::plusBtn() const
{
    Rectangle r = menuRect();
    return {r.x + 212, r.y + 36, 28, 26};
}
Rectangle BigMap::sendBtn() const
{
    Rectangle r = menuRect();
    return {r.x + 10, r.y + 70, r.width - 20, 32};
}
bool BigMap::rightClick(Vector2 m)
{
    float bd = 20.f;
    Cand pick{};
    bool found = false;
    for (auto &c : candidates())
    {
        Vector2 sp = tileToMap(c.x, c.y);
        float d = std::hypot(sp.x - m.x, sp.y - m.y - 6.f);
        if (d < bd)
        {
            bd = d;
            pick = c;
            found = true;
        }
    }
    if (!found)
        return false;
    menu = {true, {m.x + 6, m.y + 6}, pick.x, pick.y, pick.name};
    return true;
}
bool BigMap::menuInput(Vector2 m)
{
    if (!menu.open)
        return false;
    if (IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
    {
        menu.open = false;
        return true;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(m, minusBtn()))
            patrolN = std::max(1, patrolN - 1);
        else if (CheckCollisionPointRec(m, plusBtn()))
            patrolN = std::min(MAX_QUOTA_UI, patrolN + 1);
        else if (CheckCollisionPointRec(m, sendBtn()))
        {
            app.cmd.support(menu.tx, menu.ty, patrolN);
            menu.open = false;
        }
        else if (!CheckCollisionPointRec(m, menuRect()))
            menu.open = false;
    }
    return true;
}
void BigMap::drawMenu()
{
    if (!menu.open)
        return;
    Vector2 m = ui::mouse();
    Rectangle r = menuRect();
    DrawRectangleRounded(r, .1f, 6, rgb(238, 222, 190, 250));
    DrawRectangleRoundedLinesEx(r, .1f, 6, 2, rgb(120, 90, 60));
    std::string t = menu.name;
    while (MeasureText(t.c_str(), 15) > int(r.width) - 20 && t.size() > 4)
        t.pop_back();
    DrawText(t.c_str(), int(r.x) + 10, int(r.y) + 9, 15, rgb(70, 45, 30));
    DrawText("Troops:", int(r.x) + 12, int(r.y) + 41, 15, rgb(70, 45, 30));
    ui::drawButton(minusBtn(), "-", true, false, CheckCollisionPointRec(m, minusBtn()), false, 16);
    DrawText(TextFormat("%d", patrolN), int(r.x) + 178 - MeasureText(TextFormat("%d", patrolN), 16) / 2, int(r.y) + 41,
             16, rgb(70, 45, 30));
    ui::drawButton(plusBtn(), "+", true, false, CheckCollisionPointRec(m, plusBtn()), false, 16);
    ui::drawButton(sendBtn(), TextFormat("Send support patrol (%d)", patrolN), true, false,
                   CheckCollisionPointRec(m, sendBtn()), false, 14);
}
