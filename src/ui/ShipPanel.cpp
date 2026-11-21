/**
 * @file ShipPanel.cpp
 * @brief Ship panel (right-click an own ship without troops selected): load, cargo, disembark.
 */
#include "ui/ShipPanel.hpp"

#include "client/App.hpp"
#include "common/Progression.hpp"
#include "rendering/Bars.hpp"
#include "rendering/Text.hpp"
#include "ui/CostRow.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

Rectangle ShipPanel::rect() const
{
    float w = 490, h = 330;
    return {float(ui::screenW()) / 2 - w / 2, float(ui::screenH()) - h - 24, w, h};
}
Rectangle ShipPanel::btn(int i) const
{
    Rectangle r = rect();
    return {r.x + 14 + float(i) * 160, r.y + r.height - 46, 142, 34};
}

std::string ShipPanel::tooltip(const Info &in) const
{
    if (!hasTag(in.kind, KT_Naval))
        return "";
    const ClientWorld &W = app.world;
    int lv = shipLevel(in.aux ? in.aux : 1);
    std::string s = std::string(" - ") + KINFO[in.kind].name + " (level " + std::to_string(lv) + ")";
    if (in.owner == W.me || W.alliedWith(in.owner))
        s += TextFormat("  Troops %d/%d  Cargo %d/%d", int(in.prog), shipTroopCap(in.kind, lv), int(in.stock),
                        shipResCap(in.kind, lv));
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
    if (!hasTag(in.kind, KT_Naval) || in.owner != W.me)
        return false;
    auto tr = app.input.selTroops();
    if (!tr.empty())
    {
        app.cmd.board(id, tr);
        Vector2 w = {W.reg.get<RPos>(it->second).x * TILE, W.reg.get<RPos>(it->second).y * TILE};
        W.markers.push_back({w.x, w.y, GetTime()});
        int cap = shipTroopCap(in.kind, in.aux ? in.aux : 1), free = cap - int(in.prog);
        if (int(tr.size()) > free)
            app.msg(TextFormat("The %s only has room for %d more troop(s).", KINFO[in.kind].name, std::max(0, free)));
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
            app.cmd.upgradeShip(ship);
        else if (CheckCollisionPointRec(m, btn(2)))
            ship = 0;
        return true;
    }
    // clicking elsewhere closes it and still acts on the world
    if (lp && !over)
        ship = 0;
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
    int lv = shipLevel(in.aux ? in.aux : 1);
    Color ink = rgb(70, 45, 30);

    DrawRectangleRounded(r, .06f, 8, rgb(238, 222, 190));
    DrawRectangleRoundedLinesEx(r, .06f, 8, 3.f, rgb(120, 90, 60));
    drawText(TextFormat("%s  (level %d)", KINFO[in.kind].name, lv), int(r.x) + 16, int(r.y) + 12, 22, ink);
    drawText(TextFormat("Health %d / %d", int(in.hp), int(in.maxhp)), int(r.x) + 16, int(r.y) + 40, 15, ink);

    drawHold(in, r);
    drawCargo(in, r);
    drawUpgrade(in, r);
    drawActions(in, rp, m);
}

void ShipPanel::drawHold(const Info &in, Rectangle r)
{
    const ClientWorld &W = app.world;
    int tcap = shipTroopCap(in.kind, in.aux ? in.aux : 1);
    auto dit = W.shipDetails.find(ship);
    int crew = shipCrewMin(in.kind);
    drawText(TextFormat("Troops aboard: %d / %d   (crew needed: %d)", int(in.prog), tcap, crew), int(r.x) + 16,
             int(r.y) + 64, 16, in.prog >= crew ? rgb(70, 45, 30) : rgb(170, 60, 40));
    barRect({r.x + 16, r.y + 84, r.width - 32, 14}, float(in.prog) / float(tcap),
            in.prog >= tcap ? BAR_UPGRADE : BAR_TROOPS);

    std::string comp;
    if (dit != W.shipDetails.end())
    {
        static const char *nm[5] = {"Pawn", "Warrior", "Archer", "Lancer", "Monk"};
        for (int k = 0; k < 5; k++)
            if (dit->second.kinds[k] > 0)
                comp += std::string(comp.empty() ? "" : ", ") + nm[k] + " x" + std::to_string(dit->second.kinds[k]);
    }
    drawText(comp.empty() ? "Empty hold" : comp.c_str(), int(r.x) + 16, int(r.y) + 104, 14, rgb(100, 70, 50));
}

void ShipPanel::drawCargo(const Info &in, Rectangle r)
{
    const ClientWorld &W = app.world;
    int rcap = shipResCap(in.kind, in.aux ? in.aux : 1);
    auto dit = W.shipDetails.find(ship);
    drawText(TextFormat("Cargo: %d / %d", int(in.stock), rcap), int(r.x) + 16, int(r.y) + 128, 16, rgb(70, 45, 30));
    barRect({r.x + 16, r.y + 148, r.width - 32, 14}, float(in.stock) / float(rcap), BAR_CARGO);
    if (dit != W.shipDetails.end() && in.stock > 0)
        drawText(TextFormat("Wood %d  Gold %d  Stone %d  Meat %d", dit->second.res[0], dit->second.res[1],
                            dit->second.res[2], dit->second.res[3]),
                 int(r.x) + 16, int(r.y) + 168, 14, rgb(100, 70, 50));
}

// Upgrade line: running progress, or cost + the reason it is not possible right now.
void ShipPanel::drawUpgrade(const Info &in, Rectangle r)
{
    const ClientWorld &W = app.world;
    auto dit = W.shipDetails.find(ship);
    int lv = shipLevel(in.aux ? in.aux : 1);
    Color ink = rgb(100, 70, 50);
    int y = int(r.y) + 214;
    if (dit != W.shipDetails.end() && dit->second.upgradePct > 0)
    {
        drawText(TextFormat("Upgrading to level %d ...", lv + 1), int(r.x) + 16, y, 14, ink);
        barRect({r.x + 190, float(y) + 2, r.width - 206, 12}, float(dit->second.upgradePct) / 100.f, BAR_UPGRADE);
        return;
    }
    if (lv >= SHIP_MAX_LEVEL)
    {
        drawText("Fully upgraded.", int(r.x) + 16, y, 14, ink);
        return;
    }
    float c[R_Count];
    upgradeCost(in.kind, lv, c);
    drawText(TextFormat("Upgrade to level %d:", lv + 1), int(r.x) + 16, y, 14, ink);
    ui::drawCosts(c, r.x + 160, float(y) - 2, r.width - 176, W.res, ui::paperCost());
    drawText(TextFormat("Dock at a Harbour of level %d.", lv + 1), int(r.x) + 16, y + 20, 13, ink);
}

void ShipPanel::drawActions(const Info &in, const RPos &rp, Vector2 m)
{
    const ClientWorld &W = app.world;
    Rectangle r = rect();
    auto dit = W.shipDetails.find(ship);
    bool coast = W.coastNearShip(rp), busy = dit != W.shipDetails.end() && dit->second.voyage;
    const char *hint = busy ? "On a supply run for your haulers."
                            : (in.prog > 0 && !coast ? "Sail next to a coastline to disembark."
                                                     : "Select troops, then right-click the ship to embark.");
    drawText(hint, int(r.x) + 16, int(r.y) + 192, 13, (in.prog > 0 && !coast) ? rgb(170, 60, 40) : rgb(100, 70, 50));

    bool can = in.prog > 0 && coast;
    ui::drawButton(btn(0), "Disembark all", can, false, CheckCollisionPointRec(m, btn(0)),
                   CheckCollisionPointRec(m, btn(0)) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    bool up =
        shipLevel(in.aux ? in.aux : 1) < SHIP_MAX_LEVEL && !(dit != W.shipDetails.end() && dit->second.upgradePct > 0);
    ui::drawButton(btn(1), "Upgrade", up, false, CheckCollisionPointRec(m, btn(1)),
                   CheckCollisionPointRec(m, btn(1)) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    ui::drawButton(btn(2), "Close", true, true, CheckCollisionPointRec(m, btn(2)),
                   CheckCollisionPointRec(m, btn(2)) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
}
