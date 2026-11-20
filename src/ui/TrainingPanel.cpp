/**
 * @file TrainingPanel.cpp
 * @brief Archery window: troop cards, troop details / training / troop upgrades, building upgrade, and the training
 * queue strip.
 */
#include "ui/TrainingPanel.hpp"

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

void TrainingPanel::load()
{
    mTrain = loadTMap("training_panel");
}

namespace
{
const Color INK = rgb(70, 45, 30);
} // namespace

// Panel space, scaled to fit the screen.
TrainingPanel::PanelXf TrainingPanel::xf() const
{
    Vector2 s = mTrain.size();
    float sw = float(ui::screenW()), sh = float(ui::screenH());
    float sc = std::min(1.f, std::min((sw - 16) / s.x, (sh - 30) / s.y));
    return {(sw - s.x * sc) / 2, (sh - s.y * sc) / 2 + 12 * sc, sc};
}
Vector2 TrainingPanel::toPanel(Vector2 m) const
{
    PanelXf x = xf();
    return {(m.x - x.ox) / x.sc, (m.y - x.oy) / x.sc};
}
int TrainingPanel::troopLevel(Kind k) const
{
    return app.world.unitLvl[std::clamp(int(k) - int(K_Pawn), 0, 4)];
}
Rectangle TrainingPanel::card(int i) const
{
    return {30.f + float(i % 3) * 108.f, 66.f + float(i / 3) * 128.f, 100, 118};
}
// Buttons carry a label only; each cost row sits right of (troops) or below (building upgrade) its button.
Rectangle TrainingPanel::detail() const
{
    return {30.f + 3 * 108.f + 8, 66, mTrain.size().x - (30.f + 3 * 108.f + 8) - 30, 270};
}
Rectangle TrainingPanel::btnTrain() const
{
    Rectangle d = detail();
    return {d.x + 8, d.y + d.height - 84, 128, 30};
}
Rectangle TrainingPanel::btnTroopUp() const
{
    Rectangle d = detail();
    return {d.x + 8, d.y + d.height - 42, 128, 30};
}
Rectangle TrainingPanel::block() const
{
    float y = 66 + 270 + 8;
    return {30, y, mTrain.size().x - 60, mTrain.size().y - y - 30};
}
Rectangle TrainingPanel::btnBuildingUp() const
{
    Rectangle b = block();
    return {b.x + 8, b.y + 24, 210, 28};
}
Rectangle TrainingPanel::closeBtn() const
{
    return {mTrain.size().x - 52, 20, 26, 20};
}
Rectangle TrainingPanel::queueSlot(int i) const
{
    Rectangle bb = block();
    float x0 = bb.x + 246, w = std::min(54.f, (bb.width - 256) / 5.f);
    return {x0 + float(i) * (w + 2), bb.y + 22, w, 62};
}

void TrainingPanel::input(Vector2 mouse)
{
    const ClientWorld &W = app.world;
    if (IsKeyPressed(KEY_ESCAPE))
    {
        isOpen = false;
        return;
    }
    auto it = W.ents.find(target);
    if (it == W.ents.end())
    {
        isOpen = false;
        return;
    }
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;

    Vector2 m = toPanel(mouse);
    Vector2 sz = mTrain.size();
    if (m.x < 0 || m.y < 0 || m.x > sz.x || m.y > sz.y || CheckCollisionPointRec(m, closeBtn()))
    {
        isOpen = false;
        return;
    }
    if (queueClick(m))
        return;
    for (int i = 0; i < 5; i++)
        if (CheckCollisionPointRec(m, card(i)))
        {
            sel = i;
            return;
        }

    buttonInput(m, W.reg.get<Info>(it->second));
}

void TrainingPanel::buttonInput(Vector2 m, const Info &in)
{
    const ClientWorld &W = app.world;
    Kind k = Kind(K_Pawn + sel);
    int bl = in.aux;

    float tc[R_Count];
    unitCost(k, tc);
    if (CheckCollisionPointRec(m, btnTrain()) && lockReason(k, bl).empty() && W.affordable(tc))
        app.cmd.train(target, k);

    int lv = troopLevel(k);
    float uc[R_Count];
    troopCost(k, lv + 1, uc);
    if (CheckCollisionPointRec(m, btnTroopUp()) && lv < MAX_UNIT_LEVEL && bl >= troopUpgradeNeedsArchery(lv + 1) &&
        W.affordable(uc))
        app.cmd.upgradeUnit(k);

    float bc[R_Count];
    for (int r = 0; r < R_Count; r++)
        bc[r] = float(mapUpgradeCost(in, r));
    bool busy = W.jobOf(target) != nullptr;
    if (CheckCollisionPointRec(m, btnBuildingUp()) && !busy && upgradeBlock(in.kind, in.aux, W.thLevel()).empty() &&
        W.affordable(bc))
        app.cmd.upgrade(target);
}

// "" when trainable: Town Hall level is checked before the Archery level
std::string TrainingPanel::lockReason(Kind k, int archeryLevel) const
{
    if (app.world.thLevel() < unitThLevel(k))
        return TextFormat("Town Hall Lv%d", unitThLevel(k));
    if (archeryLevel < unitUnlockLevel(k))
        return TextFormat("Archery Lv%d", unitUnlockLevel(k));
    return "";
}

bool TrainingPanel::queueClick(Vector2 m)
{
    auto q = app.world.queues.find(target);
    if (q == app.world.queues.end())
        return false;
    for (size_t i = 0; i < q->second.kinds.size(); i++)
        if (CheckCollisionPointRec(m, queueSlot(int(i))))
        {
            app.cmd.dequeue(target, int(i));
            return true;
        }
    return false;
}

void TrainingPanel::draw()
{
    const ClientWorld &W = app.world;
    auto it = W.ents.find(target);
    if (it == W.ents.end())
        return;

    const Info &bin = W.reg.get<Info>(it->second);
    DrawRectangle(0, 0, ui::screenW(), ui::screenH(), rgb(0, 0, 0, 120));
    PanelXf x = xf();
    Vector2 m = toPanel(ui::mouse());
    double now = GetTime();

    rlPushMatrix();
    rlTranslatef(x.ox, x.oy, 0);
    rlScalef(x.sc, x.sc, 1);
    drawFrame(bin, m);
    for (int i = 0; i < 5; i++)
        drawCard(i, bin, m, now);
    drawTroopDetail(bin, m, now);
    drawBuildingBlock(bin, m, now);
    drawQueueStrip(m);
    rlPopMatrix();
}

void TrainingPanel::drawFrame(const Info &bin, Vector2 m)
{
    Vector2 sz = mTrain.size();
    drawTMap(mTrain, {0, 0, sz.x, sz.y});

    const TMap &mTitle = ui::titleMap();
    Rectangle tr{sz.x / 2 - mTitle.size().x / 2, -26, mTitle.size().x, mTitle.size().y};
    drawTMap(mTitle, tr);
    std::string title = TextFormat("Archery - Level %d", bin.aux);
    textShadow(title, int(tr.x + tr.width / 2) - MeasureText(title.c_str(), 20) / 2, int(tr.y + tr.height / 2) - 12, 20,
               WHITE);
    ui::drawButton(closeBtn(), "X", true, true, CheckCollisionPointRec(m, closeBtn()), false, 12);
}

void TrainingPanel::drawCard(int i, const Info &bin, Vector2 m, double now)
{
    const ClientWorld &W = app.world;
    Kind k = Kind(K_Pawn + i);
    Rectangle c = card(i);
    bool locked = !lockReason(k, bin.aux).empty(), selected = sel == i, hov = CheckCollisionPointRec(m, c);

    DrawRectangleRounded(c, .12f, 6, selected ? rgb(255, 245, 200) : (hov ? rgb(250, 235, 205) : rgb(238, 222, 190)));
    DrawRectangleRoundedLinesEx(c, .12f, 6, selected ? 3.f : 2.f, selected ? rgb(60, 140, 220) : rgb(120, 90, 60));
    Anim a = unitAnim(k, W.me, US_Idle, 0, 0, false, 1, now);
    float sc = a.fw > 200 ? .36f : .62f;
    drawAnim(a, frameFor(a, now, float(i) * .7f), c.x + c.width / 2, c.y + 84, false, sc,
             locked ? rgb(70, 70, 90, 255) : WHITE);
    DrawText(KINFO[k].name, int(c.x + c.width / 2) - MeasureText(KINFO[k].name, 14) / 2, int(c.y + c.height - 20), 14,
             INK);
    DrawRectangleRounded({c.x + 4, c.y + 4, 34, 18}, .4f, 4, rgb(60, 40, 28, 220));
    DrawText(TextFormat("Lv%d", troopLevel(k)), int(c.x) + 9, int(c.y) + 7, 12, rgb(255, 235, 160));

    if (locked)
        drawCardLock(c, k);
}

void TrainingPanel::drawCardLock(Rectangle c, Kind k)
{
    DrawRectangleRounded(c, .12f, 6, rgb(10, 10, 25, 140));
    DrawRectangle(int(c.x + c.width / 2) - 9, int(c.y + 40), 18, 14, rgb(230, 190, 70));
    DrawRectangleLines(int(c.x + c.width / 2) - 9, int(c.y + 40), 18, 14, BLACK);
    DrawCircleLines(int(c.x + c.width / 2), int(c.y + 40), 7, rgb(230, 230, 240));

    std::string need = app.world.thLevel() < unitThLevel(k) ? TextFormat("Town Hall Lv%d", unitThLevel(k))
                                                            : TextFormat("Archery Lv%d", unitUnlockLevel(k));
    DrawText(need.c_str(), int(c.x + c.width / 2) - MeasureText(need.c_str(), 12) / 2, int(c.y + 60), 12, WHITE);
}

void TrainingPanel::drawTroopDetail(const Info &bin, Vector2 m, double now)
{
    const ClientWorld &W = app.world;
    Kind k = Kind(K_Pawn + sel);
    Rectangle d = detail();
    int lv = troopLevel(k);

    DrawRectangleRounded(d, .05f, 6, rgb(255, 250, 235, 200));
    DrawRectangleRoundedLinesEx(d, .05f, 6, 2, rgb(120, 90, 60));
    Anim big = unitAnim(k, W.me, US_Idle, 0, 0, false, 1, now);
    drawAnim(big, frameFor(big, now), d.x + 52, d.y + 106, false, big.fw > 200 ? .5f : .9f);
    TroopText tt = troopText(k);
    DrawText(TextFormat("%s  Lv%d", KINFO[k].name, lv), int(d.x) + 104, int(d.y) + 8, 17, INK);
    DrawText(tt.role, int(d.x) + 104, int(d.y) + 28, 12, rgb(110, 80, 50));

    drawTroopStats(k, lv, d);
    float ty = d.y + 122;
    for (auto &ln : wrapText(tt.desc, int(d.width) - 18, 12))
    {
        // never run into the buttons
        if (ty + 13 > btnTrain().y - 6)
            break;
        DrawText(ln.c_str(), int(d.x) + 9, int(ty), 12, INK);
        ty += 14;
    }

    drawTroopButtons(k, lv, bin.aux, m);
}

void TrainingPanel::drawTroopStats(Kind k, int lv, Rectangle d)
{
    float w = 52, x0 = d.x + 100;
    auto nxt = [&](float cur, float nv) { return lv < MAX_UNIT_LEVEL ? TextFormat("+%.0f", nv - cur) : ""; };
    float hp = KINFO[k].hp * unitHpMul(lv), hpN = KINFO[k].hp * unitHpMul(lv + 1);
    float dps = KINFO[k].dps * unitDmgMul(lv), dpsN = KINFO[k].dps * unitDmgMul(lv + 1);
    float spd = KINFO[k].speed * unitSpeedMul(lv);

    ui::statBar(x0, d.y + 48, w, hp / 300.f, rgb(220, 70, 60), "Health", TextFormat("%.0f", hp), nxt(hp, hpN));
    if (hasTag(k, KT_Healer))
        ui::statBar(x0, d.y + 66, w, dps / 30.f, rgb(90, 200, 110), "Healing", TextFormat("%.0f/s", dps), "");
    else
        ui::statBar(x0, d.y + 66, w, dps / 30.f, rgb(240, 160, 50), "Damage", TextFormat("%.0f/s", dps),
                    nxt(dps, dpsN));
    ui::statBar(x0, d.y + 84, w, KINFO[k].range / 8.f, rgb(110, 160, 230), "Range", TextFormat("%.1f", KINFO[k].range),
                "");
    ui::statBar(x0, d.y + 102, w, spd / 5.f, rgb(170, 130, 230), "Speed", TextFormat("%.1f", spd), "");
}

void TrainingPanel::drawTroopButtons(Kind k, int lv, int archeryLevel, Vector2 m)
{
    const ClientWorld &W = app.world;
    std::string lock = lockReason(k, archeryLevel);
    float tc[R_Count];
    unitCost(k, tc);
    Rectangle bt = btnTrain();
    ui::drawButton(bt, lock.empty() ? "Train" : "Locked", lock.empty() && W.affordable(tc), false,
                   CheckCollisionPointRec(m, bt), false, 14);
    sideCosts(bt, lock.empty() ? tc : nullptr, lock.empty() ? "" : "Needs " + lock);

    Rectangle bu = btnTroopUp();
    float uc[R_Count];
    troopCost(k, lv + 1, uc);
    bool maxed = lv >= MAX_UNIT_LEVEL, needB = !maxed && archeryLevel < troopUpgradeNeedsArchery(lv + 1);
    ui::drawButton(bu, maxed ? "Max level" : TextFormat("Upgrade to Lv%d", lv + 1),
                   !maxed && !needB && W.affordable(uc), false, CheckCollisionPointRec(m, bu), false, 13);
    sideCosts(bu, maxed || needB ? nullptr : uc,
              maxed ? "" : (needB ? TextFormat("Needs Archery Lv%d", troopUpgradeNeedsArchery(lv + 1)) : ""));
}

void TrainingPanel::drawBuildingBlock(const Info &bin, Vector2 m, double now)
{
    const ClientWorld &W = app.world;
    Rectangle bb = block();
    DrawText(TextFormat("Building health %d / %d", int(bin.hp), int(bin.maxhp)), int(bb.x) + 4, int(bb.y) + 4, 14, INK);

    std::string why = upgradeBlock(bin.kind, bin.aux, W.thLevel());
    const BJob *job = W.jobOf(target);
    float iy = drawBuildingUpgrade(bin, m, why, job);

    std::vector<std::string> info;
    std::vector<Kind> units;
    if (!job && why.empty())
        nextLevelInfo(bin, info, units);

    // below the queue strip
    iy = std::max(iy, bb.y + 104);
    if (!info.empty())
    {
        DrawText("Next level:", int(bb.x) + 6, int(iy), 14, rgb(30, 90, 40));
        iy += 17;
        for (auto &t : info)
        {
            DrawText(t.c_str(), int(bb.x) + 12, int(iy), 13, INK);
            iy += 15;
        }
    }
    for (size_t u = 0; u < units.size(); u++)
    {
        Anim a = unitAnim(units[u], W.me, US_Idle, 0, 0, false, 1, now);
        drawAnim(a, frameFor(a, now, float(u)), bb.x + bb.width - 60 - 70 * float(units.size() - 1 - u),
                 bb.y + bb.height - 8, false, a.fw > 200 ? .4f : .65f);
    }
}

// Upgrade button plus the status / cost line under it; returns the next free y.
float TrainingPanel::drawBuildingUpgrade(const Info &bin, Vector2 m, const std::string &why, const BJob *job)
{
    const ClientWorld &W = app.world;
    float bc[R_Count];
    for (int r = 0; r < R_Count; r++)
        bc[r] = float(mapUpgradeCost(bin, r));
    Rectangle bub = btnBuildingUp();
    std::string ul =
        job ? (job->kind == SJ_Upgrade
                   ? TextFormat("Upgrading... %d%%", job->pct)
                   : (job->kind == SJ_Repair ? TextFormat("Repairing... %d%%", job->pct) : "Under construction"))
            : (!why.empty() ? std::string("Upgrade locked") : TextFormat("Upgrade to Lv%d", bin.aux + 1));
    ui::drawButton(bub, ul, !job && why.empty() && W.affordable(bc), false, CheckCollisionPointRec(m, bub), false, 13);

    float iy = bub.y + bub.height + 6;
    if (job)
    {
        static const char *phase[3] = {"Waiting for a pawn - training paused", "A pawn is on the way - training paused",
                                       "A pawn is at work - training paused"};
        DrawText(phase[std::min<int>(job->phase, 2)], int(bub.x), int(iy), 12, rgb(170, 60, 30));
        iy += 16;
    }
    else if (!why.empty())
    {
        DrawText(why.c_str(), int(bub.x), int(iy), 12, rgb(170, 60, 30));
        iy += 16;
    }
    else
    {
        Vector2 cs = ui::drawCosts(bc, bub.x, iy, 228, W.res, ui::paperCost());
        iy += cs.y + 4;
        std::string tt2 = "Takes " + timeText(upgradeSeconds(bin.kind, bin.aux)) + " of pawn work";
        DrawText(tt2.c_str(), int(bub.x), int(iy), 12, rgb(110, 80, 50));
        iy += 16;
    }
    return iy;
}

void TrainingPanel::drawQueueStrip(Vector2 m)
{
    const ClientWorld &W = app.world;
    Rectangle bb = block();
    auto q = W.queues.find(target);
    size_t n = q == W.queues.end() ? 0 : q->second.kinds.size();
    DrawText(TextFormat("Training queue %d/5%s", int(n), n ? "  (click to cancel)" : ""), int(bb.x) + 246,
             int(bb.y) + 4, 13, INK);
    double now = GetTime();
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
        Kind k = Kind(q->second.kinds[size_t(i)]);
        Anim a = unitAnim(k, W.me, US_Idle, 0, 0, false, 1, now);
        drawAnim(a, frameFor(a, now, float(i) * .6f), r.x + r.width / 2, r.y + 44, false, a.fw > 200 ? .26f : .42f,
                 i == 0 ? WHITE : rgb(200, 200, 215));
        if (i == 0)
            barRect({r.x + 4, r.y + r.height - 10, r.width - 8, 6}, float(q->second.pct) / 100.f, BAR_TRAIN);
    }
}

// cost row (or a lock reason) right of a troop button, vertically centred on it, wrapping inside the detail panel
void TrainingPanel::sideCosts(Rectangle b, const float *cost, const std::string &reason)
{
    Rectangle d = detail();
    float x = b.x + b.width + 10, w = d.x + d.width - 8 - x;
    if (!reason.empty())
    {
        auto ls = wrapText(reason, int(w), 12);
        float y = b.y + b.height / 2 - 7.f * float(ls.size());
        for (auto &l : ls)
        {
            DrawText(l.c_str(), int(x), int(y), 12, rgb(170, 60, 30));
            y += 14;
        }
        return;
    }
    if (!cost)
        return;
    ui::CostStyle st = ui::paperCost();
    st.icon = 16;
    st.fs = 13;
    st.margin = 8;
    Vector2 cs = ui::measureCosts(cost, w, st);
    ui::drawCosts(cost, x, b.y + b.height / 2 - cs.y / 2, w, app.world.res, st);
}
