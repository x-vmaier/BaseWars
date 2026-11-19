#include "ui/TownHallWindow.hpp"

#include "client/App.hpp"
#include "rendering/Bars.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/Text.hpp"
#include "rendering/UnitAnims.hpp"
#include "ui/CostRow.hpp"
#include "ui/GameText.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

#include <rlgl.h>

#include <cmath>

namespace
{
constexpr Color INK = {70, 45, 30, 255}, SOFT = {110, 80, 50, 255}, WARN = {170, 60, 30, 255},
                GOOD = {30, 110, 40, 255};
// cost row right of a button, centred on it, wrapping up to `right`; a reason replaces the costs (red, wrapped)
void costBeside(Rectangle b, float right, const float *cost, const float *have, const std::string &reason)
{
    float x = b.x + b.width + 10, w = right - x;
    if (!reason.empty())
    {
        auto ls = wrapText(reason, int(w), 12);
        float y = b.y + b.height / 2 - 7.f * float(ls.size());
        for (auto &l : ls)
        {
            DrawText(l.c_str(), int(x), int(y), 12, WARN);
            y += 14;
        }
        return;
    }
    if (!cost)
        return;
    ui::CostStyle st = ui::paperCost();
    st.icon = 17;
    Vector2 cs = ui::measureCosts(cost, w, st);
    ui::drawCosts(cost, x, b.y + b.height / 2 - cs.y / 2, w, have, st);
}
void section(Rectangle r, const char *title)
{
    DrawRectangleRounded(r, .03f, 6, rgb(255, 250, 235, 150));
    DrawRectangleRoundedLinesEx(r, .03f, 6, 2, rgb(120, 90, 60));
    DrawText(title, int(r.x) + 10, int(r.y) + 8, 18, INK);
}
} // namespace

void TownHallWindow::load()
{
    mPanel = loadTMap("townhall_panel");
}

bool TownHallWindow::openFor(uint32_t id)
{
    const ClientWorld &W = app.world;
    auto it = W.ents.find(id);
    if (it == W.ents.end())
        return false;
    const Info &in = W.reg.get<Info>(it->second);
    if (in.kind != K_TownHall || in.owner != W.me || in.prog < 100)
        return false; // never for other players' towns
    if (target != id)
        tab = T_Main;
    target = id;
    isOpen = true;
    dragRes = disposeArm = -1;
    leaveArm = false;
    return true;
}
const Info *TownHallWindow::hall() const
{
    const ClientWorld &W = app.world;
    auto it = W.ents.find(target);
    if (it == W.ents.end())
        return nullptr;
    const Info &in = W.reg.get<Info>(it->second);
    return in.kind == K_TownHall && in.owner == W.me ? &in : nullptr;
}

// layout (panel space, scaled to fit the screen)
TownHallWindow::PanelXf TownHallWindow::xf() const
{
    Vector2 s = mPanel.size();
    float sw = float(ui::screenW()), sh = float(ui::screenH());
    float sc = std::min(1.f, std::min((sw - 16) / s.x, (sh - 34) / s.y));
    return {(sw - s.x * sc) / 2, (sh - s.y * sc) / 2 + 14 * sc, sc};
}
Vector2 TownHallWindow::toPanel(Vector2 m) const
{
    PanelXf x = xf();
    return {(m.x - x.ox) / x.sc, (m.y - x.oy) / x.sc};
}
Vector2 TownHallWindow::toScreen(Vector2 p) const
{
    PanelXf x = xf();
    return {x.ox + p.x * x.sc, x.oy + p.y * x.sc};
}
Rectangle TownHallWindow::tabRect(int i) const
{
    return {30.f + float(i) * 142.f, 34, 136, 32};
}
Rectangle TownHallWindow::content() const
{
    Vector2 s = mPanel.size();
    return {30, 76, s.x - 60, s.y - 76 - 30};
}
// main tab: workers on the left, the hall on the right
Rectangle TownHallWindow::pawnCard() const
{
    Rectangle c = content();
    return {c.x + 10, c.y + 36, 100, 118};
}
Rectangle TownHallWindow::trainBtn() const
{
    Rectangle c = content();
    return {c.x + 10, c.y + 168, 120, 30};
}
Rectangle TownHallWindow::queueSlot(int i) const
{
    Rectangle c = content();
    return {c.x + 10 + float(i) * 70.f, c.y + 244, 62, 72};
}
Rectangle TownHallWindow::moveBtn() const
{
    Rectangle c = content();
    return {c.x + 10, c.y + 400, 130, 30};
}
Rectangle TownHallWindow::upgradeBtn() const
{
    Rectangle c = content();
    return {c.x + 382 + 10, c.y + c.height - 84, 170, 30};
}
Rectangle TownHallWindow::repairBtn() const
{
    Rectangle c = content();
    return {c.x + 382 + 10, c.y + c.height - 44, 170, 30};
}

// input
void TownHallWindow::input(Vector2 mouse)
{
    const Info *in = hall();
    if (!in || in->prog < 100)
    {
        close();
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE))
    {
        close();
        return;
    }
    Vector2 m = toPanel(mouse);
    if (tab == T_Storage)
        inputStorage(m); // slider drags run while the button is held
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;
    Vector2 sz = mPanel.size();
    Rectangle closeR{sz.x - 52, 20, 26, 20};
    if (m.x < 0 || m.y < -30 || m.x > sz.x || m.y > sz.y || CheckCollisionPointRec(m, closeR))
    {
        close();
        return;
    }
    for (int i = 0; i < T_Count; i++)
        if (CheckCollisionPointRec(m, tabRect(i)))
        {
            tab = i;
            disposeArm = -1;
            leaveArm = false;
            return;
        }
    if (tab == T_Main)
        inputMain(m, *in);
    else if (tab == T_Clan)
        inputClan(m);
}

void TownHallWindow::inputMain(Vector2 m, const Info &in)
{
    ClientWorld &W = app.world;
    bool blocked = in.state & (BLD_DISABLED | BLD_MOVING | BLD_CRIPPLED);
    float pc[R_Count];
    unitCost(K_Pawn, pc);
    if (CheckCollisionPointRec(m, trainBtn()) && !blocked && W.affordable(pc))
    {
        app.cmd.train(target, K_Pawn);
        return;
    }
    if (auto q = W.queues.find(target); q != W.queues.end())
        for (size_t i = 0; i < q->second.kinds.size(); i++)
            if (CheckCollisionPointRec(m, queueSlot(int(i))))
            {
                app.cmd.dequeue(target, int(i));
                return;
            }
    const BJob *job = W.jobOf(target);
    if (CheckCollisionPointRec(m, upgradeBtn()))
    {
        if (job && job->kind != SJ_Build)
        {
            app.cmd.repair(target, 1);
            return;
        }
        float c[R_Count];
        upgradeCost(K_TownHall, in.aux, c);
        if (!job && !blocked && upgradeBlock(K_TownHall, in.aux, W.thLevel()).empty() && W.affordable(c))
            app.cmd.upgrade(target);
        return;
    }
    if (CheckCollisionPointRec(m, repairBtn()) && !job && in.hp < in.maxhp - .5f)
    {
        float rc[R_Count];
        repairCost(K_TownHall, in.aux, 1.f - in.hp / std::max(1.f, in.maxhp), rc);
        if (W.affordable(rc))
            app.cmd.repair(target, 0);
        return;
    }
    if (CheckCollisionPointRec(m, moveBtn()) && !W.townMoving && !(in.state & BLD_MOVING) &&
        W.affordable(TOWN_MOVE_COST))
    {
        close();
        app.placement.begin(Placement::MoveTown, K_TownHall, 0);
    }
}

// drawing
void TownHallWindow::draw()
{
    const Info *inp = hall();
    if (!inp)
        return;
    const Info &in = *inp;
    DrawRectangle(0, 0, ui::screenW(), ui::screenH(), rgb(0, 0, 0, 120));
    PanelXf x = xf();
    Vector2 m = toPanel(ui::mouse());
    rlPushMatrix();
    rlTranslatef(x.ox, x.oy, 0);
    rlScalef(x.sc, x.sc, 1);
    Vector2 sz = mPanel.size();
    drawTMap(mPanel, {0, 0, sz.x, sz.y});
    const TMap &mTitle = ui::titleMap();
    Rectangle tr{sz.x / 2 - mTitle.size().x / 2, -30, mTitle.size().x, mTitle.size().y};
    drawTMap(mTitle, tr);
    std::string title = TextFormat("Town Hall - Level %d", in.aux);
    textShadow(title, int(tr.x + tr.width / 2) - MeasureText(title.c_str(), 20) / 2, int(tr.y + tr.height / 2) - 12, 20,
               WHITE);
    Rectangle closeR{sz.x - 52, 20, 26, 20};
    ui::drawButton(closeR, "X", true, true, CheckCollisionPointRec(m, closeR), false, 12);
    static const char *names[T_Count] = {"Main", "Storage", "Clan"};
    Rectangle tabs[T_Count];
    for (int i = 0; i < T_Count; i++)
        tabs[i] = tabRect(i);
    ui::tabStrip(tabs, names, T_Count, tab, nullptr, m, 15);
    if (tab == T_Main)
        drawMain(m, in);
    else if (tab == T_Storage)
        drawStorage(m, in);
    else
        drawClan(m);
    rlPopMatrix();
}

void TownHallWindow::drawMain(Vector2 m, const Info &in)
{
    const ClientWorld &W = app.world;
    Rectangle c = content(), L{c.x, c.y, 370, c.height};
    section(L, "Workers");
    double now = GetTime();
    Rectangle pc = pawnCard();
    card(pc, K_Pawn, false, CheckCollisionPointRec(m, pc), false, 0);
    int pawns = 0, units = 0;
    W.reg.view<Info>().each([&](const Info &i)
    {
        if (i.owner == W.me && !isBuilding(i.kind))
        {
            units++;
            if (i.kind == K_Pawn)
                pawns++;
        }
    });
    float tx = pc.x + pc.width + 12, tw = L.x + L.width - 10 - tx;
    TroopText tt = troopText(K_Pawn);
    DrawText("Pawn", int(tx), int(pc.y), 17, INK);
    DrawText(tt.role, int(tx), int(pc.y) + 20, 12, SOFT);
    DrawText(TextFormat("Pawns %d   Units %d / %d", pawns, units, UNIT_CAP), int(tx), int(pc.y) + 38, 13,
             units >= UNIT_CAP ? WARN : INK);
    float ty = pc.y + 58;
    auto desc =
        wrapText("Builds, repairs, clears obstacles, hauls resources and hunts sheep. Weak in a fight.", int(tw), 12);
    for (size_t i = 0; i < desc.size(); i++)
    {
        if (ty + 26 > pc.y + pc.height && i + 1 < desc.size())
        {
            DrawText((desc[i] + "...").c_str(), int(tx), int(ty), 12, INK);
            break;
        }
        DrawText(desc[i].c_str(), int(tx), int(ty), 12, INK);
        ty += 14;
    }
    // train button + cost row
    bool blocked = in.state & (BLD_DISABLED | BLD_MOVING | BLD_CRIPPLED);
    float cost[R_Count];
    unitCost(K_Pawn, cost);
    Rectangle tb = trainBtn();
    ui::drawButton(tb, "Train Pawn", !blocked && W.affordable(cost), false, CheckCollisionPointRec(m, tb), false, 14);
    costBeside(tb, L.x + L.width - 10, cost, W.res, blocked ? "The hall is not functional" : "");
    const BJob *job = W.jobOf(target);
    std::string why =
        units >= UNIT_CAP
            ? "Unit cap reached: training waits for room"
            : (job && job->kind != SJ_Build ? "Training paused while the hall is upgraded / repaired" : "");
    if (!why.empty())
        DrawText(why.c_str(), int(tb.x), int(tb.y + tb.height) + 6, 12, WARN);
    // queue strip (click to cancel and refund)
    auto q = W.queues.find(target);
    size_t n = q == W.queues.end() ? 0 : q->second.kinds.size();
    DrawText(TextFormat("Training queue %d/5%s", int(n), n ? "  (click to cancel)" : ""), int(L.x) + 10,
             int(queueSlot(0).y) - 18, 13, INK);
    for (int i = 0; i < 5; i++)
    {
        Rectangle r = queueSlot(i);
        bool filled = size_t(i) < n, hov = filled && CheckCollisionPointRec(m, r);
        DrawRectangleRounded(r, .15f, 5,
                             filled ? (hov ? rgb(255, 225, 200) : rgb(250, 238, 210)) : rgb(210, 195, 165, 130));
        DrawRectangleRoundedLinesEx(r, .15f, 5, i == 0 && filled ? 3.f : 1.5f,
                                    i == 0 && filled ? rgb(60, 140, 220) : rgb(120, 90, 60));
        if (!filled)
            continue;
        Anim a = unitAnim(Kind(q->second.kinds[size_t(i)]), W.me, US_Idle, 0, 0, false, 1, now);
        drawAnim(a, frameFor(a, now, float(i) * .6f), r.x + r.width / 2, r.y + 50, false, .46f,
                 i == 0 ? WHITE : rgb(200, 200, 215));
        if (i == 0)
            barRect({r.x + 5, r.y + r.height - 11, r.width - 10, 6}, float(q->second.pct) / 100.f, BAR_TRAIN);
    }
    // move town
    Rectangle mb = moveBtn();
    DrawText("Move town", int(L.x) + 10, int(mb.y) - 58, 16, INK);
    float my = mb.y - 38;
    for (auto &l : wrapText("Rebuild the Town Hall on a new site. It keeps its level; building stops until it is done.",
                            int(L.width) - 20, 12))
    {
        DrawText(l.c_str(), int(L.x) + 10, int(my), 12, SOFT);
        my += 14;
    }
    if (W.townMoving || (in.state & BLD_MOVING))
    {
        ui::drawButton(mb, "Moving...", false, false, false, false, 14);
        barRect({mb.x + mb.width + 12, mb.y + 11, L.x + L.width - 22 - (mb.x + mb.width + 12), 9},
                float(W.movePct) / 100.f, BAR_BUILD);
    }
    else
    {
        ui::drawButton(mb, "Move town", W.affordable(TOWN_MOVE_COST), false, CheckCollisionPointRec(m, mb), false, 14);
        costBeside(mb, L.x + L.width - 10, TOWN_MOVE_COST, W.res, "");
    }
    DrawText("Tip: right-click your Town Hall to open this window.", int(L.x) + 10, int(L.y + L.height) - 22, 12, SOFT);
    drawHallSection(m, in);
}

// right column: level, health, job state, next level's effects, upgrade / cancel and repair with their cost rows
void TownHallWindow::drawHallSection(Vector2 m, const Info &in)
{
    const ClientWorld &W = app.world;
    Rectangle c = content(), R{c.x + 382, c.y, c.width - 382, c.height};
    section(R, TextFormat("Town Hall  Lv%d / %d", in.aux, MAX_LEVEL));
    float x = R.x + 12, w = R.width - 24, y = R.y + 36;
    float hpf = in.hp / std::max(1.f, in.maxhp);
    DrawText(TextFormat("Health %d / %d", int(in.hp), int(in.maxhp)), int(x), int(y), 14, INK);
    barRect({x + 150, y + 3, w - 150, 10}, hpf, healthColor(hpf));
    y += 22;
    float cap = storageCap(K_TownHall, in.aux) * hpEfficiency(hpf);
    DrawText(TextFormat("Storage %d / %d", int(W.res[0] + W.res[1] + W.res[2] + W.res[3]), int(cap)), int(x), int(y),
             14, INK);
    y += 22;
    const BJob *job = W.jobOf(target);
    std::string why = upgradeBlock(K_TownHall, in.aux, W.thLevel());
    if (job && job->kind != SJ_Build)
    {
        static const char *phase[3] = {"Waiting for a pawn", "A pawn is on the way", "A pawn is at work"};
        DrawText(
            TextFormat("%s to Lv%d... %d%%", job->kind == SJ_Upgrade ? "Upgrading" : "Repairing", in.aux + 1, job->pct),
            int(x), int(y), 14, rgb(150, 95, 0));
        y += 18;
        barRect({x, y, w, 10}, float(job->pct) / 100.f,
                job->phase == SP_Waiting ? BAR_IDLE : (job->kind == SJ_Upgrade ? BAR_UPGRADE : BAR_REPAIR));
        y += 16;
        DrawText(phase[std::min<int>(job->phase, 2)], int(x), int(y), 13, job->phase == SP_Waiting ? WARN : INK);
        y += 20;
    }
    else if (in.state & BLD_CRIPPLED)
    {
        DrawText("CRITICAL DAMAGE: repair the hall", int(x), int(y), 14, WARN);
        y += 20;
    }
    // next level
    Rectangle ub = upgradeBtn();
    float stop = ub.y - 10;
    if (in.aux >= MAX_LEVEL)
    {
        DrawText("Maximum level reached.", int(x), int(y), 14, GOOD);
    }
    else
    {
        std::vector<std::string> info;
        std::vector<Kind> units;
        nextLevelInfo(in, info, units);
        townHallUnlocks(in.aux + 1, info);
        DrawText(TextFormat("Next level (Lv%d):", in.aux + 1), int(x), int(y), 15, rgb(30, 90, 40));
        y += 20;
        for (auto &t : info)
            for (auto &l : wrapText(t, int(w) - 8, 13))
            {
                if (y + 13 > stop)
                    break;
                DrawText(l.c_str(), int(x) + 6, int(y), 13, INK);
                y += 15;
            }
    }
    // upgrade / cancel
    bool blocked = in.state & (BLD_DISABLED | BLD_MOVING | BLD_CRIPPLED);
    float uc[R_Count];
    upgradeCost(K_TownHall, in.aux, uc);
    bool hov = CheckCollisionPointRec(m, ub);
    if (job && job->kind != SJ_Build)
    {
        ui::drawButton(ub, job->kind == SJ_Upgrade ? "Cancel upgrade" : "Cancel repair", true, true, hov, false, 14);
        costBeside(ub, R.x + R.width - 10, nullptr, nullptr, "Partial refund");
    }
    else if (in.aux >= MAX_LEVEL)
        ui::drawButton(ub, "Max level", false, false, false, false, 14);
    else
    {
        std::string reason = !why.empty() ? why : (blocked ? "The hall is not functional" : "");
        ui::drawButton(ub, reason.empty() ? TextFormat("Upgrade to Lv%d", in.aux + 1) : "Upgrade locked",
                       reason.empty() && W.affordable(uc), false, hov, false, 14);
        costBeside(ub, R.x + R.width - 10, uc, W.res, reason);
    }
    // repair
    Rectangle rb = repairBtn();
    if (!job && in.hp < in.maxhp - .5f)
    {
        float rc[R_Count];
        repairCost(K_TownHall, in.aux, 1.f - hpf, rc);
        ui::drawButton(rb, "Repair", W.affordable(rc), false, CheckCollisionPointRec(m, rb), false, 14);
        costBeside(rb, R.x + R.width - 10, rc, W.res, "");
    }
    else
        DrawText(job ? "Repairs wait for the current job." : "Undamaged: no repair needed.", int(rb.x), int(rb.y) + 8,
                 13, SOFT);
}

// Archery-style card: paper tile, animated idle troop, name and level badge (dimmed when locked)
void TownHallWindow::card(Rectangle c, Kind k, bool selected, bool hover, bool dim, float phase)
{
    const ClientWorld &W = app.world;
    double now = GetTime();
    DrawRectangleRounded(c, .12f, 6, selected ? rgb(255, 245, 200) : (hover ? rgb(250, 235, 205) : rgb(238, 222, 190)));
    DrawRectangleRoundedLinesEx(c, .12f, 6, selected ? 3.f : 2.f, selected ? rgb(60, 140, 220) : rgb(120, 90, 60));
    Anim a = unitAnim(k, W.me, US_Idle, 0, 0, false, 1, now);
    float sc = (a.fw > 200 ? .36f : .62f) * std::min(1.f, c.width / 100.f);
    drawAnim(a, frameFor(a, now, phase), c.x + c.width / 2, c.y + c.height * .71f, false, sc,
             dim ? rgb(70, 70, 90, 255) : WHITE);
    DrawText(KINFO[k].name, int(c.x + c.width / 2) - MeasureText(KINFO[k].name, 14) / 2, int(c.y + c.height - 20), 14,
             INK);
    DrawRectangleRounded({c.x + 4, c.y + 4, 34, 18}, .4f, 4, rgb(60, 40, 28, 220));
    DrawText(TextFormat("Lv%d", W.unitLvl[std::clamp(int(k) - int(K_Pawn), 0, 4)]), int(c.x) + 9, int(c.y) + 7, 12,
             rgb(255, 235, 160));
}
