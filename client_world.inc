// ===== [world] Client side of world/environment features: cleared-tile sync, clearing UX and progress, resource drops,
// ===== gold deposit sparkle + mine bonus, drifting clouds over discovered land. Included INSIDE `struct App`.

// ---------------------------------------------------------------- state
std::unordered_set<int64_t> clearedT;                        // tiles whose feature was removed (synced from the server)
struct CJob { float pct; };
std::unordered_map<int64_t, CJob> cjobs;                      // running clear jobs (progress 0..1)
struct DropFx { float x, y; int res, amt; double t; };
std::vector<DropFx> drops;                                    // bouncing "+N" resource drops
std::vector<Vector2> mineCtr;                                 // centres (tiles) of own mines and the mine being placed

struct CWAdapter {                                            // adapter for the shared goldNear()
    App& a;
    int biome(int x, int y) { return a.biomeAt(x, y); }
    bool cleared(int x, int y) { return a.clearedT.count(chunkKey(x, y)) > 0; }
    uint32_t seedValue() { return a.seed; }
};
int worldGoldNear(Pos c) { CWAdapter w{*this}; return goldNear(w, c); }

// ---------------------------------------------------------------- network
void worldPacket(const uint8_t* d, size_t n) {                 // hook for onPacket's default branch
    Reader r{d, d + n};
    switch (r.get<uint8_t>()) {
    case S_Cleared: {
        int mode = r.get<uint8_t>();
        uint32_t cnt = r.get<uint32_t>();
        if (!r.ok) return;
        if (mode == 0) clearedT.clear();
        for (uint32_t i = 0; i < cnt; i++) {
            int x = r.get<int32_t>(), y = r.get<int32_t>();
            if (!r.ok) return;
            clearedT.insert(chunkKey(x, y));
            cjobs.erase(chunkKey(x, y));
        }
        break;
    }
    case S_ClearJobs: {
        int cnt = r.get<uint16_t>();
        std::unordered_map<int64_t, CJob> v;
        for (int i = 0; i < cnt; i++) {
            int x = r.get<int32_t>(), y = r.get<int32_t>();
            int pct = r.get<uint8_t>();
            if (!r.ok) return;
            v[chunkKey(x, y)] = CJob{float(pct) / 100.f};
        }
        cjobs = std::move(v);
        break;
    }
    case S_Drop: {
        DropFx f{};
        f.x = r.get<float>(); f.y = r.get<float>();
        f.res = r.get<uint8_t>(); f.amt = r.get<uint16_t>();
        f.t = GetTime();
        if (!r.ok || f.res >= R_Count) return;
        drops.push_back(f);
        if (drops.size() > 80) drops.erase(drops.begin());
        break;
    }
    default: break;
    }
}
void cmdClear(const std::vector<IV>& tiles) {
    for (size_t i = 0; i < tiles.size(); i += MAX_CLEAR_TILES) {
        size_t n = std::min<size_t>(MAX_CLEAR_TILES, tiles.size() - i);
        Writer w;
        w.put<uint8_t>(C_Clear).put<uint8_t>(uint8_t(n));
        for (size_t k = 0; k < n; k++) w.put<int32_t>(tiles[i + k].x).put<int32_t>(tiles[i + k].y);
        send(w);
    }
}

// ---------------------------------------------------------------- features that must be cleared before building
uint8_t featHere(int x, int y) const {
    int b = biomeAt(x, y);
    if (!isLand(b) || isCliffTile(b, biomeAt(x, y - 1))) return F_None;
    return featureAt(x, y, b, seed);
}
bool featBlocking(int x, int y) const {
    int64_t k = chunkKey(x, y);
    return !clearedT.count(k) && !occ.count(k) && featureBlocks(featHere(x, y));
}
std::vector<IV> blockedUnder(Kind k, IV t) const {
    std::vector<IV> v;
    for (int y = t.y; y < t.y + KINFO[k].fh; y++) for (int x = t.x; x < t.x + KINFO[k].fw; x++) if (featBlocking(x, y)) v.push_back({x, y});
    return v;
}
int clearWoodCost(const std::vector<IV>& v) const { int c = 0; for (auto t : v) c += clearCostWood(featHere(t.x, t.y)); return c; }
// hook for the placement click: when only features block the footprint, start clearing exactly those tiles
bool worldPlaceClick(Kind k, IV t) {
    auto v = blockedUnder(k, t);
    if (v.empty()) return false;
    int cost = clearWoodCost(v);
    if (res[R_Wood] + 1e-3f < float(cost)) { msg("Clearing " + std::to_string(v.size()) + " tile(s) needs " + std::to_string(cost) + " wood."); return true; }
    cmdClear(v);
    return true;
}
std::string worldGhostLabel(Kind k, IV t) const {
    auto v = blockedUnder(k, t);
    if (v.empty()) return "";
    return "Click to clear " + std::to_string(v.size()) + " tile" + (v.size() > 1 ? "s" : "") + " first (" + std::to_string(clearWoodCost(v)) + " wood)";
}
void worldMineGhost(IV t, float cx, float bottom) {            // drawn in world space while placing a mine: gold bonus radius
    Pos c = footCenter(K_Mine, t.x, t.y);
    int n = worldGoldNear(c);
    DrawCircleLinesV({c.x * TILE, c.y * TILE}, GOLD_RADIUS * TILE, rgb(255, 215, 70, 170));
    std::string s = n ? TextFormat("Gold nearby: %d (+%d%% output)", n, int(std::lround(goldBonusOf(n) * 100.f))) : "No gold nearby";
    textShadow(s, int(cx) - MeasureText(s.c_str(), 16) / 2, int(bottom) + 34, 16, n ? rgb(255, 225, 90) : rgb(210, 210, 210));
}

// ---------------------------------------------------------------- drawing
void worldFrame() {                                            // once per frame, before drawing features
    mineCtr.clear();
    reg.view<Info, Goal>().each([&](const Info& i, const Goal& g) { if (i.kind == K_Mine && i.owner == me) mineCtr.push_back({g.x, g.y}); });
    if (buildKind == int(K_Mine) && !overUI(GetMousePosition())) {
        IV t = placeTile(K_Mine, GetScreenToWorld2D(GetMousePosition(), cam));
        Pos c = footCenter(K_Mine, t.x, t.y);
        mineCtr.push_back({c.x, c.y});
    }
}
bool goldNearMine(int x, int y) const {
    for (auto& m : mineCtr) { float dx = float(x) + .5f - m.x, dy = float(y) + .5f - m.y; if (dx * dx + dy * dy <= GOLD_RADIUS * GOLD_RADIUS) return true; }
    return false;
}
void goldGlints(float cx, float by, uint32_t h, double t, bool strong) {
    if (strong) {                                              // deposits that boost one of our mines glow clearly
        float pulse = .5f + .5f * std::sin(float(t) * 3.f + float(h % 9));
        DrawCircleGradient({cx, by - 30.f}, 34.f + 8.f * pulse, rgb(255, 225, 90, int(70 + 50 * pulse)), rgb(255, 200, 40, 0));
    }
    int stars = strong ? 4 : 2;
    for (int i = 0; i < stars; i++) {
        float ph = float(t) * (strong ? 2.6f : 1.7f) + float((h >> (i * 3)) % 17) * .9f;
        float k = std::fmod(ph, 3.f) / 3.f;                    // each star blooms and fades on a loop
        float s = std::sin(k * 3.14159f);
        if (s <= .05f) continue;
        float ox = float(int((h >> (i * 4 + 2)) % 49) - 24), oy = -float(int((h >> (i * 5 + 1)) % 40)) - 6.f;
        float sz = (strong ? 13.f : 8.f) * s, x = cx + ox, y = by + oy;
        Color c = rgb(255, 250, 200, int(255 * s));
        DrawLineEx({x - sz, y}, {x + sz, y}, 2.f, c);
        DrawLineEx({x, y - sz}, {x, y + sz}, 2.f, c);
        DrawLineEx({x - sz * .55f, y - sz * .55f}, {x + sz * .55f, y + sz * .55f}, 1.5f, c);
        DrawLineEx({x - sz * .55f, y + sz * .55f}, {x + sz * .55f, y - sz * .55f}, 1.5f, c);
        DrawCircleV({x, y}, sz * .3f, rgb(255, 255, 255, int(220 * s)));
    }
}
void clearBar(float cx, float by, float pct) {
    float w = 54.f, x = cx - w / 2, y = by - 74.f;
    DrawRectangle(int(x) - 2, int(y) - 2, int(w) + 4, 11, rgb(20, 14, 10, 200));
    DrawRectangle(int(x), int(y), int(w * std::clamp(pct, 0.f, 1.f)), 7, rgb(250, 190, 60));
    DrawRectangleLines(int(x) - 2, int(y) - 2, int(w) + 4, 11, rgb(255, 235, 170, 200));
}
void worldDrawFx(std::vector<DrawItem>& items, double t) {      // bouncing resource drops
    static const char* spr[R_Count] = {"Terrain/Resources/Wood/Wood Resource/Wood Resource.png", "Terrain/Resources/Gold/Gold Resource/Gold_Resource.png",
                                       "Terrain/Decorations/Rocks/Rock1.png", "Terrain/Resources/Meat/Meat Resource/Meat Resource.png"};
    static const Color txt[R_Count] = {rgb(205, 150, 90), rgb(255, 225, 80), rgb(200, 205, 215), rgb(255, 150, 140)};
    for (auto& d : drops) {
        double age = t - d.t;
        if (age < 0 || age > 1.6) continue;
        DropFx f = d;
        items.push_back({f.y * TILE + 60.f, [=, this]() {
            Texture2D tx = tex(spr[f.res]);
            int pieces = std::clamp(1 + f.amt / 5, 1, 4);
            float life = float(age) / 1.6f;
            for (int i = 0; i < pieces; i++) {
                float ang = float(i) * 2.4f + f.x * 1.7f, spread = std::min(1.f, float(age) * 4.f) * (14.f + 10.f * float(i));
                float ph = float(age) * 6.5f + float(i) * .9f;
                float hop = std::fabs(std::sin(ph)) * 30.f * std::max(0.f, 1.f - life * 1.1f);
                float px = f.x * TILE + std::cos(ang) * spread, py = f.y * TILE + std::sin(ang) * spread * .45f;
                int a = int(255 * std::clamp((1.f - life) * 3.f, 0.f, 1.f));
                DrawEllipse(int(px), int(py + 4), 10, 4, rgb(0, 0, 0, a / 4));
                if (tx.id) DrawTexturePro(tx, {0, 0, float(tx.width), float(tx.height)}, {px - 20, py - 34 - hop, 40, 40}, {0, 0}, 0, rgb(255, 255, 255, a));
            }
            std::string s = "+" + std::to_string(f.amt);
            int fs = 20, w = MeasureText(s.c_str(), fs);
            float ty = f.y * TILE - 36.f - float(age) * 34.f;
            int a = int(255 * std::clamp((1.f - life) * 2.5f, 0.f, 1.f));
            Color c = txt[f.res];
            c.a = uint8_t(a);
            textShadow(s, int(f.x * TILE) - w / 2, int(ty), fs, c);
        }});
    }
    std::erase_if(drops, [&](const DropFx& d) { return t - d.t > 1.7; });
}
// drifting clouds over DISCOVERED land: sparse grid cells, hashed presence, continuous eastward drift, translucent
template <class FE, class FB> void worldDrawClouds(int x0, int y0, int x1, int y1, float t, FE E, FB B) {
    constexpr float CELL = 7.f, DRIFT = .42f;
    float shift = DRIFT * t;
    int gx0 = int(std::floor((float(x0) - 6.f - shift) / CELL)), gx1 = int(std::floor((float(x1) + 6.f - shift) / CELL));
    int gy0 = int(std::floor((float(y0) - 4.f) / CELL)), gy1 = int(std::floor((float(y1) + 4.f) / CELL));
    for (int gy = gy0; gy <= gy1; gy++)
        for (int gx = gx0; gx <= gx1; gx++) {
            uint32_t h = hash2(gx, gy, 7331);
            if (h % 100 >= 22) continue;                                         // low density
            float cx = float(gx) * CELL + float(h >> 8 & 7) + shift, cy = float(gy) * CELL + float(h >> 12 & 7);
            int tx = int(std::floor(cx)), ty = int(std::floor(cy));
            if (!E(tx, ty) || !isLand(B(tx, ty))) continue;                      // only over land we have discovered
            Texture2D ct = tex(std::string("Terrain/Decorations/Clouds/Clouds_0") + char('1' + (h >> 16) % 8) + ".png");
            float sc = .8f + float((h >> 20) & 15) / 40.f, w = 576.f * sc, hh = 256.f * sc;
            int alpha = 60 + int((h >> 24) % 31);                                // 60..90
            DrawEllipse(int(cx * TILE + 40), int(cy * TILE + 120), w * .30f, hh * .16f, rgb(15, 25, 45, 20));   // soft ground shadow
            DrawTexturePro(ct, {0, 0, 576, 256}, {cx * TILE - w / 2, cy * TILE - hh / 2, w, hh}, {0, 0}, 0, rgb(255, 255, 255, alpha));
        }
}
