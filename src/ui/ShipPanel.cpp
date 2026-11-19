/**
 * @file ShipPanel.cpp
 * @brief Ship panel (right-click an own ship without troops selected): load, cargo, disembark.
 */
#include "ui/ShipPanel.hpp"

#include "client/App.hpp"
#include "rendering/Bars.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

Rectangle ShipPanel::rect() const
{
    float w = 330, h = 262;
    return {float(ui::screenW()) / 2 - w / 2, float(ui::screenH()) - h - 24, w, h};
}
Rectangle ShipPanel::btn(int i) const
{
    Rectangle r = rect();
    return {r.x + 14 + float(i) * 160, r.y + r.height - 46, 142, 34};
}

std::string ShipPanel::tooltip(const Info &in) const
{
    if (in.kind != K_Ship)
        return "";
    const ClientWorld &W = app.world;
    int tier = shipTier(in.aux ? in.aux : 1);
    std::string s = std::string(" - ") + SHIP_NAME[tier - 1] + " (tier " + std::to_string(tier) + ")";
    if (in.owner == W.me || W.alliedWith(in.owner))
        s += TextFormat("  Troops %d/%d  Cargo %d/%d", int(in.prog), shipTroopCap(tier), int(in.stock),
                        shipResCap(tier));
    return s;
}

// Right-click an own ship: embark the selected troops, or open the panel when none are selected.
bool ShipPanel::rightClick(uint32_t id)
{
    ClientWorld &W = app.world;
    auto it = id ? W.ents.find(id) : W.ents.end();
    if (it == W.ents.end())
        return false;
    const Info &in = W.reg.get<Info>(it->second);
    if (in.kind != K_Ship || in.owner != W.me)
        return false;
    auto tr = app.input.selTroops();
    if (!tr.empty())
    {
        app.cmd.board(id, tr);
        Vector2 w = {W.reg.get<RPos>(it->second).x * TILE, W.reg.get<RPos>(it->second).y * TILE};
        W.markers.push_back({w.x, w.y, GetTime()});
        int cap = shipTroopCap(in.aux ? in.aux : 1), free = cap - int(in.prog);
        if (int(tr.size()) > free)
            app.msg(TextFormat("The %s only has room for %d more troop(s).",
                               SHIP_NAME[shipTier(in.aux ? in.aux : 1) - 1], std::max(0, free)));
        return true;
    }
    ship = id;
    app.context.close();
    return true;
}

bool ShipPanel::input(Vector2 m, bool lp, bool rp)
{
    if (!ship)
        return false;
    const ClientWorld &W = app.world;
    auto it = W.ents.find(ship);
    if (it == W.ents.end() || W.reg.get<Info>(it->second).owner != W.me)
    {
        ship = 0;
        return false;
    }
    if (IsKeyPressed(KEY_ESCAPE))
    {
        ship = 0;
        return true;
    }
    bool over = CheckCollisionPointRec(m, rect());
    if (lp && over)
    {
        const Info &in = W.reg.get<Info>(it->second);
        if (CheckCollisionPointRec(m, btn(0)))
        {
            if (in.prog == 0)
                app.msg("Nobody is aboard.");
            else if (!W.coastNearShip(W.reg.get<RPos>(it->second)))
                app.msg("Move the ship next to a coastline to disembark.");
            else
                app.cmd.disembark(ship);
        }
        else if (CheckCollisionPointRec(m, btn(1)))
            ship = 0;
        return true;
    }
    if (lp && !over)
        ship = 0; // clicking elsewhere closes it and still acts on the world
    if (rp && over)
        return true;
    return false;
}

void ShipPanel::draw()
{
    if (!ship)
        return;
    const ClientWorld &W = app.world;
    auto it = W.ents.find(ship);
    if (it == W.ents.end())
    {
        ship = 0;
        return;
    }
    const Info &in = W.reg.get<Info>(it->second);
    const RPos &rp = W.reg.get<RPos>(it->second);
    Vector2 m = ui::mouse();
    Rectangle r = rect();
    int tier = shipTier(in.aux ? in.aux : 1), tcap = shipTroopCap(tier), rcap = shipResCap(tier);
    DrawRectangleRounded(r, .06f, 8, rgb(238, 222, 190));
    DrawRectangleRoundedLinesEx(r, .06f, 8, 3.f, rgb(120, 90, 60));
    Color ink = rgb(70, 45, 30);
    DrawText(TextFormat("%s  (tier %d)", SHIP_NAME[tier - 1], tier), int(r.x) + 16, int(r.y) + 12, 22, ink);
    DrawText(TextFormat("Health %d / %d", int(in.hp), int(in.maxhp)), int(r.x) + 16, int(r.y) + 40, 15, ink);
    auto bar = [&](float y, float frac, Color c) { barRect({r.x + 16, y, r.width - 32, 14}, frac, c); };
    auto dit = W.shipDetails.find(ship);
    DrawText(TextFormat("Troops aboard: %d / %d", int(in.prog), tcap), int(r.x) + 16, int(r.y) + 64, 16, ink);
    bar(r.y + 84, float(in.prog) / float(tcap), in.prog >= tcap ? BAR_UPGRADE : BAR_TROOPS);
    std::string comp;
    if (dit != W.shipDetails.end())
    {
        static const char *nm[5] = {"Pawn", "Warrior", "Archer", "Lancer", "Monk"};
        for (int k = 0; k < 5; k++)
            if (dit->second.kinds[k] > 0)
                comp += std::string(comp.empty() ? "" : ", ") + nm[k] + " x" + std::to_string(dit->second.kinds[k]);
    }
    DrawText(comp.empty() ? "Empty hold" : comp.c_str(), int(r.x) + 16, int(r.y) + 104, 14, rgb(100, 70, 50));
    DrawText(TextFormat("Cargo: %d / %d", int(in.stock), rcap), int(r.x) + 16, int(r.y) + 128, 16, ink);
    bar(r.y + 148, float(in.stock) / float(rcap), BAR_CARGO);
    if (dit != W.shipDetails.end() && in.stock > 0)
        DrawText(TextFormat("Wood %d  Gold %d  Stone %d  Meat %d", dit->second.res[0], dit->second.res[1],
                            dit->second.res[2], dit->second.res[3]),
                 int(r.x) + 16, int(r.y) + 168, 14, rgb(100, 70, 50));
    bool coast = W.coastNearShip(rp), busy = dit != W.shipDetails.end() && dit->second.voyage;
    const char *hint = busy ? "On a supply run for your haulers."
                            : (in.prog > 0 && !coast ? "Sail next to a coastline to disembark."
                                                     : "Select troops, then right-click the ship to embark.");
    DrawText(hint, int(r.x) + 16, int(r.y) + 190, 13, (in.prog > 0 && !coast) ? rgb(170, 60, 40) : rgb(100, 70, 50));
    bool can = in.prog > 0 && coast;
    ui::drawButton(btn(0), "Disembark all", can, false, CheckCollisionPointRec(m, btn(0)),
                   CheckCollisionPointRec(m, btn(0)) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    ui::drawButton(btn(1), "Close", true, true, CheckCollisionPointRec(m, btn(1)),
                   CheckCollisionPointRec(m, btn(1)) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
}
