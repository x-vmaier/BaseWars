// client_ships.inc - ships (tiers, capacity, embark / disembark, ship panel) and path lines. Included INSIDE `struct App` (client.cpp).
//
// Server data used:  S_State ships: aux = tier, prog = troops aboard, stock = resources aboard.
//                    units aboard a ship arrive with prog == UNIT_ABOARD_PROG and are not drawn / picked / selected.
//                    S_ShipInfo: passenger and cargo breakdown of our own ships.  S_Paths: path lines (owner + clan/allies only).
// Input: right-click an own ship with troops selected = embark;  right-click it with no troops selected = ship panel.

struct ShipDetail { int tier = 1, kinds[5]{}, res[R_Count]{}, voyage = 0; };
std::unordered_map<uint32_t, ShipDetail> shipDetails;
std::unordered_map<uint32_t, std::vector<IV>> pathLines;
uint32_t shipPanel = 0;

static bool aboardUnit(const Info& i) { return !isBuilding(i.kind) && i.prog == UNIT_ABOARD_PROG; }

void onShipsInfo(Reader& r) {
    int n = r.get<uint8_t>();
    std::unordered_map<uint32_t, ShipDetail> m;
    for (int i = 0; i < n; i++) {
        uint32_t id = r.get<uint32_t>();
        ShipDetail d;
        d.tier = r.get<uint8_t>();
        for (int& k : d.kinds) k = r.get<uint8_t>();
        for (int& v : d.res) v = r.get<uint16_t>();
        d.voyage = r.get<uint8_t>();
        if (!r.ok) return;
        m[id] = d;
    }
    shipDetails = std::move(m);
}
void onShipsPaths(Reader& r) {
    int n = r.get<uint16_t>();
    for (int i = 0; i < n; i++) {
        uint32_t id = r.get<uint32_t>();
        int np = r.get<uint8_t>();
        std::vector<IV> v;
        for (int j = 0; j < np; j++) { int x = r.get<int32_t>(), y = r.get<int32_t>(); v.push_back({x, y}); }
        if (!r.ok) return;
        if (np) pathLines[id] = std::move(v); else pathLines.erase(id);
    }
}

// ---------------------------------------------------------------- path lines (thin, light gray, translucent; only what the server sent us)
void queuePathLines(std::vector<DrawItem>& items) {
    std::erase_if(pathLines, [&](auto& kv) { return ents.find(kv.first) == ents.end(); });
    if (pathLines.empty()) return;
    items.push_back({-1e8f, [this]() {
        float th = std::max(1.6f, 2.2f / std::max(.4f, cam.zoom));
        for (auto& [id, pts] : pathLines) {
            auto it = ents.find(id);
            if (it == ents.end() || pts.empty()) continue;
            const RPos& rp = reg.get<RPos>(it->second);
            Vector2 prev{rp.x * TILE, rp.y * TILE};
            for (auto& q : pts) {
                Vector2 nx{(float(q.x) + .5f) * TILE, (float(q.y) + .5f) * TILE};
                DrawLineEx(prev, nx, th, rgb(215, 215, 220, 130));
                prev = nx;
            }
            DrawCircleV(prev, th * 1.6f, rgb(225, 225, 230, 150));
        }
    }});
}

// ---------------------------------------------------------------- ship drawing (three visibly different tiers)
void drawShip(float px, float py, int owner, bool flip, bool hover, double t, uint32_t id, int tier, int troops) {
    tier = std::clamp(tier, 1, 3);
    float s = tier == 1 ? 1.f : (tier == 2 ? 1.3f : 1.6f);
    float bob = std::sin(float(t) * 2.f + float(id)) * 2.5f;
    py += bob;
    float d = flip ? -1.f : 1.f;
    Color hullA = tier == 3 ? rgb(92, 62, 42) : (tier == 2 ? rgb(112, 78, 50) : rgb(122, 84, 48));
    Color hullB = tier == 3 ? rgb(108, 72, 48) : (tier == 2 ? rgb(128, 90, 56) : rgb(138, 96, 56));
    Color edge = rgb(70, 44, 26);
    DrawEllipse(int(px), int(py + 6 * s), int(46 * s), int(12 * s), rgb(0, 40, 80, 70));
    Vector2 a{px - 44 * s * d, py - 10 * s}, b{px + 44 * s * d, py - 10 * s}, c2{px + 30 * s * d, py + 8 * s}, e{px - 30 * s * d, py + 8 * s};
    DrawTriangle(a, e, c2, hullA);
    DrawTriangle(a, c2, b, hullB);
    DrawLineEx(a, b, 3, tier == 3 ? rgb(214, 170, 60) : rgb(88, 58, 32));       // gold rail on the galleon
    DrawLineEx(a, e, 2, edge);
    DrawLineEx(e, c2, 2, edge);
    DrawLineEx(c2, b, 2, edge);
    if (tier >= 2)                                                             // shield row along the rail
        for (int k = 0; k < 5; k++) {
            float u = -.7f + float(k) * .35f;
            DrawCircleV({px + 40 * s * u * d, py - 6 * s}, 3.6f * s, k % 2 ? teamColor(owner) : rgb(235, 230, 210));
        }
    if (tier == 3) {                                                           // figurehead + stern cabin
        DrawTriangle({px + 44 * s * d, py - 10 * s}, {px + 56 * s * d, py - 16 * s}, {px + 40 * s * d, py - 2 * s}, rgb(214, 170, 60));
        DrawRectangle(int(px - 40 * s * d - (flip ? 0 : 0) - 12), int(py - 26 * s), 24, int(14 * s), rgb(96, 66, 38));
        DrawRectangleLines(int(px - 40 * s * d - 12), int(py - 26 * s), 24, int(14 * s), edge);
    }
    int masts = tier;
    for (int m = 0; m < masts; m++) {
        float mx = px + (masts == 1 ? 0.f : (float(m) - float(masts - 1) * .5f) * 30.f * s) * d;
        float h = (m == 0 || masts == 1 ? 56.f : 46.f) * s * (masts == 1 ? 1.f : .85f);
        DrawRectangle(int(mx - 2), int(py - 8 * s - h), 4, int(h), rgb(96, 66, 38));
        Vector2 s1{mx + 2, py - 6 * s - h}, s2{mx + 2 + 34 * s * d * (masts == 1 ? 1.f : .8f), py - 8 * s - h * .5f}, s3{mx + 2, py - 12 * s};
        Color sail = tier == 3 ? rgb(248, 242, 224) : rgb(240, 236, 220);
        if (d > 0) DrawTriangle(s1, s3, s2, sail); else DrawTriangle(s1, s2, s3, sail);
        if (tier == 3) DrawLineEx({s1.x, s1.y + 14 * s}, {s2.x, s2.y - 8 * s}, 2, teamColor(owner));
        if (m == 0) {
            Vector2 f1{mx - 2, py - 6 * s - h}, f2{mx - 2 - 18 * s * d, py - h + 2 * s}, f3{mx - 2, py - h + 14 * s};
            if (d > 0) DrawTriangle(f1, f3, f2, teamColor(owner)); else DrawTriangle(f1, f2, f3, teamColor(owner));
        }
    }
    int show = std::min(troops, 4);                                            // a few crew figures stand on deck while troops are aboard
    if (show > 0 && troops != UNIT_ABOARD_PROG) {
        Anim w = mkAnim(std::string("Units/") + COLOR_NAME[colorIdx(owner)] + " Units/Warrior/Warrior_Idle.png", 192, 192, 7, 96, 135);
        for (int k = 0; k < show; k++) drawAnim(w, frameFor(w, t, float(id % 5 + k)), px + (float(k) - float(show - 1) * .5f) * 15.f * s * d, py - 6 * s, flip, .42f * (tier == 1 ? 1.f : 1.1f), WHITE, hover ? 1 : 0, rgb(255, 255, 255));
    }
    if (hover) DrawEllipseLines(int(px), int(py - 4), int(50 * s), int(16 * s), WHITE);
}
// Overlay above an own/allied ship: tier name and load.
void drawShipLabel(float px, float py, const Info& i, bool selected, bool hover) {
    if (!(i.owner == me || alliedWith(i.owner)) || (!selected && !hover && i.prog == 0)) return;
    int tier = shipTier(i.aux ? i.aux : 1);
    float s = tier == 1 ? 1.f : (tier == 2 ? 1.3f : 1.6f);
    std::string t = TextFormat("%d/%d", int(i.prog), shipTroopCap(tier));
    textShadow(t, int(px) - MeasureText(t.c_str(), 14) / 2, int(py - 78 * s), 14, i.prog >= shipTroopCap(tier) ? rgb(255, 170, 120) : rgb(235, 245, 255));
}
std::string shipTooltip(const Info& in) {
    if (in.kind != K_Ship) return "";
    int tier = shipTier(in.aux ? in.aux : 1);
    std::string s = std::string(" - ") + SHIP_NAME[tier - 1] + " (tier " + std::to_string(tier) + ")";
    if (in.owner == me || alliedWith(in.owner))
        s += TextFormat("  Troops %d/%d  Cargo %d/%d", int(in.prog), shipTroopCap(tier), int(in.stock), shipResCap(tier));
    return s;
}
bool coastNearShip(const RPos& p) {                                           // any walkable land within 2 tiles (the server uses the same radius)
    int cx = int(std::floor(p.x)), cy = int(std::floor(p.y));
    for (int y = cy - 2; y <= cy + 2; y++)
        for (int x = cx - 2; x <= cx + 2; x++) {
            int b = biomeAt(x, y);
            if (isLand(b) && !isCliffTile(b, biomeAt(x, y - 1))) return true;
        }
    return false;
}

// ---------------------------------------------------------------- commands
std::vector<uint32_t> selTroops() {
    std::vector<uint32_t> v;
    for (auto id : sel) {
        auto it = ents.find(id);
        if (it == ents.end()) continue;
        const Info& i = reg.get<Info>(it->second);
        if (isTroop(i.kind) && i.owner == me && !aboardUnit(i) && v.size() < 255) v.push_back(id);
    }
    return v;
}
void cmdBoard(uint32_t ship, const std::vector<uint32_t>& us) {
    Writer w;
    w.put<uint8_t>(C_Board).put<uint8_t>(uint8_t(us.size()));
    for (auto u : us) w.put<uint32_t>(u);
    w.put<uint32_t>(ship);
    send(w);
}
void cmdDisembark(uint32_t ship) { Writer w; w.put<uint8_t>(C_Disembark).put<uint32_t>(ship); send(w); }
// Right-click on `id`: true when the click was consumed (embark order given or panel opened).
bool shipRightClick(uint32_t id) {
    auto it = id ? ents.find(id) : ents.end();
    if (it == ents.end()) return false;
    const Info& in = reg.get<Info>(it->second);
    if (in.kind != K_Ship || in.owner != me) return false;
    auto tr = selTroops();
    if (!tr.empty()) {
        cmdBoard(id, tr);
        Vector2 w = {reg.get<RPos>(it->second).x * TILE, reg.get<RPos>(it->second).y * TILE};
        markers.push_back({w.x, w.y, GetTime()});
        int cap = shipTroopCap(in.aux ? in.aux : 1), free = cap - int(in.prog);
        if (int(tr.size()) > free) msg(TextFormat("The %s only has room for %d more troop(s).", SHIP_NAME[shipTier(in.aux ? in.aux : 1) - 1], std::max(0, free)));
        return true;
    }
    shipPanel = id;
    ctxOpen = false;
    return true;
}

// ---------------------------------------------------------------- ship panel
Rectangle shipPanelRect() { float w = 330, h = 262; return {float(GetScreenWidth()) / 2 - w / 2, float(GetScreenHeight()) - h - 24, w, h}; }
Rectangle shipPanelBtn(int i) { Rectangle r = shipPanelRect(); return {r.x + 14 + float(i) * 160, r.y + r.height - 46, 142, 34}; }
bool shipPanelOver(Vector2 m) { return shipPanel && CheckCollisionPointRec(m, shipPanelRect()); }
// Returns true when the input event was consumed by the panel.
bool shipUiInput(Vector2 m, bool lp, bool rp) {
    if (!shipPanel) return false;
    auto it = ents.find(shipPanel);
    if (it == ents.end() || reg.get<Info>(it->second).owner != me) { shipPanel = 0; return false; }
    if (IsKeyPressed(KEY_ESCAPE)) { shipPanel = 0; return true; }
    bool over = CheckCollisionPointRec(m, shipPanelRect());
    if (lp && over) {
        const Info& in = reg.get<Info>(it->second);
        if (CheckCollisionPointRec(m, shipPanelBtn(0))) {
            if (in.prog == 0) msg("Nobody is aboard.");
            else if (!coastNearShip(reg.get<RPos>(it->second))) msg("Move the ship next to a coastline to disembark.");
            else cmdDisembark(shipPanel);
        } else if (CheckCollisionPointRec(m, shipPanelBtn(1))) shipPanel = 0;
        return true;
    }
    if (lp && !over) shipPanel = 0;                           // clicking elsewhere closes it and still acts on the world
    if (rp && over) return true;
    return false;
}
void drawShipPanel() {
    if (!shipPanel) return;
    auto it = ents.find(shipPanel);
    if (it == ents.end()) { shipPanel = 0; return; }
    const Info& in = reg.get<Info>(it->second);
    const RPos& rp = reg.get<RPos>(it->second);
    Vector2 m = GetMousePosition();
    Rectangle r = shipPanelRect();
    int tier = shipTier(in.aux ? in.aux : 1), tcap = shipTroopCap(tier), rcap = shipResCap(tier);
    DrawRectangleRounded(r, .06f, 8, rgb(238, 222, 190));
    DrawRectangleRoundedLinesEx(r, .06f, 8, 3.f, rgb(120, 90, 60));
    Color ink = rgb(70, 45, 30);
    DrawText(TextFormat("%s  (tier %d)", SHIP_NAME[tier - 1], tier), int(r.x) + 16, int(r.y) + 12, 22, ink);
    DrawText(TextFormat("Health %d / %d", int(in.hp), int(in.maxhp)), int(r.x) + 16, int(r.y) + 40, 15, ink);
    auto bar = [&](float y, float frac, Color c) {
        Rectangle b{r.x + 16, y, r.width - 32, 14};
        DrawRectangleRec(b, rgb(200, 180, 140));
        DrawRectangleRec({b.x, b.y, b.width * std::clamp(frac, 0.f, 1.f), b.height}, c);
        DrawRectangleLinesEx(b, 1.5f, rgb(120, 90, 60));
    };
    auto dit = shipDetails.find(shipPanel);
    DrawText(TextFormat("Troops aboard: %d / %d", int(in.prog), tcap), int(r.x) + 16, int(r.y) + 64, 16, ink);
    bar(r.y + 84, float(in.prog) / float(tcap), in.prog >= tcap ? rgb(220, 120, 70) : rgb(90, 160, 220));
    std::string comp;
    if (dit != shipDetails.end()) {
        static const char* nm[5] = {"Pawn", "Warrior", "Archer", "Lancer", "Monk"};
        for (int k = 0; k < 5; k++) if (dit->second.kinds[k] > 0) comp += std::string(comp.empty() ? "" : ", ") + nm[k] + " x" + std::to_string(dit->second.kinds[k]);
    }
    DrawText(comp.empty() ? "Empty hold" : comp.c_str(), int(r.x) + 16, int(r.y) + 104, 14, rgb(100, 70, 50));
    DrawText(TextFormat("Cargo: %d / %d", int(in.stock), rcap), int(r.x) + 16, int(r.y) + 128, 16, ink);
    bar(r.y + 148, float(in.stock) / float(rcap), rgb(220, 180, 70));
    if (dit != shipDetails.end() && in.stock > 0)
        DrawText(TextFormat("Wood %d  Gold %d  Stone %d  Meat %d", dit->second.res[0], dit->second.res[1], dit->second.res[2], dit->second.res[3]), int(r.x) + 16, int(r.y) + 168, 14, rgb(100, 70, 50));
    bool coast = coastNearShip(rp), busy = dit != shipDetails.end() && dit->second.voyage;
    const char* hint = busy ? "On a supply run for your haulers." : (in.prog > 0 && !coast ? "Sail next to a coastline to disembark." : "Select troops, then right-click the ship to embark.");
    DrawText(hint, int(r.x) + 16, int(r.y) + 190, 13, (in.prog > 0 && !coast) ? rgb(170, 60, 40) : rgb(100, 70, 50));
    bool can = in.prog > 0 && coast;
    drawButton(shipPanelBtn(0), "Disembark all", can, false, CheckCollisionPointRec(m, shipPanelBtn(0)), CheckCollisionPointRec(m, shipPanelBtn(0)) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    drawButton(shipPanelBtn(1), "Close", true, true, CheckCollisionPointRec(m, shipPanelBtn(1)), CheckCollisionPointRec(m, shipPanelBtn(1)) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
}
