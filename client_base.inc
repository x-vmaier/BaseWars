// Client side of the base mechanics (included INSIDE `struct App` in client.cpp):
// burning / disabled / relocating buildings, firefighting + fire-arrow effects, training queues, clan alert toasts,
// support patrols on the maps, defense quota slider, homeless placement, town move and building relocation.

// ---------------------------------------------------------------- state
struct QEntry { std::vector<uint8_t> kinds; int pct = 0; };
std::unordered_map<uint32_t, QEntry> queues;                 // building uid -> training queue (S_Queue)
struct Toast { std::string text; float x, y; int type; double t; };
std::vector<Toast> toasts;                                    // clan alerts (click to jump)
int myQuota = 5, movePct = 0, placeMode = 0, patrolN = 5;     // placeMode: 0 normal, 1 homeless Town Hall, 2 move town, 3 relocate building
bool homeless = false, wasHomeless = false, townMoving = false, ctxQuota = false;
uint32_t placeTarget = 0;
struct BmMenu { bool open = false; Vector2 at{}; float tx = 0, ty = 0; std::string name; } bmMenu;

// ---------------------------------------------------------------- protocol
void cmdQuota(int q) { Writer w; w.put<uint8_t>(C_SetQuota).put<uint8_t>(uint8_t(q)); send(w); }
void cmdSupport(float x, float y, int n) { Writer w; w.put<uint8_t>(C_Support).put<float>(x).put<float>(y).put<uint8_t>(uint8_t(n)); send(w); }
void cmdMoveTown(IV t) { Writer w; w.put<uint8_t>(C_MoveTown).put<int32_t>(t.x).put<int32_t>(t.y); send(w); }
void cmdRelocate(uint32_t b, IV t) { Writer w; w.put<uint8_t>(C_Relocate).put<uint32_t>(b).put<int32_t>(t.x).put<int32_t>(t.y); send(w); }
void cmdDequeue(uint32_t b, int idx) { Writer w; w.put<uint8_t>(C_Dequeue).put<uint32_t>(b).put<uint8_t>(uint8_t(idx)); send(w); }

bool baseOnPacket(uint8_t type, Reader& r) {
    switch (type) {
    case S_Queue: {
        int n = r.get<uint8_t>();
        std::unordered_map<uint32_t, QEntry> q;
        for (int i = 0; i < n; i++) {
            uint32_t uid = r.get<uint32_t>();
            int cnt = r.get<uint8_t>();
            QEntry e;
            e.pct = r.get<uint8_t>();
            for (int j = 0; j < cnt; j++) e.kinds.push_back(r.get<uint8_t>());
            if (!r.ok) return true;
            q[uid] = e;
        }
        queues = q;
        return true;
    }
    case S_Alert: {
        Toast t{};
        t.type = r.get<uint8_t>();
        t.x = r.get<float>();
        t.y = r.get<float>();
        t.text = r.str();
        t.t = GetTime();
        if (!r.ok) return true;
        toasts.push_back(t);
        if (toasts.size() > 4) toasts.erase(toasts.begin());
        if (t.type == 2) cam.target = {t.x * TILE, t.y * TILE};          // lost our town: look at the clan
        return true;
    }
    case S_BaseInfo: {
        int q = r.get<uint8_t>(), hl = r.get<uint8_t>(), mv = r.get<uint8_t>(), pct = r.get<uint8_t>();
        if (!r.ok) return true;
        if (!sliderDrag) myQuota = q;
        homeless = hl != 0;
        townMoving = mv != 0;
        movePct = pct;
        if (homeless && !wasHomeless) beginPlace(1, K_TownHall, 0);
        if (!homeless && wasHomeless && placeMode == 1) buildKind = -1;
        wasHomeless = homeless;
        return true;
    }
    default: return false;
    }
}

// ---------------------------------------------------------------- placement modes (homeless hall, move town, relocate)
int effMode() const { return buildKind >= 0 ? placeMode : 0; }
int curPlaceMode() const { int m = effMode(); return m == 1 ? PM_HomelessHall : (m == 2 ? PM_MoveHall : PM_Normal); }
const float* placeCost(Kind k) const {
    static const float zero[R_Count] = {0, 0, 0, 0};
    int m = effMode();
    return m == 1 ? zero : (m == 2 ? TOWN_MOVE_COST : KINFO[k].cost);
}
int placeCheck(Kind k, IV t) { PW pw{*this}; return checkPlace(pw, k, t.x, t.y, curPlaceMode()); }
void cmdPlace(Kind k, IV t) {
    switch (effMode()) {
    case 2: cmdMoveTown(t); break;
    case 3: cmdRelocate(placeTarget, t); break;
    default: cmdBuild(k, t); break;                              // normal build / homeless Town Hall (the server tells them apart)
    }
}
void beginPlace(int mode, Kind k, uint32_t target) {
    placeMode = mode;
    placeTarget = target;
    buildKind = int(k);
    this->mode = M_None;
    ctxOpen = false;
    if (mode == 1) msg("Your town fell! Click to place a new Town Hall (free) within 120 tiles of a clan town.");
    else if (mode == 2) msg("Choose the new site for your Town Hall (500 Gold, 300 Wood, 200 Stone). Esc/right-click cancels.");
    else if (mode == 3) msg("Choose the new site for this building (normal cost, it keeps its level and stock).");
}
void drawPlaceExtras() {                                           // territory hints while placing a special building
    int m = effMode();
    if (m == 1) {
        for (auto& p : roster) {
            if (p.id == me || !p.hasLoc || !p.clan || p.clan != myClan()) continue;
            float r = CLAN_TOWN_RADIUS;
            DrawRectangleRec({(p.lx - r) * TILE, (p.ly - r) * TILE, 2 * r * TILE, 2 * r * TILE}, rgb(255, 170, 60, 14));
            DrawRectangleLinesEx({(p.lx - r) * TILE, (p.ly - r) * TILE, 2 * r * TILE, 2 * r * TILE}, 4, rgb(255, 190, 90, 190));
        }
    }
}
bool noFlag(const Info& i, uint8_t f) const { return !(i.state & f); }

// ---------------------------------------------------------------- picking / UI hit areas
Rectangle toastRect(int i) { return {float(GetScreenWidth()) / 2 - 200, 52.f + 52.f * float(i), 400, 46}; }
Rectangle homelessBanner() { return {float(GetScreenWidth()) / 2 - 230, float(GetScreenHeight()) - 128, 460, 56}; }
bool overBaseUI(Vector2 m) {
    for (size_t i = 0; i < toasts.size(); i++) if (CheckCollisionPointRec(m, toastRect(int(i)))) return true;
    if (homeless && CheckCollisionPointRec(m, homelessBanner())) return true;
    return false;
}
bool baseInput(Vector2 m) {                                          // returns true when a click was consumed
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    for (size_t i = 0; i < toasts.size(); i++)
        if (CheckCollisionPointRec(m, toastRect(int(i)))) { cam.target = {toasts[i].x * TILE, toasts[i].y * TILE}; toasts.erase(toasts.begin() + long(i)); return true; }
    if (homeless && CheckCollisionPointRec(m, homelessBanner())) { beginPlace(1, K_TownHall, 0); return true; }
    return false;
}

// ---------------------------------------------------------------- HUD: toasts, homeless banner, town move progress
void drawBaseUI() {
    double now = GetTime();
    std::erase_if(toasts, [&](const Toast& t) { return now - t.t > 14.0; });
    Vector2 m = GetMousePosition();
    for (size_t i = 0; i < toasts.size(); i++) {
        const Toast& t = toasts[i];
        Rectangle r = toastRect(int(i));
        float a = std::clamp(float(14.0 - (now - t.t)) / 2.f, 0.f, 1.f), pulse = t.type == 0 ? float(std::sin(now * 8) * .5 + .5) : 0.f;
        bool hov = CheckCollisionPointRec(m, r);
        Color base = t.type == 0 ? rgb(150, 36, 30, int(235 * a)) : rgb(60, 80, 140, int(235 * a));
        DrawRectangleRounded(r, .2f, 6, hov ? rgb(base.r + 30, base.g + 20, base.b + 20, base.a) : base);
        DrawRectangleRoundedLinesEx(r, .2f, 6, 2 + pulse * 2, rgb(255, 210, 120, int(255 * a)));
        std::string s = t.text;
        while (MeasureText(s.c_str(), 15) > int(r.width) - 20 && s.size() > 4) s.pop_back();
        DrawText(s.c_str(), int(r.x) + 10, int(r.y) + 7, 15, rgb(255, 255, 255, int(255 * a)));
        std::string loc = TextFormat("(%d, %d)  click to jump", int(t.x), int(t.y));
        DrawText(loc.c_str(), int(r.x) + 10, int(r.y) + 27, 12, rgb(255, 230, 170, int(255 * a)));
    }
    if (homeless) {
        Rectangle b = homelessBanner();
        bool hov = CheckCollisionPointRec(m, b);
        DrawRectangleRounded(b, .2f, 6, hov ? rgb(190, 100, 40, 240) : rgb(160, 80, 30, 235));
        DrawRectangleRoundedLinesEx(b, .2f, 6, 2, rgb(255, 220, 140));
        DrawText("You are homeless", int(b.x) + 14, int(b.y) + 8, 18, WHITE);
        DrawText("Click here, then place a new Town Hall near your clan (free).", int(b.x) + 14, int(b.y) + 32, 13, rgb(255, 235, 200));
    }
    if (townMoving) {
        Rectangle nr = nameRect();
        textShadow(TextFormat("Moving town: %d%%", movePct), int(nr.x) + 8, int(nr.y + nr.height) + 6, 15, rgb(255, 220, 120));
    }
    if (effMode() == 1 || effMode() == 2 || effMode() == 3) {
        std::string h = effMode() == 1 ? "Place your new Town Hall" : (effMode() == 2 ? "Place the moved Town Hall" : "Place the relocated building");
        textShadow(h, GetScreenWidth() / 2 - MeasureText(h.c_str(), 20) / 2, 16, 20, rgb(255, 230, 120));
    }
}

// ---------------------------------------------------------------- context panel additions
void baseContext(const Info& in, bool mine, bool done) {
    if (!isBuilding(in.kind)) return;
    Color red = rgb(170, 40, 30), ink = rgb(70, 45, 30);
    bool burning = in.state & BLD_BURNING, dis = in.state & BLD_DISABLED, mov = in.state & BLD_MOVING;
    if (burning) ctxLines.push_back({"ON FIRE! Idle units will put it out.", red});
    if (mov) ctxLines.push_back({in.kind == K_TownHall ? "Town is being moved to a new site." : "Being relocated.", rgb(120, 80, 160)});
    else if (dis) ctxLines.push_back({"NON-FUNCTIONAL: outside your territory", red});
    if (!mine || !done) return;
    auto q = queues.find(ctxTarget);
    if (q != queues.end() && !q->second.kinds.empty()) {
        std::string s = "Queue:";
        for (size_t i = 0; i < q->second.kinds.size(); i++) s += std::string(i ? ", " : " ") + KINFO[q->second.kinds[i]].name + (i == 0 ? TextFormat(" %d%%", q->second.pct) : "");
        ctxLines.push_back({s, ink});
    }
    if (in.kind == K_TownHall && !mov) {
        ctxSlider = true;
        ctxQuota = true;
        if (!sliderDrag) ctxPct = myQuota;
        bool can = !townMoving && affordable(TOWN_MOVE_COST);
        ctx.push_back({"Move town  " + costText(TOWN_MOVE_COST), 20, ctxTarget, 0, can, false});
    }
    if (dis && !mov && in.kind != K_TownHall && in.kind != K_Campfire) {
        bool can = affordable(KINFO[in.kind].cost) && !townMoving;
        ctx.push_back({"Relocate  " + costText(KINFO[in.kind].cost), 21, ctxTarget, 0, can, false});
    }
    if (dis || mov) for (auto& a : ctx) if (a.act == 1 || a.act == 2) a.enabled = false;
}
bool baseRunContext(const MenuAct& a) {
    auto it = ents.find(a.a);
    switch (a.act) {
    case 20: beginPlace(2, K_TownHall, 0); return true;
    case 21: if (it != ents.end()) beginPlace(3, reg.get<Info>(it->second).kind, a.a); return true;
    default: return false;
    }
}
int quotaFromMouse(float mx, const Rectangle& sr) { return std::clamp(int(std::lround(float(MAX_QUOTA_UI) * (mx - sr.x) / sr.width)), 0, MAX_QUOTA_UI); }
static constexpr int MAX_QUOTA_UI = 20;

// ---------------------------------------------------------------- training queue (Archery panel)
Rectangle queueSlot(int i) {
    Rectangle bb = trainBlock();
    float x0 = bb.x + 236, w = std::min(54.f, (bb.width - 246) / 5.f);
    return {x0 + float(i) * (w + 2), bb.y + 22, w, 62};
}
bool queueClick(Vector2 m) {                                          // click a queued unit to cancel it (refund)
    auto q = queues.find(trainTarget);
    if (q == queues.end()) return false;
    for (size_t i = 0; i < q->second.kinds.size(); i++) if (CheckCollisionPointRec(m, queueSlot(int(i)))) { cmdDequeue(trainTarget, int(i)); return true; }
    return false;
}
void drawQueueStrip(Vector2 m) {
    Rectangle bb = trainBlock();
    auto q = queues.find(trainTarget);
    size_t n = q == queues.end() ? 0 : q->second.kinds.size();
    Color ink = rgb(70, 45, 30);
    DrawText(TextFormat("Training queue %d/5%s", int(n), n ? "  (click to cancel)" : ""), int(bb.x) + 236, int(bb.y) + 4, 13, ink);
    double now = GetTime();
    for (int i = 0; i < 5; i++) {
        Rectangle r = queueSlot(i);
        bool filled = size_t(i) < n, hov = filled && CheckCollisionPointRec(m, r);
        DrawRectangleRounded(r, .15f, 5, filled ? (hov ? rgb(255, 225, 200) : rgb(250, 238, 210)) : rgb(210, 195, 165, 130));
        DrawRectangleRoundedLinesEx(r, .15f, 5, i == 0 && filled ? 3.f : 1.5f, i == 0 && filled ? rgb(60, 140, 220) : rgb(120, 90, 60));
        if (!filled) continue;
        Kind k = Kind(q->second.kinds[size_t(i)]);
        Anim a = unitAnim(k, me, US_Idle, 0, 0, false, 1, now);
        drawAnim(a, frameFor(a, now, float(i) * .6f), r.x + r.width / 2, r.y + 44, false, a.fw > 200 ? .26f : .42f, i == 0 ? WHITE : rgb(200, 200, 215));
        if (i == 0) {
            DrawRectangle(int(r.x) + 3, int(r.y + r.height) - 9, int(r.width) - 6, 6, rgb(60, 40, 28, 220));
            DrawRectangle(int(r.x) + 3, int(r.y + r.height) - 9, int((r.width - 6) * float(q->second.pct) / 100.f), 6, rgb(90, 200, 110));
        }
    }
}

// ---------------------------------------------------------------- world map: support patrols (right-click an allied / clan town)
struct BmCand { std::string name; float x, y; };
std::vector<BmCand> bmCandidates() {
    std::vector<BmCand> v;
    reg.view<Info, Goal>().each([&](const Info& i, const Goal& g) {
        if ((i.kind != K_TownHall && i.kind != K_Outpost) || i.owner == me || isFactionOwner(i.owner) || !alliedWith(i.owner) || i.prog < 100) return;
        v.push_back({nameOf(i.owner) + (i.kind == K_TownHall ? " (town)" : " (outpost)"), g.x, g.y});
    });
    for (auto& p : roster) {
        if (p.id == me || !p.hasLoc || !(p.flags & 2 || (p.clan && p.clan == myClan()))) continue;
        bool dup = false;
        for (auto& c : v) if (std::hypot(c.x - p.lx, c.y - p.ly) < 4.f) dup = true;
        if (!dup) v.push_back({p.name + " (town)", p.lx, p.ly});
    }
    return v;
}
Rectangle bmMenuRect() {
    float w = 250, h = 112;
    return {std::clamp(bmMenu.at.x, 4.f, float(GetScreenWidth()) - w - 4), std::clamp(bmMenu.at.y, 4.f, float(GetScreenHeight()) - h - 4), w, h};
}
Rectangle bmMinus() { Rectangle r = bmMenuRect(); return {r.x + 130, r.y + 36, 28, 26}; }
Rectangle bmPlus() { Rectangle r = bmMenuRect(); return {r.x + 212, r.y + 36, 28, 26}; }
Rectangle bmSend() { Rectangle r = bmMenuRect(); return {r.x + 10, r.y + 70, r.width - 20, 32}; }
bool bmRightClick(Vector2 m) {                                        // returns true when the click opened the menu
    Vector2 best{};
    float bd = 20.f;
    BmCand pick{};
    bool found = false;
    for (auto& c : bmCandidates()) {
        Vector2 sp = tileToMap(c.x, c.y);
        float d = std::hypot(sp.x - m.x, sp.y - m.y - 6.f);
        if (d < bd) { bd = d; pick = c; found = true; best = sp; }
    }
    if (!found) return false;
    bmMenu = {true, {m.x + 6, m.y + 6}, pick.x, pick.y, pick.name};
    (void)best;
    return true;
}
bool bmMenuInput(Vector2 m) {                                          // consumes all input while the menu is open
    if (!bmMenu.open) return false;
    if (IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { bmMenu.open = false; return true; }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(m, bmMinus())) patrolN = std::max(1, patrolN - 1);
        else if (CheckCollisionPointRec(m, bmPlus())) patrolN = std::min(MAX_QUOTA_UI, patrolN + 1);
        else if (CheckCollisionPointRec(m, bmSend())) { cmdSupport(bmMenu.tx, bmMenu.ty, patrolN); bmMenu.open = false; }
        else if (!CheckCollisionPointRec(m, bmMenuRect())) bmMenu.open = false;
    }
    return true;
}
void drawBmMenu() {
    if (!bmMenu.open) return;
    Vector2 m = GetMousePosition();
    Rectangle r = bmMenuRect();
    DrawRectangleRounded(r, .1f, 6, rgb(238, 222, 190, 250));
    DrawRectangleRoundedLinesEx(r, .1f, 6, 2, rgb(120, 90, 60));
    std::string t = bmMenu.name;
    while (MeasureText(t.c_str(), 15) > int(r.width) - 20 && t.size() > 4) t.pop_back();
    DrawText(t.c_str(), int(r.x) + 10, int(r.y) + 9, 15, rgb(70, 45, 30));
    DrawText("Troops:", int(r.x) + 12, int(r.y) + 41, 15, rgb(70, 45, 30));
    drawButton(bmMinus(), "-", true, false, CheckCollisionPointRec(m, bmMinus()), false, 16);
    DrawText(TextFormat("%d", patrolN), int(r.x) + 178 - MeasureText(TextFormat("%d", patrolN), 16) / 2, int(r.y) + 41, 16, rgb(70, 45, 30));
    drawButton(bmPlus(), "+", true, false, CheckCollisionPointRec(m, bmPlus()), false, 16);
    drawButton(bmSend(), TextFormat("Send support patrol (%d)", patrolN), true, false, CheckCollisionPointRec(m, bmSend()), false, 14);
}
// support patrols are drawn on both maps with a shield-and-sword icon (orange: ours / allies, red: hostile)
void drawSupportMapIcons(Rectangle in, Vector2 c, float tpp, bool mini) {
    double now = GetTime();
    reg.view<Info, RPos>().each([&](const Info& i, const RPos& p) {
        if (isBuilding(i.kind) || !(i.state & UNIT_SUPPORT_FLAG)) return;
        Vector2 sp{in.x + in.width / 2 + (p.x - c.x) / tpp, in.y + in.height / 2 + (p.y - c.y) / tpp};
        if (sp.x < in.x + 3 || sp.y < in.y + 3 || sp.x > in.x + in.width - 3 || sp.y > in.y + in.height - 3) return;
        bool friendly = alliedWith(i.owner);
        Color col = friendly ? rgb(255, 170, 40) : rgb(255, 70, 60);
        float s = mini ? 4.f : 9.f + (friendly ? 0.f : 2.f * float(std::sin(now * 6) * .5 + .5));
        Vector2 a{sp.x, sp.y - s}, b{sp.x + s, sp.y}, d{sp.x, sp.y + s}, e{sp.x - s, sp.y};
        DrawTriangle(a, e, b, Color{col.r, col.g, col.b, 230});
        DrawTriangle(e, d, b, Color{col.r, col.g, col.b, 230});
        DrawTriangleLines(a, e, b, BLACK);
        DrawLineV(e, d, BLACK);
        DrawLineV(d, b, BLACK);
        if (mini) return;
        DrawLineEx({sp.x - 4, sp.y + 4}, {sp.x + 4, sp.y - 4}, 2, WHITE);          // crossed sword
        DrawLineEx({sp.x - 4, sp.y - 4}, {sp.x + 4, sp.y + 4}, 1, rgb(60, 40, 20));
        if (tpp <= 2.f) DrawText(friendly ? "Patrol" : "Hostile patrol", int(sp.x) + 12, int(sp.y) - 5, 10, friendly ? rgb(255, 215, 130) : rgb(255, 150, 140));
    });
}

// ---------------------------------------------------------------- world effects
Anim extinguishAnim(Kind k, int owner) {                               // bucket / hammer duty: pawns scrub, soldiers stamp at the flames
    std::string base = std::string("Units/") + COLOR_NAME[colorIdx(owner)] + " Units/";
    switch (k) {
    case K_Warrior: return mkAnim(base + "Warrior/Warrior_Run.png", 192, 192, 14, 96, 135);
    case K_Archer: return mkAnim(base + "Archer/Archer_Run.png", 192, 192, 14, 96, 135);
    case K_Lancer: return mkAnim(base + "Lancer/Lancer_Run.png", 320, 320, 14, 160, 198);
    case K_Monk: return mkAnim(base + "Monk/Run.png", 192, 192, 12, 96, 135);
    default: return mkAnim(base + "Pawn/Pawn_Interact Hammer.png", 192, 192, 8, 96, 135);
    }
}
void drawExtinguishSplash(float px, float py, bool left, double t, uint32_t id) {
    Anim s = mkAnim("Particle FX/Water Splash.png", 192, 192, 10, 96, 150);
    float dir = left ? -1.f : 1.f;
    drawAnim(s, frameFor(s, t, float(id % 5)), px + dir * 34.f, py + 8, false, .42f, rgb(255, 255, 255, 220));
    for (int k = 0; k < 4; k++) {                                       // droplets arcing towards the fire
        float ph = float(std::fmod(t * 2.2 + double(k) * .25 + double(id % 7) * .1, 1.0));
        DrawCircleV({px + dir * (14.f + ph * 34.f), py - 38.f - std::sin(ph * 3.14159f) * 22.f + ph * 28.f}, 3.f, rgb(120, 190, 255, int(230 * (1.f - ph * .5f))));
    }
}
void baseBuildingFx(const Info& i, uint32_t id, float cx, float bottom, int fw, int fh, float sortY, std::vector<DrawItem>& items, double t) {
    bool burning = i.state & BLD_BURNING, dis = (i.state & BLD_DISABLED) != 0, mov = (i.state & BLD_MOVING) != 0;
    if (burning)
        items.push_back({sortY + 3.f, [=, this]() {
            float w = float(fw) * TILE, h = float(fh) * TILE;
            Anim f = mkAnim("Particle FX/Fire_02.png", 64, 64, 12, 32, 62), f1 = mkAnim("Particle FX/Fire_01.png", 64, 64, 12, 32, 62);
            BeginBlendMode(BLEND_ADDITIVE);
            DrawCircleGradient({cx, bottom - h * .55f}, w * .55f, rgb(255, 120, 30, 90), rgb(255, 60, 0, 0));
            EndBlendMode();
            for (int k = 0; k < 4 + fw; k++) {
                float fx2 = cx - w * .38f + w * .76f * float(k) / float(3 + fw), fy = bottom - 20.f - h * .25f - float((k * 37 + int(id)) % 40);
                drawAnim((k & 1) ? f : f1, frameFor(f, t, float(k) * .37f + float(id % 5)), fx2, fy, false, 1.5f + float(k % 3) * .35f);
            }
            Anim d = mkAnim("Particle FX/Dust_02.png", 64, 64, 9, 32, 60);
            for (int k = 0; k < 3; k++) {
                float ph = float(std::fmod(t * .6 + double(k) * .33 + double(id % 9) * .1, 1.0));
                drawAnim(d, int(ph * 9.f) % 9, cx - w * .25f + float(k) * w * .25f, bottom - h - 50.f - ph * 70.f, false, 2.1f, rgb(70, 70, 76, int(180 * (1.f - ph))));
            }
        }});
    if (dis)
        items.push_back({sortY + 6.f, [=, this]() {                              // red cross: this building does not work
            float h = float(fh) * TILE, y = bottom - h * .5f - 40.f, r = 24.f + float(std::sin(t * 4.0)) * 1.5f;
            DrawCircleV({cx, y}, r + 3.f, rgb(255, 255, 255, mov ? 90 : 235));
            DrawCircleV({cx, y}, r, rgb(40, 20, 20, mov ? 90 : 210));
            DrawLineEx({cx - r * .62f, y - r * .62f}, {cx + r * .62f, y + r * .62f}, 9, rgb(230, 40, 40, mov ? 110 : 255));
            DrawLineEx({cx - r * .62f, y + r * .62f}, {cx + r * .62f, y - r * .62f}, 9, rgb(230, 40, 40, mov ? 110 : 255));
            if (!mov) { std::string s = "No territory"; textShadow(s, int(cx) - MeasureText(s.c_str(), 12) / 2, int(y + r + 6), 12, rgb(255, 150, 130)); }
        }});
    if (mov && !dis)
        items.push_back({sortY + 6.f, [=, this]() { std::string s = i.kind == K_TownHall ? "Moving..." : "Relocating..."; textShadow(s, int(cx) - MeasureText(s.c_str(), 14) / 2, int(bottom - float(fh) * TILE * .5f - 60.f), 14, rgb(210, 180, 255)); }});
}
void baseDrawFx(const Fx& f, std::vector<DrawItem>& items, double t) {
    double age = t - f.t;
    Fx fx = f;
    switch (f.type) {
    case E_FireArrow: {
        float dur = std::max(.12f, std::hypot(f.x2 - f.x, f.y2 - f.y) / 16.f);
        if (age < dur)
            items.push_back({f.y2 * TILE + 50, [=, this]() {
                float k = float(age) / dur;
                Vector2 a{fx.x * TILE, fx.y * TILE - 30}, b{fx.x2 * TILE, fx.y2 * TILE - 24};
                Vector2 p{a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k - std::sin(k * 3.14159f) * 22.f};
                float rot = std::atan2(b.y - a.y, b.x - a.x) * 57.2958f;
                Texture2D ar = tex("Units/Blue Units/Archer/Arrow.png");
                BeginBlendMode(BLEND_ADDITIVE);
                DrawCircleGradient({p.x, p.y}, 22.f, rgb(255, 150, 40, 170), rgb(255, 80, 0, 0));
                EndBlendMode();
                DrawTexturePro(ar, {0, 0, 64, 64}, {p.x, p.y, 48, 48}, {24, 24}, rot, rgb(255, 170, 90));
                Anim fl = mkAnim("Particle FX/Fire_03.png", 64, 64, 12, 32, 62);
                drawAnim(fl, frameFor(fl, t), p.x + std::cos(rot / 57.2958f) * 16.f, p.y + 14.f, false, .7f);
            }});
        else if (age < dur + .7)
            items.push_back({f.y2 * TILE + 50, [=, this]() {
                Anim fl = mkAnim("Particle FX/Fire_02.png", 64, 64, 12, 32, 62);
                drawAnim(fl, frameFor(fl, t), fx.x2 * TILE, fx.y2 * TILE + 6.f, false, 1.2f, rgb(255, 255, 255, int(255 * (1.0 - (age - dur) / .7))));
            }});
        break;
    }
    case E_Extinguish:
        items.push_back({f.y * TILE + 60, [=, this]() {
            Anim s = mkAnim("Particle FX/Water Splash.png", 192, 192, 12, 96, 130);
            int fr = int(age * 12);
            if (fr < s.n) drawAnim(s, fr, fx.x * TILE, fx.y * TILE + 4.f, false, .75f);
            Anim d = mkAnim("Particle FX/Dust_02.png", 64, 64, 9, 32, 60);
            int df = int(age * 9);
            if (df < d.n) drawAnim(d, df, fx.x * TILE, fx.y * TILE - 30.f - float(age) * 30.f, false, 1.6f, rgb(215, 220, 230, 150));
        }});
        break;
    default: break;
    }
}
