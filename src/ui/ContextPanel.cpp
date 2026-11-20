/**
 * @file ContextPanel.cpp
 * @brief Context panel of a building: stats lines, action buttons with cost rows, haul slider, next-level preview.
 */
#include "ui/ContextPanel.hpp"

#include "client/App.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/UnitAnims.hpp"
#include "ui/CostRow.hpp"
#include "ui/GameText.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

#include <cmath>

void ContextPanel::load()
{
    mCtx = loadTMap("context_panel");
}

void ContextPanel::open(uint32_t id, Vector2 at)
{
    target = id;
    isOpen = true;
    pos = {at.x + 12, at.y - 20};
}

namespace
{
const Color INK = rgb(70, 45, 30);
const Color WARN_BROWN = rgb(130, 90, 30);
} // namespace

void ContextPanel::rebuild()
{
    ClientWorld &W = app.world;
    acts.clear();
    lines.clear();
    next.clear();
    nextUnits.clear();
    slider = false;

    auto it = W.ents.find(target);
    if (it == W.ents.end())
    {
        isOpen = false;
        return;
    }

    const Info &in = W.reg.get<Info>(it->second);
    bool mine = in.owner == W.me, done = in.prog >= 100;

    ownerLines(in, mine);
    if (isFactionOwner(in.owner))
        factionLines(in);
    if (in.prog < 100)
        lines.push_back({"Under construction " + std::to_string(in.prog) + "%", INK});
    lines.push_back({TextFormat("Health %d / %d", int(in.hp), int(in.maxhp)), INK});
    if (mine || W.alliedWith(in.owner))
        statLines(it->second, in, mine, done);

    if (mine && done)
        ownActs(in);
    else if (!mine)
        foreignActs(in);

    baseContext(in, mine, done);
    layout();
}

void ContextPanel::ownerLines(const Info &in, bool mine)
{
    const ClientWorld &W = app.world;
    Color c = mine ? rgb(30, 90, 40) : (isFactionOwner(in.owner) ? rgb(60, 50, 110) : rgb(150, 40, 30));
    lines.push_back({std::string("Owner: ") + W.nameOf(in.owner), c});
}

void ContextPanel::factionLines(const Info &in)
{
    const ClientWorld &W = app.world;
    int f = in.owner - FACTION_BASE, rp = W.facRep[f];
    lines.push_back({TextFormat("Reputation: %s (%d)", repTier(rp), rp),
                     rp <= HOSTILE_REP ? rgb(170, 40, 30) : (rp >= FRIENDLY_REP ? rgb(30, 110, 40) : INK)});
    lines.push_back({rp <= HOSTILE_REP
                         ? "They will fight you on sight."
                         : (rp >= FRIENDLY_REP ? "They will aid you if attacked." : "Gifts earn their trust."),
                     INK});

    const FacDemand &d = W.demand[f];
    int left = std::max(0, int(d.changeAt - GetTime()));
    lines.push_back(
        {TextFormat("Town demand: %s (x%.1f reputation)", RES_NAME[d.res], cfg().demandBonus), rgb(150, 95, 0)});
    lines.push_back({TextFormat("Changes in %d:%02d (or once satisfied)", left / 60, left % 60), INK});
}

// Lines only the owner and allies see: level, biome, then output / storage / site note.
void ContextPanel::statLines(entt::entity e, const Info &in, bool mine, bool done)
{
    ClientWorld &W = app.world;
    int lvCap = maxLevelFor(in.kind, mine ? W.thLevel() : MAX_LEVEL);
    bool single = hasTag(in.kind, KT_SingleLevel);
    lines.push_back({lvCap < MAX_LEVEL && !single
                         ? TextFormat("Level %d / %d (max %d at Town Hall Lv%d)", in.aux, MAX_LEVEL, lvCap, W.thLevel())
                         : TextFormat("Level %d / %d", in.aux, single ? 1 : MAX_LEVEL),
                     INK});

    Pos c = {W.reg.get<Goal>(e).x, W.reg.get<Goal>(e).y};
    int b = W.biomeAt(int(std::floor(c.x)), int(std::floor(c.y)));
    lines.push_back({std::string("Biome: ") + (b == INVALID_BIOME ? "?" : BIOME_NAME[b]), INK});

    if (isResourceBuilding(in.kind))
        outputLines(in, c, b);
    else if (isStorage(in.kind))
        storageLines(in, mine, done);
    else if (KINDS[in.kind].siteNote[0])
        lines.push_back({KINDS[in.kind].siteNote, INK});
}

void ContextPanel::outputLines(const Info &in, Pos c, int biome)
{
    const ClientWorld &W = app.world;
    Yield yv = yieldOf(in.kind, biome, hasTag(in.kind, KT_Coastal));
    int goldN = hasTag(in.kind, KT_GoldBoosted) ? W.goldNear(c) : 0;
    float gb = 1.f + goldBonusOf(goldN);
    float eff = hpEfficiency(in.hp / std::max(1.f, in.maxhp));

    std::string pr;
    for (int r = 0; r < R_Count; r++)
        if (yv.r[r] > 0)
            pr += TextFormat("%s %.2f/s  ", RES_NAME[r], yv.r[r] * productionMul(in.aux) * gb * eff);
    lines.push_back({"Output: " + pr, INK});

    if (hasTag(in.kind, KT_GoldBoosted))
        lines.push_back(
            {goldN ? TextFormat("Gold deposits nearby: %d (+%d%% output)", goldN, int(std::lround((gb - 1.f) * 100.f)))
                   : "No gold deposits nearby (+20% each, max +100%)",
             goldN ? rgb(150, 105, 0) : WARN_BROWN});

    bool pref = preferredBiome(in.kind, biome);
    lines.push_back(
        {pref ? "Preferred biome: BONUS ACTIVE" : KINDS[in.kind].siteNote, pref ? rgb(30, 110, 40) : WARN_BROWN});
    lines.push_back({TextFormat("Stock waiting: %d", in.stock), INK});
}

void ContextPanel::storageLines(const Info &in, bool mine, bool done)
{
    const ClientWorld &W = app.world;
    float hpEff = hpEfficiency(in.hp / std::max(1.f, in.maxhp));
    float cap = std::max(1.f, storageCap(in.kind, in.aux) * hpEff);

    if (in.kind == K_TownHall)
        lines.push_back(
            {TextFormat("Storage: %d / %d", int(W.res[0] + W.res[1] + W.res[2] + W.res[3]), int(cap)), INK});
    else
        lines.push_back(
            {TextFormat("Stored: %d / %d (%d%%)", int(in.stock), int(cap), int(in.stock * 100.f / cap)), INK});

    if (KINFO[in.kind].dps > 0)
        lines.push_back({TextFormat("Defence %.0f dps, territory %d", KINFO[in.kind].dps * levelMul(in.aux) * hpEff,
                                    anchorRadius(in.kind)),
                         INK});

    if (mine && done && hasTag(in.kind, KT_Depot))
    {
        slider = true;
        if (!sliderDrag)
            pct = in.aim;
    }
}

void ContextPanel::ownActs(const Info &in)
{
    trainActs(in);
    if (!hasTag(in.kind, KT_SingleLevel))
        upgradeActs(in);
}

void ContextPanel::trainActs(const Info &in)
{
    const ClientWorld &W = app.world;
    int th = W.thLevel();
    for (int u = K_Pawn; u < K_Count; u++)
    {
        if (!canTrain(in.kind, Kind(u)))
            continue;

        float c[R_Count];
        unitCost(Kind(u), c);
        bool locked = th < unitThLevel(Kind(u));
        if (locked)
            addAct({TextFormat("%s: requires Town Hall Lv%d", KINFO[u].name, unitThLevel(Kind(u))), 1, target,
                    uint32_t(u), false, false});
        else
            addAct({std::string(KINDS[u].verb) + " " + KINFO[u].name, 1, target, uint32_t(u), W.affordable(c), false},
                   c);
    }
}

void ContextPanel::upgradeActs(const Info &in)
{
    const ClientWorld &W = app.world;
    Kind k = in.kind;
    float c[R_Count];
    upgradeCost(k, in.aux, c);
    std::string why = upgradeBlock(k, in.aux, W.thLevel());
    const BJob *j = W.jobOf(target);

    if (j && j->kind != SJ_Build)
        addAct({j->kind == SJ_Upgrade ? "Cancel upgrade (partial refund)" : "Cancel repair (partial refund)", 8, target,
                1, true, true});
    else if (!why.empty())
        addAct({why, 2, target, 0, false, false});
    else
        addAct({"Upgrade to Lv" + std::to_string(in.aux + 1), 2, target, 0, W.affordable(c), false}, c,
               timeText(upgradeSeconds(k, in.aux)));

    if (!j && in.hp < in.maxhp - .5f)
    {
        float rc[R_Count];
        repairCost(k, in.aux, 1.f - in.hp / std::max(1.f, in.maxhp), rc);
        addAct({"Repair", 7, target, 0, W.affordable(rc), false}, rc);
    }
    if (!j && why.empty())
        nextLevelInfo(in, next, nextUnits);
}

void ContextPanel::foreignActs(const Info &in)
{
    if (isFactionOwner(in.owner))
        giftActs(in.owner - FACTION_BASE);

    acts.push_back({"Attack this target", 3, target, 0, true, true});
    if (in.owner > 0 && !isFactionOwner(in.owner))
        allianceAct(in.owner);
}

void ContextPanel::giftActs(int f)
{
    const ClientWorld &W = app.world;
    float wait = W.donateWait();
    for (int r = 0; r < R_Count; r++)
    {
        int mul = W.demand[f].mul[r];
        float gift[R_Count]{};
        gift[r] = 50.f;
        std::string note = wait > 0
                               ? TextFormat("wait %d s", int(std::ceil(wait)))
                               : TextFormat("rep x%.1f%s", float(mul) / 100.f, W.demand[f].res == r ? " WANTED" : "");
        addAct({"Give", 6, uint32_t(f), uint32_t(r), W.res[r] >= 50.f && wait <= 0, false}, gift, note);
    }
}

void ContextPanel::allianceAct(int owner)
{
    auto p = app.world.peerOf(owner);
    uint8_t fl = p ? p->flags : 0;
    if (fl & 2)
        acts.push_back({"Break alliance", 5, uint32_t(owner), 0, true, true});
    else if (fl & 4)
        acts.push_back({"Accept alliance", 4, uint32_t(owner), 0, true, false});
    else if (fl & 8)
        acts.push_back({"Alliance invite pending", 4, uint32_t(owner), 0, false, false});
    else
        acts.push_back({"Invite to alliance", 4, uint32_t(owner), 0, true, false});
}

void ContextPanel::addAct(MenuAct a, const float *cost, const std::string &note)
{
    if (cost)
    {
        a.hasCost = true;
        for (int r = 0; r < R_Count; r++)
            a.cost[r] = cost[r];
    }
    a.note = note;
    acts.push_back(std::move(a));
}

// fire / disabled / moving lines, training queue, defense quota slider, move town, relocate
void ContextPanel::baseContext(const Info &in, bool mine, bool done)
{
    const ClientWorld &W = app.world;
    if (!isBuilding(in.kind))
        return;
    Color red = rgb(170, 40, 30), ink = rgb(70, 45, 30);
    bool burning = in.state & BLD_BURNING, dis = in.state & BLD_DISABLED, mov = in.state & BLD_MOVING;
    if (burning)
        lines.push_back({"ON FIRE! Idle units will put it out.", red});
    siteLines(in, mine || W.alliedWith(in.owner));
    if (mov)
        lines.push_back(
            {in.kind == K_TownHall ? "Town is being moved to a new site." : "Being relocated.", rgb(120, 80, 160)});
    else if (dis)
        lines.push_back({"NON-FUNCTIONAL: outside your territory", red});
    if (!mine || !done)
        return;
    auto q = W.queues.find(target);
    if (q != W.queues.end() && !q->second.kinds.empty())
    {
        std::string s = "Queue:";
        for (size_t i = 0; i < q->second.kinds.size(); i++)
            s += std::string(i ? ", " : " ") + KINFO[q->second.kinds[i]].name +
                 (i == 0 ? TextFormat(" %d%%", q->second.pct) : "");
        lines.push_back({s, ink});
    }
    if (dis && !mov && !hasTag(in.kind, KT_Freestanding))
    {
        bool can = W.affordable(KINFO[in.kind].cost) && !W.townMoving;
        addAct({"Relocate", 21, target, 0, can, false}, KINFO[in.kind].cost);
    }
    if (dis || mov || (in.state & BLD_CRIPPLED))
        for (auto &a : acts)
            if (a.act == 1 || a.act == 2)
                a.enabled = false;
}

// construction / upgrade / repair progress, damage efficiency, paused production / training
void ContextPanel::siteLines(const Info &in, bool friendly)
{
    const ClientWorld &W = app.world;
    Color red = rgb(170, 40, 30), work = rgb(150, 95, 0), ink = rgb(70, 45, 30);
    const BJob *j = friendly ? W.jobOf(target) : nullptr;
    static const char *phase[3] = {"Waiting for a pawn", "A pawn is on the way", "A pawn is at work"};
    if (j)
    {
        if (j->kind == SJ_Upgrade)
            lines.push_back({TextFormat("Upgrading to Lv%d... %d%%", in.aux + 1, j->pct), work});
        else if (j->kind == SJ_Repair)
            lines.push_back({TextFormat("Repairing... %d%%", j->pct), work});
        lines.push_back({phase[std::min<int>(j->phase, 2)], j->phase == SP_Waiting ? red : ink});
    }
    else if (in.state & BLD_WORK)
        lines.push_back({"Work in progress", work});
    float hpf = in.hp / std::max(1.f, in.maxhp);
    if (in.prog >= 100 && (in.state & BLD_CRIPPLED))
        lines.push_back({"CRITICAL DAMAGE: disabled until repaired", red});
    else if (in.prog >= 100 && hpf < .995f && friendly)
        lines.push_back({TextFormat("Damaged: working at %d%% efficiency", int(std::lround(hpEfficiency(hpf) * 100.f))),
                         rgb(170, 90, 30)});
    bool producer = hasTag(in.kind, KT_Producer) || hasTag(in.kind, KT_Trainer);
    if (friendly && producer && (in.prog < 100 || (in.state & BLD_WORK)))
        lines.push_back({"Production and training paused", red});
}

void ContextPanel::run(const MenuAct &a)
{
    Commands &cmd = app.cmd;
    switch (a.act)
    {
    case 1:
        cmd.train(a.a, Kind(a.b));
        break;
    case 2:
        cmd.upgrade(a.a);
        break;
    case 3:
        cmd.attack(a.a);
        break;
    case 4:
        cmd.ally(int(a.a), AA_Invite);
        break;
    case 5:
        cmd.ally(int(a.a), AA_Break);
        break;
    case 6:
        cmd.donate(int(a.a), int(a.b), 50.f);
        break;
    case 7:
        cmd.repair(a.a, 0);
        break;
    case 8:
        cmd.repair(a.a, 1);
        break;
    default:
        if (!baseRun(a))
            isOpen = false;
        break;
    }
}
bool ContextPanel::baseRun(const MenuAct &a)
{
    auto it = app.world.ents.find(a.a);
    switch (a.act)
    {
    case 21:
        if (it != app.world.ents.end())
            app.placement.begin(Placement::Relocate, app.world.reg.get<Info>(it->second).kind, a.a);
        return true;
    default:
        return false;
    }
}

// Rows: [button: label only][cost row, wrapping][note]. A row is as tall as its wrapped cost row (at least a button).
namespace
{
constexpr float BTN_H = 30, ROW_GAP = 6, COST_GAP = 10, NOTE_FS = 12;
float btnWidth(const ContextPanel::MenuAct &a, float inner)
{
    if (!a.hasCost)
        return inner;
    return std::clamp(float(MeasureText(a.label.c_str(), 14)) + 28.f, 76.f, inner * .5f);
}
} // namespace

float ContextPanel::innerW() const
{
    return mCtx.size().x - 2 * float(mCtx.padding) - 12;
}
void ContextPanel::layout()
{
    rowY.assign(acts.size(), 0);
    rowH.assign(acts.size(), BTN_H);
    float y = 0, inner = innerW();
    ui::CostStyle st = ui::paperCost();
    for (size_t i = 0; i < acts.size(); i++)
    {
        const MenuAct &a = acts[i];
        float h = BTN_H;
        if (a.hasCost)
        {
            float cw = inner - btnWidth(a, inner) - COST_GAP;
            Vector2 cs = ui::measureCosts(a.cost, cw, st);
            float nh = 0;
            if (!a.note.empty())
                nh = cs.x + 8 + float(MeasureText(a.note.c_str(), int(NOTE_FS))) <= cw ? 0.f : NOTE_FS + 3;
            h = std::max(BTN_H, cs.y + nh);
        }
        rowY[i] = y;
        rowH[i] = h;
        y += h + ROW_GAP;
    }
    float nextH = next.empty() ? 0.f : 22.f + 16.f * float(next.size()) + 6.f;
    float content = 20.f + 19.f * float(lines.size()) + (slider ? 50.f : 4.f) + y + nextH;
    panelH = std::clamp(content + 2 * float(mCtx.padding) + 8, 160.f, std::max(160.f, float(ui::screenH()) - 64));
}
Rectangle ContextPanel::rect() const
{
    Vector2 s = mCtx.size();
    float h = panelH > 0 ? panelH : s.y;
    return {std::clamp(pos.x, 8.f, float(ui::screenW()) - s.x - 8),
            std::clamp(pos.y, 56.f, std::max(56.f, float(ui::screenH()) - h - 8)), s.x, h};
}
float ContextPanel::topY() const
{
    Rectangle r = rect();
    return r.y + float(mCtx.padding) + 20;
}
Rectangle ContextPanel::sliderRect() const
{
    Rectangle r = rect();
    float pad = float(mCtx.padding);
    return {r.x + pad + 12, topY() + 19.f * float(lines.size()) + 22, r.width - 2 * pad - 24, 14};
}
Rectangle ContextPanel::button(int i) const
{
    Rectangle r = rect();
    float pad = float(mCtx.padding), y0 = topY() + 19.f * float(lines.size()) + (slider ? 50.f : 4.f);
    if (i >= int(acts.size()) || i >= int(rowY.size()))
    {
        float end = rowY.empty() ? 0.f : rowY.back() + rowH.back() + ROW_GAP;
        return {r.x + pad + 6, y0 + end, innerW(), BTN_H};
    }
    const MenuAct &a = acts[size_t(i)];
    return {r.x + pad + 6, y0 + rowY[size_t(i)] + (rowH[size_t(i)] - BTN_H) / 2, btnWidth(a, innerW()), BTN_H};
}
Rectangle ContextPanel::costArea(int i) const
{
    Rectangle b = button(i);
    float x = b.x + b.width + COST_GAP, w = innerW() - b.width - COST_GAP;
    float y0 = topY() + 19.f * float(lines.size()) + (slider ? 50.f : 4.f) + rowY[size_t(i)];
    return {x, y0, w, rowH[size_t(i)]};
}

// auto-haul threshold of an outpost / storage
bool ContextPanel::sliderInput(Vector2 m, bool lp, bool lr)
{
    if (!isOpen || !slider)
        return false;
    Rectangle sr = sliderRect();
    Rectangle hit{sr.x - 12, sr.y - 10, sr.width + 24, sr.height + 20};
    if (lp && CheckCollisionPointRec(m, hit))
        sliderDrag = true;
    if (sliderDrag && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        int v = std::clamp(int(std::lround((10.f + 90.f * (m.x - sr.x) / sr.width) / 5.f)) * 5, 10, 100);
        if (v != pct)
        {
            pct = v;
            app.cmd.setHaul(target, v);
        }
    }
    if (sliderDrag && lr)
        sliderDrag = false;
    return sliderDrag || (lp && CheckCollisionPointRec(m, hit));
}

bool ContextPanel::click(Vector2 m)
{
    bool hit = false;
    for (size_t i = 0; i < acts.size(); i++)
        if (CheckCollisionPointRec(m, button(int(i))))
        {
            hit = true;
            if (acts[i].enabled)
            {
                MenuAct a = acts[i];
                run(a);
            }
            break;
        }
    if (hit || CheckCollisionPointRec(m, rect()))
        return true;
    isOpen = false;
    return false;
}

void ContextPanel::draw()
{
    if (!isOpen)
        return;

    const ClientWorld &W = app.world;
    auto it = W.ents.find(target);
    if (it == W.ents.end())
        return;

    const Info &in = W.reg.get<Info>(it->second);
    Rectangle cr = rect();
    drawTMap(mCtx, cr);
    ui::panelTitle(cr, KINFO[in.kind].name, 20, .55f, 12);

    float x = cr.x + float(mCtx.padding) + 8, y = topY();
    for (auto &l : lines)
    {
        DrawText(l.first.c_str(), int(x), int(y), 15, l.second);
        y += 19;
    }
    if (slider)
        drawSlider(x, y);

    Vector2 m = ui::mouse();
    ui::CostStyle st = ui::paperCost();
    for (size_t i = 0; i < acts.size(); i++)
        drawAct(int(i), m, st);

    if (!next.empty())
        drawNextLevel(cr, x);
}

void ContextPanel::drawSlider(float x, float y) const
{
    DrawText(TextFormat("Auto-haul to Town Hall at %d%% full", pct), int(x), int(y) + 2, 13, INK);

    Rectangle sr = sliderRect();
    float k = (float(pct) - 10.f) / 90.f;
    DrawRectangleRounded(sr, .5f, 6, rgb(60, 40, 28, 220));
    DrawRectangleRounded({sr.x, sr.y, std::max(8.f, sr.width * k), sr.height}, .5f, 6, rgb(80, 170, 220));
    DrawCircleV({sr.x + sr.width * k, sr.y + sr.height / 2}, 10, rgb(250, 235, 200));
    DrawCircleLines(int(sr.x + sr.width * k), int(sr.y + sr.height / 2), 10, rgb(60, 40, 28));
}

void ContextPanel::drawAct(int i, Vector2 m, const ui::CostStyle &st) const
{
    const ClientWorld &W = app.world;
    const MenuAct &a = acts[size_t(i)];
    Rectangle b = button(i);
    ui::drawButton(b, a.label, a.enabled, a.red, CheckCollisionPointRec(m, b),
                   IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, b));
    if (!a.hasCost)
        return;

    Rectangle ca = costArea(i);
    float noteH = a.note.empty() ? 0.f : NOTE_FS + 3;
    Vector2 cs = ui::measureCosts(a.cost, ca.width, st);
    bool inlineNote = !a.note.empty() && cs.x + 8 + float(MeasureText(a.note.c_str(), int(NOTE_FS))) <= ca.width;
    float blockH = cs.y + (inlineNote || a.note.empty() ? 0.f : noteH);
    float cy = ca.y + (ca.height - blockH) / 2;
    cs = ui::drawCosts(a.cost, ca.x, cy, ca.width, W.res, st);
    if (a.note.empty())
        return;

    Color nc = a.note.find("WANTED") != std::string::npos ? rgb(30, 120, 40) : rgb(120, 90, 60);
    if (inlineNote)
        DrawText(a.note.c_str(), int(ca.x + cs.x + 8), int(cy + (cs.y - NOTE_FS) / 2 + 1), int(NOTE_FS), nc);
    else
        DrawText(a.note.c_str(), int(ca.x), int(cy + cs.y + 3), int(NOTE_FS), nc);
}

void ContextPanel::drawNextLevel(Rectangle cr, float x) const
{
    const ClientWorld &W = app.world;
    float pad = float(mCtx.padding);
    float ny = button(int(acts.size())).y + 4;
    DrawText("Next level:", int(x), int(ny), 14, rgb(30, 90, 40));
    ny += 17;
    for (auto &t : next)
    {
        DrawText(t.c_str(), int(x) + 6, int(ny), 13, INK);
        ny += 16;
    }

    float px = cr.x + cr.width - pad - 46 * float(nextUnits.size());
    for (size_t u = 0; u < nextUnits.size(); u++)
    {
        Anim a = unitAnim(nextUnits[u], W.me, US_Idle, 0, 0, false, 1, GetTime());
        float sc = a.fw > 200 ? .26f : .42f;
        drawAnim(a, frameFor(a, GetTime(), float(u)), px + 46 * float(u) + 22, ny - 6, false, sc);
    }
}
