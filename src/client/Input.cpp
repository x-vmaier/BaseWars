#include "client/Input.hpp"

#include "client/App.hpp"
#include "ui/UiScale.hpp"

#include <cmath>

// Two mouse spaces: `rm` real pixels (camera, picking, dragging) and `m` UI space (panels and buttons, ui::mouse()).
void Input::update(float dt)
{
    Camera2D &cam = app.cam;
    Vector2 rm = ::GetMousePosition();
    Vector2 m = ui::mouse();
    float sw = float(::GetScreenWidth()), sh = float(::GetScreenHeight());
    cam.offset = {sw * .5f, sh * .5f};
    bool ui = app.overUI(m);

    // modal panels own the input while open
    if (app.pause.isOpen)
    {
        app.pause.input();
        return;
    }
    if (app.chat.isOpen)
    {
        app.chat.update(m);
        return;
    }
    if (app.baseMenus.input(m))
        return;
    if (app.bigMap.isOpen)
    {
        app.bigMap.input(m);
        return;
    }
    if (app.training.isOpen)
    {
        app.training.input(m);
        return;
    }
    if (app.townHall.isOpen)
    {
        app.townHall.input(m);
        return;
    }
    if (app.social.isOpen)
    {
        // the build menu stays usable beside the social menu
        bool lp = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        if (lp && app.buildMenu.over(m))
        {
            app.buildMenu.click(m);
            return;
        }
        if (lp && CheckCollisionPointRec(m, Hud::socialTabBtn()))
        {
            app.social.isOpen = false;
            app.social.textFocus = false;
            return;
        }
        if (!app.social.textFocus && IsKeyPressed(KEY_B))
        {
            app.buildMenu.isOpen = !app.buildMenu.isOpen;
            return;
        }
        if (!app.social.textFocus && app.buildMenu.isOpen)
            app.buildMenu.hotkeys();
        app.social.input(m);
        return;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
    {
        app.chat.open();
        return;
    }
    if (IsKeyPressed(KEY_N))
    {
        app.bigMap.open();
        return;
    }
    if (IsKeyPressed(KEY_P))
    {
        app.social.open();
        return;
    }
    camera(dt, rm, ui);
    if (hotkeys())
        return;

    hovered = ui || dragging ? 0 : pick(rm);
    hasFeat = !hovered && !ui && !dragging && !app.placement.active() && mode == M_None && pickFeature(rm, hoverFeat);
    bool lp = IsMouseButtonPressed(MOUSE_BUTTON_LEFT), lr = IsMouseButtonReleased(MOUSE_BUTTON_LEFT),
         rp = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (app.shipPanel.input(m, lp, rp))
        return;

    if (dragging && lr)
    {
        dragging = false;
        doSelect(dragStart, rm, shift);
        return;
    }
    if (app.context.isOpen)
        app.context.rebuild();
    if (app.context.sliderInput(m, lp, lr))
        return;
    if (lp)
    {
        if (app.context.isOpen && app.context.click(m))
            return;
        if (app.buildMenu.click(m))
            return;
        if (CheckCollisionPointRec(m, Hud::socialTabBtn()))
        {
            app.social.open();
            return;
        }
        if (CheckCollisionPointRec(m, app.minimap.rect()))
        {
            app.bigMap.open();
            return;
        }
        if (ui)
            return;
    }
    if (ui)
    {
        if (rp)
            app.context.close();
        return;
    }

    Vector2 w = GetScreenToWorld2D(rm, cam);
    if (lp)
    {
        if (app.placement.active())
            app.placement.click(w, shift);
        else
            worldClick(w, rm, shift);
    }
    if (rp)
        worldRightClick(rm, m, w);
}

void Input::camera(float dt, Vector2 rm, bool ui)
{
    Camera2D &cam = app.cam;
    float sw = float(::GetScreenWidth()), sh = float(::GetScreenHeight());
    float sp = 800.f * dt / cam.zoom;
    if (IsKeyDown(KEY_UP))
        cam.target.y -= sp;
    if (IsKeyDown(KEY_DOWN))
        cam.target.y += sp;
    if (IsKeyDown(KEY_LEFT))
        cam.target.x -= sp;
    if (IsKeyDown(KEY_RIGHT))
        cam.target.x += sp;
    if (IsCursorOnScreen() && !dragging && !ui)
    { // edge scrolling
        if (rm.x < 3)
            cam.target.x -= sp;
        if (rm.x > sw - 4)
            cam.target.x += sp;
        if (rm.y < 3)
            cam.target.y -= sp;
        if (rm.y > sh - 4)
            cam.target.y += sp;
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
    {
        Vector2 d = GetMouseDelta();
        cam.target.x -= d.x / cam.zoom;
        cam.target.y -= d.y / cam.zoom;
    }

    float wh = GetMouseWheelMove();
    if (wh != 0 && !ui)
    {
        Vector2 b = GetScreenToWorld2D(rm, cam);
        cam.zoom = std::clamp(cam.zoom * (1 + wh * .1f), .2f, 2.2f);
        Vector2 a = GetScreenToWorld2D(rm, cam);
        cam.target.x += b.x - a.x;
        cam.target.y += b.y - a.y;
    }
}

bool Input::hotkeys()
{
    Camera2D &cam = app.cam;
    Placement &pl = app.placement;

    if (IsKeyPressed(KEY_H))
    {
        Pos t = app.world.myTH(cam.target);
        cam.target = {t.x * TILE, t.y * TILE};
    }
    if (IsKeyPressed(KEY_B))
        app.buildMenu.isOpen = !app.buildMenu.isOpen;
    if (IsKeyPressed(KEY_M))
    {
        mode = mode == M_Move ? M_None : M_Move;
        pl.cancel();
    }
    if (IsKeyPressed(KEY_A))
    {
        mode = mode == M_Attack ? M_None : M_Attack;
        pl.cancel();
    }
    if (IsKeyPressed(KEY_ESCAPE))
    {
        if (pl.active() || mode != M_None || app.context.isOpen || !sel.empty())
        {
            pl.cancel();
            mode = M_None;
            app.context.close();
            sel.clear();
        }
        else
        {
            app.pause.isOpen = true;
            return true;
        }
    }

    if (app.buildMenu.isOpen)
        app.buildMenu.hotkeys();

    if (GetTime() - lastCam > 1.5 && app.session.connected)
    {
        lastCam = GetTime();
        app.cmd.camera(cam.target);
    }
    return false;
}

void Input::worldClick(Vector2 w, Vector2 rm, bool shift)
{
    if (mode == M_Move)
    {
        app.cmd.move(w);
        if (!shift)
            mode = M_None;
    }
    else if (mode == M_Attack)
    {
        if (hovered && app.world.attackable(hovered))
            app.cmd.attack(hovered);
        else
            app.cmd.attackMove(w);
        if (!shift)
            mode = M_None;
    }
    else if (hovered && app.world.hostileTargetId(hovered) && armyCountSel() > 0)
    {
        app.cmd.attack(hovered);
    }
    else
    {
        dragging = true;
        dragStart = rm;
        if (!shift)
        {
            sel.clear();
        }
    }
}

void Input::worldRightClick(Vector2 rm, Vector2 m, Vector2 w)
{
    ClientWorld &W = app.world;
    if (app.placement.active() || mode != M_None)
    {
        app.placement.cancel();
        mode = M_None;
        return;
    }

    uint32_t id = pick(rm);
    auto it = id ? W.ents.find(id) : W.ents.end();
    if (app.shipPanel.rightClick(id))
        return;

    if (it != W.ents.end() && isBuilding(W.reg.get<Info>(it->second).kind))
    {
        const Info &bi = W.reg.get<Info>(it->second);
        if (bi.kind == K_Archery && bi.owner == W.me && bi.prog >= 100 && !(bi.state & (BLD_DISABLED | BLD_MOVING)))
        {
            app.training.openFor(id);
            app.context.close();
        }
        else if (app.townHall.openFor(id))
            app.context.close(); // own completed Town Hall only
        else
            app.context.open(id, m);
        return;
    }
    if (!id && hasFeat)
    { // obstacle: selected pawns (or the nearest free pawn) clear it
        std::vector<uint32_t> pawns;
        for (auto s : selUnits())
        {
            auto e = W.ents.find(s);
            if (e != W.ents.end() && hasTag(W.reg.get<Info>(e->second).kind, KT_Worker))
                pawns.push_back(s);
        }

        if (sel.empty() || !pawns.empty())
        {
            app.cmd.clear({hoverFeat}, pawns);
            W.markers.push_back({(float(hoverFeat.x) + .5f) * TILE, (float(hoverFeat.y) + .5f) * TILE, GetTime()});
            return;
        }
    }

    if (!selUnits().empty())
    {
        if (id && W.hostileTargetId(id))
            app.cmd.attack(id);
        else
            app.cmd.move(w);
    }
}

uint32_t Input::pick(Vector2 mouse, bool includeBuildings)
{
    Vector2 w = GetScreenToWorld2D(mouse, app.cam);
    uint32_t best = 0;
    float bd = 1e9f;

    auto &reg = app.world.reg;
    reg.view<RPos, Info, NetId>().each([&](const RPos &p, const Info &i, const NetId &n)
    {
        if (isBuilding(i.kind) || aboardUnit(i))
            return;
        float cy = p.y * TILE - (i.kind == K_Sheep ? 14.f : (i.kind == K_Ship ? 20.f : 28.f)), cx = p.x * TILE;
        float d = std::hypot(w.x - cx, w.y - cy);
        if (d < (i.kind == K_Ship ? 54.f : 34.f) && d < bd)
        {
            bd = d;
            best = n.id;
        }
    });
    if (best || !includeBuildings)
        return best;
    reg.view<RPos, Info, NetId>().each([&](const RPos &p, const Info &i, const NetId &n)
    {
        if (!isBuilding(i.kind))
            return;
        float fw = float(KINFO[i.kind].fw) * TILE, fh = float(KINFO[i.kind].fh) * TILE;
        Rectangle r{p.x * TILE - fw / 2, p.y * TILE - fh / 2 - TILE * .9f, fw, fh + TILE * .9f};
        if (CheckCollisionPointRec(w, r))
            best = n.id;
    });
    return best;
}

bool Input::pickFeature(Vector2 mouse, IV &out)
{
    Vector2 w = GetScreenToWorld2D(mouse, app.cam);
    int tx = int(std::floor(w.x / TILE)), ty = int(std::floor(w.y / TILE));
    for (int d = 0; d <= 2; d++)
    {
        // a tile's sprite reaches up into the tiles above it
        uint8_t f = app.world.featVisible(tx, ty + d);
        int tall = f == F_Pine ? 3 : (f == F_Oak ? 2 : 1);
        if (f != F_None && d < tall)
        {
            out = {tx, ty + d};
            return true;
        }
    }
    return false;
}

void Input::doSelect(Vector2 a, Vector2 b, bool add)
{
    ClientWorld &W = app.world;
    if (!add)
        sel.clear();

    if (std::abs(b.x - a.x) > 6 || std::abs(b.y - a.y) > 6)
    {
        Rectangle R{std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
        W.reg.view<RPos, Info, NetId>().each([&](const RPos &p, const Info &i, const NetId &n)
        {
            if (i.owner != W.me || isBuilding(i.kind) || aboardUnit(i))
                return;
            if (CheckCollisionPointRec(GetWorldToScreen2D({p.x * TILE, p.y * TILE - 24}, app.cam), R))
                sel.insert(n.id);
        });
    }
    else
    {
        uint32_t id = pick(b);
        auto it = W.ents.find(id);
        if (id && it != W.ents.end() && W.reg.get<Info>(it->second).owner == W.me)
            sel.insert(id);

        if (id && !add && app.townHall.openFor(id))
            app.context.close(); // selecting our Town Hall opens its window
    }
}

std::vector<uint32_t> Input::selUnits()
{
    ClientWorld &W = app.world;
    std::vector<uint32_t> v;
    for (auto id : sel)
    {
        auto it = W.ents.find(id);
        if (it != W.ents.end() && !isBuilding(W.reg.get<Info>(it->second).kind) && v.size() < 255)
            v.push_back(id);
    }
    return v;
}
std::vector<uint32_t> Input::selTroops()
{
    ClientWorld &W = app.world;
    std::vector<uint32_t> v;
    for (auto id : sel)
    {
        auto it = W.ents.find(id);
        if (it == W.ents.end())
            continue;

        const Info &i = W.reg.get<Info>(it->second);
        if (isTroop(i.kind) && i.owner == W.me && !aboardUnit(i) && v.size() < 255)
            v.push_back(id);
    }
    return v;
}
int Input::armyCountSel()
{
    ClientWorld &W = app.world;
    int n = 0;
    for (auto id : sel)
    {
        auto it = W.ents.find(id);
        if (it != W.ents.end())
        {
            Kind k = W.reg.get<Info>(it->second).kind;
            if (!isBuilding(k) && !hasTag(k, KT_NonCombat))
                n++;
        }
    }
    return n;
}
