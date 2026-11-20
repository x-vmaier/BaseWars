/**
 * @file TownHallTabs.cpp
 * @brief Town Hall window: the Storage and Clan tabs (the frame and the Main tab are in TownHallWindow.cpp).
 */
#include "ui/TownHallWindow.hpp"

#include "client/App.hpp"
#include "rendering/Bars.hpp"
#include "rendering/Palette.hpp"
#include "rendering/Text.hpp"
#include "ui/CostRow.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

#include <cmath>

namespace
{
constexpr Color INK = {70, 45, 30, 255}, SOFT = {110, 80, 50, 255}, WARN = {170, 60, 30, 255};
constexpr double CONFIRM_SECONDS = 3.0;
void box(Rectangle r)
{
    DrawRectangleRounded(r, .04f, 6, rgb(255, 250, 235, 150));
    DrawRectangleRoundedLinesEx(r, .04f, 6, 2, rgb(120, 90, 60));
}
void centred(const std::string &s, float cx, float y, int fs, Color c)
{
    DrawText(s.c_str(), int(cx) - MeasureText(s.c_str(), fs) / 2, int(y), fs, c);
}
std::string shortNum(float v)
{
    return v >= 10000.f ? TextFormat("%.0fk", v / 1000.f)
                        : (v >= 1000.f ? TextFormat("%.1fk", v / 1000.f) : TextFormat("%.0f", v));
}
} // namespace

Rectangle TownHallWindow::slot(int r) const
{
    Rectangle c = content();
    float w = (c.width - 36) / 4;
    return {c.x + float(r) * (w + 12), c.y + 162, w, c.height - 162 - 34};
}
Rectangle TownHallWindow::allocSlider(int r) const
{
    Rectangle s = slot(r);
    return {s.x + 18, s.y + 166, s.width - 36, 12};
}
Rectangle TownHallWindow::disposeBtn(int r) const
{
    Rectangle s = slot(r);
    return {s.x + 16, s.y + s.height - 46, s.width - 32, 30};
}
Rectangle TownHallWindow::clanBtn() const
{
    Rectangle c = content();
    return {c.x + c.width - 172, c.y + 40, 158, 30};
}
Rectangle TownHallWindow::troopCard(int t) const
{
    Rectangle c = content();
    float w = 130, gap = 48, x0 = c.x + (c.width - (4 * w + 3 * gap)) / 2;
    return {x0 + float(t) * (w + gap), c.y + 124 + 66, w, 140};
}
Rectangle TownHallWindow::quotaArrow(int t, int dir) const
{
    Rectangle k = troopCard(t);
    return dir < 0 ? Rectangle{k.x, k.y + k.height + 10, 34, 34}
                   : Rectangle{k.x + k.width - 34, k.y + k.height + 10, 34, 34};
}

void TownHallWindow::inputStorage(Vector2 m)
{
    ClientWorld &W = app.world;
    bool lp = IsMouseButtonPressed(MOUSE_BUTTON_LEFT), down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (lp)
        for (int r = 0; r < R_Count; r++)
        {
            Rectangle sl = allocSlider(r);
            if (CheckCollisionPointRec(m, {sl.x - 12, sl.y - 12, sl.width + 24, sl.height + 24}))
                dragRes = r;
        }
    if (dragRes >= 0)
    {
        Rectangle sl = allocSlider(dragRes);
        int v = std::clamp(int(std::lround(100.f * (m.x - sl.x) / sl.width / float(ALLOC_STEP))) * ALLOC_STEP, 0, 100);
        if (v != W.alloc[dragRes])
        {
            W.alloc[dragRes] = v;
            W.baseEditAt = GetTime();
        }
        if (!down)
        {
            app.cmd.setAlloc(dragRes, W.alloc[dragRes]);
            W.baseEditAt = GetTime();
            dragRes = -1;
        }
        return;
    }
    if (!lp)
        return;
    for (int r = 0; r < R_Count; r++)
    {
        if (!CheckCollisionPointRec(m, disposeBtn(r)) || W.res[r] < 1.f)
            continue;
        if (disposeArm == r && GetTime() - disposeAt < CONFIRM_SECONDS)
        {
            app.cmd.dispose(r);
            disposeArm = -1;
        }
        else
        {
            disposeArm = r;
            disposeAt = GetTime();
        }
        return;
    }
}

void TownHallWindow::drawStorage(Vector2 m, const Info &in)
{
    Rectangle c = content();
    float hpf = in.hp / std::max(1.f, in.maxhp), cap = storageCap(K_TownHall, in.aux) * hpEfficiency(hpf);

    drawStorageSummary({c.x, c.y, c.width, 150}, in, cap, hpf);
    for (int r = 0; r < R_Count; r++)
        drawStorageSlot(r, m, cap);
    DrawText("Income above a resource's share is discarded. Shares can add up to more than 100%: the total capacity "
             "still applies.",
             int(c.x) + 4, int(c.y + c.height) - 22, 12, SOFT);
}

void TownHallWindow::drawStorageSummary(Rectangle top, const Info &in, float cap, float hpf)
{
    const ClientWorld &W = app.world;
    box(top);
    DrawText("Storage", int(top.x) + 10, int(top.y) + 8, 18, INK);

    float used = W.res[0] + W.res[1] + W.res[2] + W.res[3];
    float lw = top.width - 360;
    DrawText(TextFormat("Total %d / %d", int(used), int(cap)), int(top.x) + 12, int(top.y) + 38, 16, INK);
    barRect({top.x + 12, top.y + 60, lw - 12, 12}, used / std::max(1.f, cap),
            used >= cap * .98f ? BAR_UPGRADE : BAR_BUILD);

    float ty = top.y + 82;
    std::string lv =
        in.aux < MAX_LEVEL
            ? TextFormat("Town Hall Lv%d holds %d; Lv%d holds %d.", in.aux, int(storageCap(K_TownHall, in.aux)),
                         in.aux + 1, int(storageCap(K_TownHall, in.aux + 1)))
            : TextFormat("Town Hall Lv%d holds %d (maximum).", in.aux, int(storageCap(K_TownHall, in.aux)));
    if (hpf < .995f)
        lv += TextFormat(" Damage limits it to %d%%.", int(std::lround(hpEfficiency(hpf) * 100.f)));
    for (auto &l : wrapText(lv + " Each resource may use its share of the total.", int(lw) - 12, 13))
    {
        DrawText(l.c_str(), int(top.x) + 12, int(ty), 13, SOFT);
        ty += 15;
    }
    drawCapacityChart({top.x + top.width - 340, top.y + 10, 328, 130}, in.aux);
}

void TownHallWindow::drawStorageSlot(int r, Vector2 m, float cap)
{
    const ClientWorld &W = app.world;
    Rectangle s = slot(r);
    box(s);
    float cx = s.x + s.width / 2;
    ui::resourceIcon(r, {cx - 24, s.y + 10, 48, 48});
    centred(r == R_Meat ? "Food (Meat)" : RES_NAME[r], cx, s.y + 64, 16, INK);

    float rc = allocCap(cap, W.alloc[r]), have = W.res[r], frac = have / std::max(1.f, rc);
    centred(TextFormat("%d / %d", int(have), int(rc)), cx, s.y + 88, 15, frac >= .98f ? WARN : INK);
    barRect({s.x + 18, s.y + 110, s.width - 36, 10}, rc > 0 ? frac : 0.f, frac >= .98f ? BAR_UPGRADE : BAR_BUILD);
    centred(TextFormat("Share %d%%", W.alloc[r]), cx, s.y + 136, 15, INK);

    Rectangle sl = allocSlider(r);
    ui::sliderTrack(sl, float(W.alloc[r]) / 100.f,
                    dragRes == r || CheckCollisionPointRec(m, {sl.x - 12, sl.y - 12, sl.width + 24, sl.height + 24}));
    centred(W.alloc[r] == 0 ? "None is kept" : (frac >= .98f ? "Full: income is discarded" : "of the hall's capacity"),
            cx, s.y + 190, 12, frac >= .98f || W.alloc[r] == 0 ? WARN : SOFT);

    Rectangle db = disposeBtn(r);
    bool armed = disposeArm == r && GetTime() - disposeAt < CONFIRM_SECONDS;
    if (disposeArm == r && !armed)
        disposeArm = -1;
    centred(armed ? TextFormat("Discard all %d?", int(have)) : "Discard this resource", cx, db.y - 18, 12,
            armed ? WARN : SOFT);
    ui::drawButton(db, armed ? "Confirm" : "Dispose", have >= 1.f, true, CheckCollisionPointRec(m, db), armed, 14);
}

// Capacity per Town Hall level as small columns (current level highlighted).
void TownHallWindow::drawCapacityChart(Rectangle r, int level)
{
    DrawText("Capacity by Town Hall level", int(r.x), int(r.y), 13, INK);
    float top = r.y + 32, base = r.y + r.height - 16, mx = storageCap(K_TownHall, MAX_LEVEL),
          cw = r.width / float(MAX_LEVEL);
    for (int l = 1; l <= MAX_LEVEL; l++)
    {
        float v = storageCap(K_TownHall, l), h = (base - top) * v / mx, x = r.x + float(l - 1) * cw;
        bool cur = l == level;
        DrawRectangleRec({x + 4, base - h, cw - 8, h},
                         cur ? rgb(60, 140, 220) : (l < level ? rgb(150, 120, 80) : rgb(205, 185, 150)));
        DrawRectangleLinesEx({x + 4, base - h, cw - 8, h}, 1, rgb(90, 60, 40));
        centred(TextFormat("%d", l), x + cw / 2, base + 3, 11, cur ? rgb(30, 90, 170) : INK);
        if (cur || l == MAX_LEVEL || l == 1)
            centred(shortNum(v), x + cw / 2, base - h - 13, 11, cur ? rgb(30, 90, 170) : SOFT);
    }
}

void TownHallWindow::sendQuotas()
{
    app.cmd.quotas(app.world.quota);
    app.world.baseEditAt = GetTime();
}

void TownHallWindow::inputClan(Vector2 m)
{
    ClientWorld &W = app.world;
    for (int t = 0; t < QUOTA_TYPES; t++)
        for (int d = -1; d <= 1; d += 2)
            if (CheckCollisionPointRec(m, quotaArrow(t, d)))
            {
                int v = std::clamp(W.quota[t] + d, 0, MAX_QUOTA_UI);
                if (v != W.quota[t])
                {
                    W.quota[t] = v;
                    sendQuotas();
                }
                return;
            }
    int mc = W.myClan();
    Rectangle cb = clanBtn(), mb{cb.x - 168, cb.y, 158, 30};
    if (CheckCollisionPointRec(m, cb))
    {
        if (!mc)
        {
            close();
            app.social.open();
            app.social.tab = 2;
            return;
        }
        if (leaveArm && GetTime() - leaveAt < CONFIRM_SECONDS)
        {
            app.cmd.clanLeave();
            leaveArm = false;
        }
        else
        {
            leaveArm = true;
            leaveAt = GetTime();
        }
        return;
    }
    if (mc && CheckCollisionPointRec(m, mb))
    {
        close();
        app.social.open();
        app.social.tab = 3;
    }
}

void TownHallWindow::drawClan(Vector2 m)
{
    const ClientWorld &W = app.world;
    Rectangle c = content(), top{c.x, c.y, c.width, 112};
    drawClanMembership(m, top);

    Rectangle q{c.x, c.y + 124, c.width, c.height - 124};
    box(q);
    DrawText("Defense quotas", int(q.x) + 10, int(q.y) + 8, 18, INK);
    DrawText("Idle troops of each type sent to every clan mate under attack.", int(q.x) + 12, int(q.y) + 34, 13, SOFT);

    int total = 0;
    for (int t = 0; t < QUOTA_TYPES; t++)
    {
        drawQuotaCard(t, m);
        total += W.quota[t];
    }
    std::string sum = TextFormat("Up to %d troops per attacked clan mate.", total);
    if (!W.myClan())
        sum += " Quotas apply once you are in a clan.";
    DrawText(sum.c_str(), int(q.x) + 12, int(q.y + q.height) - 26, 14, W.myClan() ? INK : WARN);
}

void TownHallWindow::drawClanMembership(Vector2 m, Rectangle top)
{
    const ClientWorld &W = app.world;
    box(top);
    DrawText("Clan", int(top.x) + 10, int(top.y) + 8, 18, INK);

    int mc = W.myClan();
    Rectangle cb = clanBtn();
    if (!mc)
    {
        DrawText("You are not in a clan.", int(top.x) + 12, int(top.y) + 40, 16, INK);
        DrawText("Join or create one in the social menu (P) to defend each other.", int(top.x) + 12, int(top.y) + 62,
                 13, SOFT);
        ui::drawButton(cb, "Find a clan", true, false, CheckCollisionPointRec(m, cb), false, 14);
        return;
    }

    int members = 0, leader = 0;
    for (auto &cl : W.clansList)
        if (cl.id == mc)
        {
            members = cl.members;
            leader = cl.leader;
        }
    textShadow(W.clanName(mc), int(top.x) + 12, int(top.y) + 38, 20, clanColor(mc));
    std::string sub =
        TextFormat("%d member%s, leader %s", members, members == 1 ? "" : "s", leader ? W.nameOf(leader).c_str() : "?");
    DrawText(sub.c_str(), int(top.x) + 12, int(top.y) + 64, 13, SOFT);
    DrawText("Clan mates share vision; their troops help defend you.", int(top.x) + 12, int(top.y) + 82, 13, SOFT);

    bool armed = leaveArm && GetTime() - leaveAt < CONFIRM_SECONDS;
    if (leaveArm && !armed)
        leaveArm = false;
    ui::drawButton(cb, armed ? "Confirm leave" : "Leave clan", true, true, CheckCollisionPointRec(m, cb), armed, 14);
    Rectangle mb{cb.x - 168, cb.y, 158, 30};
    ui::drawButton(mb, "Members", true, false, CheckCollisionPointRec(m, mb), false, 14);
}

void TownHallWindow::drawQuotaCard(int t, Vector2 m)
{
    const ClientWorld &W = app.world;
    Kind k = QUOTA_KINDS[t];
    Rectangle tc = troopCard(t);
    bool locked = W.thLevel() < unitThLevel(k);
    card(tc, k, W.quota[t] > 0, CheckCollisionPointRec(m, tc), locked, float(t) * .7f);

    int owned = 0;
    W.reg.view<Info>().each([&](const Info &i)
    {
        if (i.owner == W.me && i.kind == k)
            owned++;
    });
    Rectangle la = quotaArrow(t, -1), ra = quotaArrow(t, 1);
    ui::arrowButton(la, -1, W.quota[t] > 0, CheckCollisionPointRec(m, la));
    ui::arrowButton(ra, 1, W.quota[t] < MAX_QUOTA_UI, CheckCollisionPointRec(m, ra));
    centred(std::to_string(W.quota[t]), tc.x + tc.width / 2, la.y + 6, 22, W.quota[t] > 0 ? INK : SOFT);
    centred(TextFormat("You have %d", owned), tc.x + tc.width / 2, la.y + la.height + 8, 12, SOFT);
    if (locked)
        centred(TextFormat("Town Hall Lv%d", unitThLevel(k)), tc.x + tc.width / 2, la.y + la.height + 24, 12, WARN);
}
