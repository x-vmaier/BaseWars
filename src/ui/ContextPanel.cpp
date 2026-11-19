#include "ui/ContextPanel.hpp"

#include "client/App.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/UnitAnims.hpp"
#include "ui/GameText.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

#include <cmath>

void ContextPanel::load() { mCtx = loadTMap("context_panel"); }

void ContextPanel::open(uint32_t id, Vector2 at) {
    target = id;
    isOpen = true;
    pos = {at.x + 12, at.y - 20};
}

// ---------------------------------------------------------------- content
void ContextPanel::rebuild() {
    ClientWorld& W = app.world;
    acts.clear();
    lines.clear();
    next.clear();
    nextUnits.clear();
    slider = false;
    quota = false;
    auto it = W.ents.find(target);
    if (it == W.ents.end()) { isOpen = false; return; }
    const Info& in = W.reg.get<Info>(it->second);
    bool mine = in.owner == W.me, done = in.prog >= 100;
    Color ink = rgb(70, 45, 30);
    auto line = [&](const std::string& t, Color c) { lines.push_back({t, c}); };
    line(std::string("Owner: ") + W.nameOf(in.owner), mine ? rgb(30, 90, 40) : (isFactionOwner(in.owner) ? rgb(60, 50, 110) : rgb(150, 40, 30)));
    if (isFactionOwner(in.owner)) {
        int rp = W.facRep[in.owner - FACTION_BASE];
        line(TextFormat("Reputation: %s (%d)", repTier(rp), rp), rp <= HOSTILE_REP ? rgb(170, 40, 30) : (rp >= FRIENDLY_REP ? rgb(30, 110, 40) : ink));
        line(rp <= HOSTILE_REP ? "They will fight you on sight." : (rp >= FRIENDLY_REP ? "They will aid you if attacked." : "Gifts earn their trust."), ink);
    }
    if (in.prog < 100) line("Under construction " + std::to_string(in.prog) + "%", ink);
    line(TextFormat("Health %d / %d", int(in.hp), int(in.maxhp)), ink);
    if (mine || W.alliedWith(in.owner)) {
        line(TextFormat("Level %d / %d", in.aux, MAX_LEVEL), ink);
        Pos c = {W.reg.get<Goal>(it->second).x, W.reg.get<Goal>(it->second).y};
        int b = W.biomeAt(int(std::floor(c.x)), int(std::floor(c.y)));
        line(std::string("Biome: ") + (b == INVALID_BIOME ? "?" : BIOME_NAME[b]), ink);
        if (isResourceBuilding(in.kind)) {
            Yield yv = yieldOf(in.kind, b, in.kind == K_Harbour);
            std::string pr;
            int goldN = in.kind == K_Mine ? W.goldNear(c) : 0;                          // nearby gold deposits boost mines
            float gb = 1.f + goldBonusOf(goldN);
            for (int r = 0; r < R_Count; r++) if (yv.r[r] > 0) pr += TextFormat("%s %.1f/s  ", RES_NAME[r], yv.r[r] * levelMul(in.aux) * gb);
            line("Output: " + pr, ink);
            if (in.kind == K_Mine) line(goldN ? TextFormat("Gold deposits nearby: %d (+%d%% output)", goldN, int(std::lround((gb - 1.f) * 100.f))) : "No gold deposits nearby (+20% each, max +100%)", goldN ? rgb(150, 105, 0) : rgb(130, 90, 30));
            bool pref = preferredBiome(in.kind, b);
            line(pref ? "Preferred biome: BONUS ACTIVE" : (in.kind == K_Lumber ? "Best in: Forest" : (in.kind == K_Mine ? "Best in: Highlands, Desert" : "Coastal fishing")), pref ? rgb(30, 110, 40) : rgb(130, 90, 30));
            line(TextFormat("Stock waiting: %d", in.stock), ink);
        } else if (isStorage(in.kind)) {
            float cap = storageCap(in.kind, in.aux);
            if (in.kind == K_TownHall) line(TextFormat("Storage: %d / %d", int(W.res[0] + W.res[1] + W.res[2] + W.res[3]), int(cap)), ink);
            else line(TextFormat("Stored: %d / %d (%d%%)", int(in.stock), int(cap), int(in.stock * 100.f / cap)), ink);
            if (KINFO[in.kind].dps > 0) line(TextFormat("Defence %.0f dps, territory %d", KINFO[in.kind].dps * levelMul(in.aux), anchorRadius(in.kind)), ink);
            if (mine && done && (in.kind == K_Outpost || in.kind == K_Storage)) { slider = true; if (!sliderDrag) pct = in.aim; }
        } else if (in.kind == K_Campfire) line("Warms units in the snow", ink);
    }
    if (mine && done) {
        Kind k = in.kind;
        for (int u = K_Pawn; u < K_Count; u++)
            if (canTrain(k, Kind(u))) acts.push_back({std::string("Build ") + KINFO[u].name + "  " + costText(KINFO[u].cost), 1, target, uint32_t(u), W.affordable(KINFO[u].cost), false});
        if (k != K_Campfire) {
            float c[R_Count];
            for (int r = 0; r < R_Count; r++) c[r] = float(mapUpgradeCost(in, r));
            acts.push_back({in.aux >= MAX_LEVEL ? "Max level reached" : "Upgrade to Lv" + std::to_string(in.aux + 1) + "  " + costText(c), 2, target, 0, in.aux < MAX_LEVEL && W.affordable(c), false});
            nextLevelInfo(in, next, nextUnits);
        }
    } else if (!mine) {
        if (isFactionOwner(in.owner)) {
            static const char* nm[R_Count] = {"Wood", "Gold", "Stone", "Meat"};
            for (int r = 0; r < R_Count; r++) acts.push_back({std::string("Help: give 50 ") + nm[r], 6, uint32_t(in.owner - FACTION_BASE), uint32_t(r), W.res[r] >= 50.f, false});
        }
        acts.push_back({"Attack this target", 3, target, 0, true, true});
        if (in.owner > 0 && !isFactionOwner(in.owner)) {
            auto p = W.peerOf(in.owner);
            uint8_t fl = p ? p->flags : 0;
            if (fl & 2) acts.push_back({"Break alliance", 5, uint32_t(in.owner), 0, true, true});
            else if (fl & 4) acts.push_back({"Accept alliance", 4, uint32_t(in.owner), 0, true, false});
            else if (fl & 8) acts.push_back({"Alliance invite pending", 4, uint32_t(in.owner), 0, false, false});
            else acts.push_back({"Invite to alliance", 4, uint32_t(in.owner), 0, true, false});
        }
    }
    baseContext(in, mine, done);
}

// fire / disabled / moving lines, training queue, defense quota slider, move town, relocate
void ContextPanel::baseContext(const Info& in, bool mine, bool done) {
    const ClientWorld& W = app.world;
    if (!isBuilding(in.kind)) return;
    Color red = rgb(170, 40, 30), ink = rgb(70, 45, 30);
    bool burning = in.state & BLD_BURNING, dis = in.state & BLD_DISABLED, mov = in.state & BLD_MOVING;
    if (burning) lines.push_back({"ON FIRE! Idle units will put it out.", red});
    if (mov) lines.push_back({in.kind == K_TownHall ? "Town is being moved to a new site." : "Being relocated.", rgb(120, 80, 160)});
    else if (dis) lines.push_back({"NON-FUNCTIONAL: outside your territory", red});
    if (!mine || !done) return;
    auto q = W.queues.find(target);
    if (q != W.queues.end() && !q->second.kinds.empty()) {
        std::string s = "Queue:";
        for (size_t i = 0; i < q->second.kinds.size(); i++) s += std::string(i ? ", " : " ") + KINFO[q->second.kinds[i]].name + (i == 0 ? TextFormat(" %d%%", q->second.pct) : "");
        lines.push_back({s, ink});
    }
    if (in.kind == K_TownHall && !mov) {
        slider = true;
        quota = true;
        if (!sliderDrag) pct = W.myQuota;
        bool can = !W.townMoving && W.affordable(TOWN_MOVE_COST);
        acts.push_back({"Move town  " + costText(TOWN_MOVE_COST), 20, target, 0, can, false});
    }
    if (dis && !mov && in.kind != K_TownHall && in.kind != K_Campfire) {
        bool can = W.affordable(KINFO[in.kind].cost) && !W.townMoving;
        acts.push_back({"Relocate  " + costText(KINFO[in.kind].cost), 21, target, 0, can, false});
    }
    if (dis || mov) for (auto& a : acts) if (a.act == 1 || a.act == 2) a.enabled = false;
}

void ContextPanel::run(const MenuAct& a) {
    Commands& cmd = app.cmd;
    switch (a.act) {
    case 1: cmd.train(a.a, Kind(a.b)); break;
    case 2: cmd.upgrade(a.a); break;
    case 3: cmd.attack(a.a); break;
    case 4: cmd.ally(int(a.a), AA_Invite); break;
    case 5: cmd.ally(int(a.a), AA_Break); break;
    case 6: cmd.donate(int(a.a), int(a.b), 50.f); break;
    default: if (!baseRun(a)) isOpen = false; break;
    }
}
bool ContextPanel::baseRun(const MenuAct& a) {
    auto it = app.world.ents.find(a.a);
    switch (a.act) {
    case 20: app.placement.begin(Placement::MoveTown, K_TownHall, 0); return true;
    case 21: if (it != app.world.ents.end()) app.placement.begin(Placement::Relocate, app.world.reg.get<Info>(it->second).kind, a.a); return true;
    default: return false;
    }
}

// ---------------------------------------------------------------- layout and input
Rectangle ContextPanel::rect() const {
    Vector2 s = mCtx.size();
    return {std::clamp(pos.x, 8.f, float(ui::screenW()) - s.x - 8), std::clamp(pos.y, 56.f, float(ui::screenH()) - s.y - 8), s.x, s.y};
}
float ContextPanel::topY() const { Rectangle r = rect(); return r.y + float(mCtx.padding) + 20; }
Rectangle ContextPanel::sliderRect() const { Rectangle r = rect(); float pad = float(mCtx.padding); return {r.x + pad + 12, topY() + 19.f * float(lines.size()) + 22, r.width - 2 * pad - 24, 14}; }
Rectangle ContextPanel::button(int i) const {
    Rectangle r = rect();
    float pad = float(mCtx.padding);
    return {r.x + pad + 6, topY() + 19.f * float(lines.size()) + (slider ? 50.f : 4.f) + 34.f * float(i), r.width - 2 * pad - 12, 30};
}
int ContextPanel::quotaFromMouse(float mx, const Rectangle& sr) { return std::clamp(int(std::lround(float(MAX_QUOTA_UI) * (mx - sr.x) / sr.width)), 0, MAX_QUOTA_UI); }

// auto-haul threshold (outposts / storage) or defense quota (Town Hall)
bool ContextPanel::sliderInput(Vector2 m, bool lp, bool lr) {
    if (!isOpen || !slider) return false;
    Rectangle sr = sliderRect();
    Rectangle hit{sr.x - 12, sr.y - 10, sr.width + 24, sr.height + 20};
    if (lp && CheckCollisionPointRec(m, hit)) sliderDrag = true;
    if (sliderDrag && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        int v = quota ? quotaFromMouse(m.x, sr) : std::clamp(int(std::lround((10.f + 90.f * (m.x - sr.x) / sr.width) / 5.f)) * 5, 10, 100);
        if (v != pct) { pct = v; if (quota) { app.world.myQuota = v; app.cmd.quota(v); } else app.cmd.setHaul(target, v); }
    }
    if (sliderDrag && lr) sliderDrag = false;
    return sliderDrag || (lp && CheckCollisionPointRec(m, hit));
}

bool ContextPanel::click(Vector2 m) {
    bool hit = false;
    for (size_t i = 0; i < acts.size(); i++)
        if (CheckCollisionPointRec(m, button(int(i)))) { hit = true; if (acts[i].enabled) { MenuAct a = acts[i]; run(a); } break; }
    if (hit || CheckCollisionPointRec(m, rect())) return true;
    isOpen = false;
    return false;
}

void ContextPanel::draw() {
    if (!isOpen) return;
    const ClientWorld& W = app.world;
    auto it = W.ents.find(target);
    if (it == W.ents.end()) return;
    Vector2 m = ui::mouse();
    const Info& in = W.reg.get<Info>(it->second);
    Rectangle cr = rect();
    drawTMap(mCtx, cr);
    ui::panelTitle(cr, KINFO[in.kind].name, 20, .55f, 12);
    float pad = float(mCtx.padding);
    float x = cr.x + pad + 8, y = topY();
    Color ink = rgb(70, 45, 30);
    for (auto& l : lines) { DrawText(l.first.c_str(), int(x), int(y), 15, l.second); y += 19; }
    if (slider) {
        if (quota) DrawText(TextFormat("Defense quota: %d troops per attacked clan mate", pct), int(x), int(y) + 2, 13, ink);
        else DrawText(TextFormat("Auto-haul to Town Hall at %d%% full", pct), int(x), int(y) + 2, 13, ink);
        Rectangle sr = sliderRect();
        DrawRectangleRounded(sr, .5f, 6, rgb(60, 40, 28, 220));
        float k = quota ? float(pct) / float(MAX_QUOTA_UI) : (float(pct) - 10.f) / 90.f;
        DrawRectangleRounded({sr.x, sr.y, std::max(8.f, sr.width * k), sr.height}, .5f, 6, rgb(80, 170, 220));
        DrawCircleV({sr.x + sr.width * k, sr.y + sr.height / 2}, 10, rgb(250, 235, 200));
        DrawCircleLines(int(sr.x + sr.width * k), int(sr.y + sr.height / 2), 10, rgb(60, 40, 28));
    }
    for (size_t i = 0; i < acts.size(); i++) {
        Rectangle b = button(int(i));
        ui::drawButton(b, acts[i].label, acts[i].enabled, acts[i].red, CheckCollisionPointRec(m, b), IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, b));
    }
    if (!next.empty()) {                                            // what the next level will change
        float ny = button(int(acts.size())).y + 4;
        DrawText("Next level:", int(x), int(ny), 14, rgb(30, 90, 40));
        ny += 17;
        for (auto& t : next) { DrawText(t.c_str(), int(x) + 6, int(ny), 13, ink); ny += 16; }
        float px = cr.x + cr.width - pad - 46 * float(nextUnits.size());
        for (size_t u = 0; u < nextUnits.size(); u++) {
            Anim a = unitAnim(nextUnits[u], W.me, US_Idle, 0, 0, false, 1, GetTime());
            float sc = a.fw > 200 ? .26f : .42f;
            drawAnim(a, frameFor(a, GetTime(), float(u)), px + 46 * float(u) + 22, ny - 6, false, sc);
        }
    }
}
