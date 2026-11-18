// ===== [world] World, environment and NPC life: clearing of surface features, gold bonus, hunting pawns, sheep repopulation,
// ===== faction patrols with loot and dynamic retaliation. Included INSIDE `struct Server` (see server.cpp).

// ---------------------------------------------------------------- state
struct ClearJob { int owner; uint8_t feat; float t, total; };
struct Hunt { float meat = 0; double pause = 0, scan = 0; uint32_t storage = 0; };                      // free pawn: hunts sheep, carries meat home
struct Patrol { uint32_t group; uint8_t slot; float repath = 0; };                                      // member of an AI patrol group
struct PatGroup {
    uint16_t town = 0;
    std::vector<uint32_t> m;                                                                              // member uids
    uint32_t leader = 0, tgt = 0;
    Pos wp{0, 0}, retPos{0, 0};
    bool hasWp = false, pending = true;
    int fails = 0, ret = 0;                                                                               // ret: player id being hunted (0 = peaceful patrol)
    double wait = 0, repath = 0, wpUntil = 0, respawnAt = 0, retUntil = 0;
};
struct SheepRec { uint32_t uid; Pos p; };

std::unordered_set<int64_t> cleared;                         // tiles whose surface feature has been removed (persisted)
std::vector<int64_t> clearedNew;                             // delta waiting to be broadcast
uint32_t clearVer = 1;                                       // bumps whenever `cleared` changes (invalidates mine bonus cache)
std::unordered_map<int64_t, ClearJob> clearJobs;
bool jobsWereSent = false;
std::unordered_map<uint32_t, std::pair<uint32_t, float>> mineCache;
std::unordered_map<uint32_t, PatGroup> pgroups;
uint32_t nextGroup = 1;
std::vector<SheepRec> sheepList;
std::unordered_map<uint32_t, uint32_t> sheepClaim;           // sheep uid -> hunter uid
double nextRepop = 8;

struct WorldAdapter {                                        // adapter for the shared goldNear()
    Server& s;
    int biome(int x, int y) { return s.biome(x, y); }
    bool cleared(int x, int y) { return s.cleared.count(tkey(x, y)) > 0; }
    uint32_t seedValue() { return s.seed; }
};

// ---------------------------------------------------------------- surface features
uint8_t featOf(int x, int y) {
    int b = biome(x, y);
    if (!isLand(b) || isCliffTile(b, biome(x, y - 1))) return F_None;
    return featureAt(x, y, b, seed);
}
bool isCleared(int x, int y) const { return cleared.count(tkey(x, y)) > 0; }
void markCleared(int x, int y) {
    if (cleared.insert(tkey(x, y)).second) { clearedNew.push_back(tkey(x, y)); clearVer++; }
}
void markFootprint(Kind k, int tx, int ty) {                  // a building's footprint never shows (or demands clearing of) features again
    for (int y = ty; y < ty + KINFO[k].fh; y++) for (int x = tx; x < tx + KINFO[k].fw; x++) markCleared(x, y);
}
// feature tiles under a footprint that still have to be cleared
std::vector<IV> blockedTiles(Kind k, int tx, int ty) {
    std::vector<IV> v;
    for (int y = ty; y < ty + KINFO[k].fh; y++)
        for (int x = tx; x < tx + KINFO[k].fw; x++)
            if (!isCleared(x, y) && featureBlocks(featOf(x, y))) v.push_back({x, y});
    return v;
}
bool worldBuildOk(int id, Kind k, int tx, int ty) {           // hook for C_Build
    auto v = blockedTiles(k, tx, ty);
    if (v.empty()) return true;
    int trees = 0, rocks = 0, gold = 0;
    for (auto t : v) { uint8_t f = featOf(t.x, t.y); (f == F_Rock ? rocks : (f == F_Gold ? gold : trees))++; }
    std::string s = "Cannot build here: clear";
    if (trees) s += " " + std::to_string(trees) + " tree" + (trees > 1 ? "s" : "");
    if (rocks) s += (trees ? "," : "") + std::string(" ") + std::to_string(rocks) + " rock" + (rocks > 1 ? "s" : "");
    if (gold) s += ((trees || rocks) ? "," : "") + std::string(" ") + std::to_string(gold) + " gold vein" + (gold > 1 ? "s" : "");
    say(id, s + " first (click the blocked tiles to start clearing).");
    return false;
}
bool inClearReach(int id, int x, int y) {                     // clearing is allowed anywhere the player could place a building
    bool ok = false;
    reg.view<Info, Building, Owner>().each([&](const Info& i, const Building& b, const Owner& o) {
        if (ok || (i.kind != K_TownHall && i.kind != K_Outpost) || !allied(id, o.id) || b.progress < 1.f) return;
        Pos c = footCenter(i.kind, b.tx, b.ty);
        float reach = i.kind == K_TownHall ? 60.f : 34.f;
        if (std::max(std::abs(c.x - float(x)), std::abs(c.y - float(y))) <= reach) ok = true;
    });
    return ok;
}
void onClear(int id, Reader& r) {
    int n = r.get<uint8_t>();
    if (!r.ok || n < 1 || n > MAX_CLEAR_TILES) return;
    std::vector<IV> ts;
    for (int i = 0; i < n; i++) { int x = r.get<int32_t>(), y = r.get<int32_t>(); if (!r.ok) return; ts.push_back({x, y}); }
    int mine = 0;
    for (auto& j : clearJobs) if (j.second.owner == id) mine++;
    float cost[R_Count]{};
    std::vector<IV> ok;
    for (auto t : ts) {
        int64_t key = tkey(t.x, t.y);
        if (clearJobs.count(key) || isCleared(t.x, t.y) || !explored(id, t.x, t.y) || occ.count(key)) continue;
        uint8_t f = featOf(t.x, t.y);
        if (!featureBlocks(f) || !inClearReach(id, t.x, t.y)) continue;
        if (mine + int(ok.size()) >= 60) break;
        cost[R_Wood] += float(clearCostWood(f));
        ok.push_back(t);
    }
    if (ok.empty()) { say(id, "Nothing to clear there."); return; }
    if (!afford(id, cost)) { say(id, "Not enough wood to clear that (" + std::to_string(int(cost[R_Wood])) + " needed)."); return; }
    spend(id, cost);
    for (auto t : ok) {
        uint8_t f = featOf(t.x, t.y);
        clearJobs[tkey(t.x, t.y)] = ClearJob{id, f, 0.f, clearSeconds(f)};
    }
    say(id, "Clearing " + std::to_string(ok.size()) + " tile" + (ok.size() > 1 ? "s" : "") + " (-" + std::to_string(int(cost[R_Wood])) + " wood).");
}
void sendDrop(Pos p, int res, int amt, int credited) {         // visible bouncing "+N" drop for everyone who can see the spot
    Writer w;
    w.put<uint8_t>(S_Drop).put<float>(p.x).put<float>(p.y).put<uint8_t>(uint8_t(res)).put<uint16_t>(uint16_t(std::clamp(amt, 0, 65000)));
    for (int id = 1; id <= MAX_PLAYERS; id++) {
        if (!pl[id].online) continue;
        if (id != credited && !seen(sourcesFor(id), p.x, p.y, 2.f)) continue;
        net.send(pl[id].conn, w);
    }
}
void creditDrop(int player, Pos p, int res, int amt) {
    if (isPlayer(player) && pl[player].used) pl[player].res[res] += float(amt);
    sendDrop(p, res, amt, player);
}
void sendClearedList(int id, const std::vector<int64_t>& tiles, bool full) {
    size_t i = 0;
    do {                                                                                                   // batches keep packets small
        size_t n = std::min<size_t>(3000, tiles.size() - i);
        Writer w;
        w.put<uint8_t>(S_Cleared).put<uint8_t>(full ? (i == 0 ? 0 : 2) : 1).put<uint32_t>(uint32_t(n));   // 0 = replace, 2 = append, 1 = delta
        for (size_t k = 0; k < n; k++) w.put<int32_t>(keyCx(tiles[i + k])).put<int32_t>(keyCy(tiles[i + k]));
        net.send(pl[id].conn, w);
        i += n;
    } while (i < tiles.size());
}
void sendClearedFull(int id) {                                 // hook for onHello
    std::vector<int64_t> v(cleared.begin(), cleared.end());
    sendClearedList(id, v, true);
}
void clearTick(float dt) {
    std::vector<int64_t> done;
    for (auto& [key, j] : clearJobs) { j.t += dt; if (j.t >= j.total) done.push_back(key); }
    for (auto key : done) {
        ClearJob j = clearJobs[key];
        clearJobs.erase(key);
        int x = keyCx(key), y = keyCy(key);
        markCleared(x, y);
        Pos p{float(x) + .5f, float(y) + .6f};
        creditDrop(j.owner, p, dropRes(j.feat), dropAmount(j.feat));
        fx(E_Dust, p.x, p.y);
    }
    if (!clearedNew.empty()) {
        for (int id = 1; id <= MAX_PLAYERS; id++) if (pl[id].online) sendClearedList(id, clearedNew, false);
        clearedNew.clear();
    }
    if (tick % 4 == 0 && (!clearJobs.empty() || jobsWereSent)) {
        jobsWereSent = !clearJobs.empty();
        Writer w;
        w.put<uint8_t>(S_ClearJobs).put<uint16_t>(uint16_t(std::min<size_t>(clearJobs.size(), 400)));
        size_t n = 0;
        for (auto& [key, j] : clearJobs) {
            if (n++ >= 400) break;
            w.put<int32_t>(keyCx(key)).put<int32_t>(keyCy(key)).put<uint8_t>(uint8_t(std::clamp(j.t / j.total, 0.f, 1.f) * 100.f));
        }
        for (int id = 1; id <= MAX_PLAYERS; id++) if (pl[id].online) net.send(pl[id].conn, w);
    }
}

// ---------------------------------------------------------------- gold deposits boost mines
float mineMul(entt::entity e, Kind k, Pos c) {                  // output multiplier from nearby gold deposits (cached per clear-version)
    if (k != K_Mine) return 1.f;
    uint32_t uid = reg.get<Uid>(e).id;
    auto it = mineCache.find(uid);
    if (it != mineCache.end() && it->second.first == clearVer) return it->second.second;
    WorldAdapter wa{*this};
    float m = 1.f + goldBonusOf(goldNear(wa, c));
    mineCache[uid] = {clearVer, m};
    return m;
}

// ---------------------------------------------------------------- hunting (free pawns) and sheep
entt::entity nearestOwnStorage(int ow, Pos p, bool needRoom) {
    entt::entity best = entt::null;
    float bd = 1e9f;
    reg.view<Info, Building, Owner>().each([&](entt::entity st, const Info& i, const Building& sb, const Owner& so) {
        if (!isStorage(i.kind) || sb.progress < 1.f || so.id != ow || (needRoom && capOf(st) - stockOf(st) < 1.f)) return;
        Pos c = footCenter(i.kind, sb.tx, sb.ty);
        float d = std::hypot(p.x - c.x, p.y - c.y);
        if (d < bd) { bd = d; best = st; }
    });
    return best;
}
void huntPause(entt::entity e) { reg.get_or_emplace<Hunt>(e).pause = clock + 25; }   // hook: the player gave this pawn an order
bool isFreePawn(entt::entity e) const { return !reg.all_of<Worker>(e) && !reg.all_of<Convoy>(e) && !reg.all_of<Haul>(e) && !reg.all_of<Raider>(e); }
uint8_t worldAux(entt::entity e) const {                       // hook for sendState: show the meat a hunter carries
    const Hunt* h = reg.try_get<Hunt>(e);
    return h && h->meat > 0 ? uint8_t(R_Meat + 1) : uint8_t(0);
}
bool huntKill(entt::entity sheep, int killer) {                 // hook for processKills: a hunter pawn that killed a sheep carries the meat home
    const Hit* hit = reg.try_get<Hit>(sheep);
    if (!hit) return false;
    auto he = find(hit->by);
    if (he == entt::null || !reg.all_of<Hunt>(he) || reg.get<Owner>(he).id != killer) return false;
    reg.get<Hunt>(he).meat += 12.f;
    return true;
}
// hook for the unit loop: returns true when it handled the unit's behaviour
bool huntTick(entt::entity e, Unit& u, int ow, Pos& p, float dt) {
    if (!isFreePawn(e)) return false;
    auto& h = reg.get_or_emplace<Hunt>(e);
    (void)dt;
    if (clock < h.pause || !pl[ow].online) { combat(e, u, K_Pawn, ow, p, dt); return true; }
    if (h.meat > 0.f) {                                          // carry the meat to the nearest storage with room
        reg.remove<Attack>(e);
        auto st = find(h.storage);
        if (st == entt::null || !complete(st) || capOf(st) - stockOf(st) < 1.f) { st = nearestOwnStorage(ow, p, true); h.storage = st == entt::null ? 0 : reg.get<Uid>(st).id; u.path.clear(); }
        if (st == entt::null) return true;                       // every storage is full: wait
        if (rectDist(p, st) <= 1.6f) {
            float cargo[R_Count] = {0, 0, 0, h.meat};
            storeCargo(st, cargo);
            h.meat = cargo[R_Meat] < .5f ? 0.f : cargo[R_Meat];
            h.storage = 0;
            u.path.clear();
        } else if (u.path.empty() && clock >= h.scan) {
            h.scan = clock + 2.0;
            if (!order(e, posOf(st), reg.get<Uid>(st).id, 12000)) h.storage = 0;
        }
        return true;
    }
    if (reg.all_of<Attack>(e)) { combat(e, u, K_Pawn, ow, p, dt); return true; }
    if (clock < h.scan) return true;
    h.scan = clock + 1.5 + double(rng() % 100) / 100.0;
    auto home = nearestOwnStorage(ow, p, false);
    if (home == entt::null) return true;
    Pos hp = posOf(home);
    uint32_t myUid = reg.get<Uid>(e).id, bestUid = 0;
    float bd = 25.f;
    Pos bp{0, 0};
    for (auto& s : sheepList) {
        float d = std::hypot(s.p.x - p.x, s.p.y - p.y);
        if (d >= bd || std::hypot(s.p.x - hp.x, s.p.y - hp.y) > 45.f) continue;
        auto cl = sheepClaim.find(s.uid);
        if (cl != sheepClaim.end() && cl->second != myUid) {
            auto he = find(cl->second);
            if (he != entt::null && reg.all_of<Attack>(he) && reg.get<Attack>(he).target == s.uid) continue;
        }
        if (find(s.uid) == entt::null || dying.count(s.uid)) continue;
        bd = d; bestUid = s.uid; bp = s.p;
    }
    if (!bestUid) return true;
    if (order(e, bp)) { reg.emplace_or_replace<Attack>(e, Attack{bestUid}); sheepClaim[bestUid] = myUid; }
    return true;
}
void sheepSecond() {
    sheepList.clear();
    std::unordered_map<int64_t, int> perChunk;
    reg.view<Info, Pos, Uid>().each([&](const Info& i, const Pos& p, const Uid& u) {
        if (i.kind != K_Sheep) return;
        sheepList.push_back({u.id, p});
        perChunk[chunkKey(chunkOf(int(std::floor(p.x))), chunkOf(int(std::floor(p.y))))]++;
    });
    std::erase_if(sheepClaim, [&](auto& kv) { return find(kv.first) == entt::null || find(kv.second) == entt::null; });
    if (clock < nextRepop) return;
    nextRepop = clock + 8.0;
    if (sheepList.size() >= 900) return;
    std::unordered_set<int64_t> awake;                          // chunks around online players' things
    reg.view<Owner, Info>().each([&](entt::entity e, const Owner& o, const Info&) {
        if (!isPlayer(o.id) || !pl[o.id].online) return;
        Pos q = posOf(e);
        int cx = chunkOf(int(std::floor(q.x))), cy = chunkOf(int(std::floor(q.y)));
        for (int y = cy - 2; y <= cy + 2; y++) for (int x = cx - 2; x <= cx + 2; x++) awake.insert(chunkKey(x, y));
    });
    for (auto k : awake) {
        if (!spawned.count(k)) continue;
        int cx = keyCx(k), cy = keyCy(k);
        int b = biome(cx * CH + 8, cy * CH + 8);
        if (b != B_Plains && b != B_Flowers) continue;
        auto it = perChunk.find(k);
        if (it != perChunk.end() && it->second >= 4) continue;
        if (rng() % 100 >= 45) continue;
        for (int tries = 0; tries < 6; tries++) {
            int x = cx * CH + int(rng() % CH), y = cy * CH + int(rng() % CH);
            if (walkable(x, y, false, 0) && biome(x, y) == b) { makeUnit(K_Sheep, 0, float(x) + .5f, float(y) + .5f); break; }
        }
    }
}

// ---------------------------------------------------------------- faction patrols
void patrolLoot(Kind k, float mul, int killer, Pos p) {
    static const float base[3][R_Count] = {{10, 12, 0, 6}, {8, 14, 0, 5}, {6, 22, 4, 8}};     // warrior, archer, lancer
    const float* b = base[k == K_Archer ? 1 : (k == K_Lancer ? 2 : 0)];
    float tot = 0;
    int shown = 0;
    for (int r = 0; r < R_Count; r++) {
        int amt = int(std::lround(b[r] * mul * (.7f + float(rng() % 60) / 100.f)));
        if (amt <= 0) continue;
        tot += float(amt);
        creditDrop(killer, {p.x + float(shown % 2) * .4f - .2f, p.y + float(shown) * .15f}, r, amt);
        shown++;
    }
    if (isPlayer(killer) && tot > 0) say(killer, std::string("Loot from the fallen ") + KINFO[k].name + ": +" + std::to_string(int(tot)) + " resources.");
}
PatGroup* groupOf(uint32_t id) { auto it = pgroups.find(id); return it == pgroups.end() ? nullptr : &it->second; }
void spawnPatrol(uint32_t gid, PatGroup& g) {
    Town* t = townById(g.town);
    if (!t || !t->alive) return;
    int n = 2 + int(rng() % 3);
    static const Kind mix[] = {K_Warrior, K_Archer, K_Lancer};
    g.m.clear();
    for (int i = 0; i < n; i++) {
        Kind k = i == 0 ? (rng() % 2 ? K_Warrior : K_Lancer) : mix[rng() % 3];
        float a = float(i) * 1.3f + float(g.town);
        float x = float(t->cx) + .5f + std::cos(a) * 3.4f, y = float(t->cy) + 3.8f + std::sin(a) * 1.3f;
        if (!walkable(int(std::floor(x)), int(std::floor(y)), false, 0)) { x = float(t->cx) + .5f; y = float(t->cy) + 3.8f; }
        auto u = makeUnit(k, uint8_t(FACTION_BASE + t->faction), x, y);
        reg.emplace<TownTag>(u, TownTag{t->id});
        reg.emplace<Patrol>(u, Patrol{gid, uint8_t(i), 0.f});
        g.m.push_back(reg.get<Uid>(u).id);
    }
    g.leader = g.m.front();
    g.hasWp = false;
    g.wait = clock + 2.0;
    g.pending = false;
    g.ret = 0;
    g.tgt = 0;
}
// hook for processKills (before the victim is destroyed): patrol loot and retaliation
void worldOnKill(entt::entity e, Kind k, int owner, int killer, Pos p) {
    if (!isFactionOwner(owner) || !isArmy(k)) return;
    const Patrol* pt = reg.try_get<Patrol>(e);
    const TownTag* tt = reg.try_get<TownTag>(e);
    if (!pt && !tt) return;
    if (isPlayer(killer) && killer != owner) patrolLoot(k, pt ? 1.f : .6f, killer, p);
    if (!isPlayer(killer)) return;
    Pos where = p;
    if (const Hit* hit = reg.try_get<Hit>(e)) { auto a = find(hit->by); if (a != entt::null) where = posOf(a); }
    uint16_t town = tt ? tt->town : uint16_t(0);
    bool told = false;
    for (auto& [gid, g] : pgroups) {
        if (g.m.empty() || (pt ? gid != pt->group : g.town != town)) continue;
        g.ret = killer;
        g.retUntil = clock + 240.0;
        g.retPos = where;
        g.tgt = 0;
        told = true;
    }
    if (told) say(killer, std::string("The ") + FACTION_NAME[owner - FACTION_BASE] + " patrol is hunting you!");
}
Pos groupPos(const PatGroup& g) {
    auto l = find(g.leader);
    if (l != entt::null) return reg.get<Pos>(l);
    for (auto id : g.m) { auto e = find(id); if (e != entt::null) return reg.get<Pos>(e); }
    return {0, 0};
}
bool pickWaypoint(PatGroup& g) {
    Town* t = townById(g.town);
    if (!t) return false;
    for (int n = 0; n < 10; n++) {
        float a = float(rng() % 6283) / 1000.f, r = 18.f + float(rng() % 38);
        int x = int(std::floor(float(t->cx) + std::cos(a) * r)), y = int(std::floor(float(t->cy) + std::sin(a) * r));
        if (!walkable(x, y, false, 0) || biome(x, y) == B_Highlands) continue;
        g.wp = {float(x) + .5f, float(y) + .5f};
        g.hasWp = true;
        g.fails = 0;
        g.wpUntil = clock + 100.0;
        return true;
    }
    return false;
}
// hook for the unit loop: movement of one patrol member (combat is handled by the normal combat() pass)
void runPatrol(entt::entity e, Unit& u, Patrol& pt, float dt) {
    pt.repath -= dt;
    if (reg.all_of<Attack>(e) || reg.all_of<AMove>(e)) return;
    PatGroup* g = groupOf(pt.group);
    if (!g) return;
    Pos p = reg.get<Pos>(e);
    uint32_t uid = reg.get<Uid>(e).id;
    if (g->ret) {                                                // retaliation: hunt the perpetrator
        if (g->tgt) {
            auto te = find(g->tgt);
            if (te != entt::null && !dying.count(g->tgt)) {
                if (pt.repath <= 0) {
                    pt.repath = 1.5f;
                    if (order(e, posOf(te), isBuilding(reg.get<Info>(te).kind) ? g->tgt : 0)) reg.emplace_or_replace<Attack>(e, Attack{g->tgt});
                }
                return;
            }
        }
        if (u.path.empty() && pt.repath <= 0) {
            pt.repath = 2.5f + float(uid % 7) * .1f;
            if (std::hypot(p.x - g->retPos.x, p.y - g->retPos.y) > 3.f) order(e, g->retPos, 0, 20000);   // repeated partial-path replans carry us across the map
        }
        return;
    }
    if (uid == g->leader) {
        if (g->hasWp && (std::hypot(p.x - g->wp.x, p.y - g->wp.y) < 3.f || clock > g->wpUntil)) { g->hasWp = false; g->wait = clock + 6.0 + double(rng() % 12); u.path.clear(); }
        if (!g->hasWp) { if (clock >= g->wait) pickWaypoint(*g); return; }
        if (u.path.empty() && clock >= g->repath) {
            g->repath = clock + 2.0;
            if (order(e, g->wp, 0, 9000)) g->fails = 0;
            else if (++g->fails > 2) { g->hasWp = false; g->wait = clock + 2.0; g->fails = 0; }
        }
        return;
    }
    auto le = find(g->leader);
    if (le == entt::null) return;
    Pos lp = reg.get<Pos>(le);
    float a = float(pt.slot) * 2.1f + 0.7f, rad = 1.5f + .35f * float(pt.slot);
    Pos slot{lp.x + std::cos(a) * rad, lp.y + std::sin(a) * rad};
    float d = std::hypot(p.x - slot.x, p.y - slot.y);
    if (d > 2.2f) {
        if (pt.repath <= 0) { pt.repath = 1.f; order(e, slot, 0, 900); }
    } else if (d < 1.0f && !u.path.empty()) u.path.clear();
}
void patrolSecond() {
    for (auto& t : towns) {                                      // make sure each living town owns its patrol groups
        int want = 1 + int(hash2(t.id, 0, seed + 444) & 1), have = 0;
        for (auto& [id, g] : pgroups) if (g.town == t.id) have++;
        if (t.alive && have < want) {
            static const bool fast = std::getenv("BW_PATROLNOW") != nullptr;      // test hook: patrols appear immediately
            PatGroup g;
            g.town = t.id;
            g.pending = true;
            g.respawnAt = clock + (fast ? 1.0 : 15.0 + double(rng() % 25));
            pgroups[nextGroup++] = g;
        }
    }
    static const bool fastAll = std::getenv("BW_PATROLNOW") != nullptr;
    for (auto& [gid, g] : pgroups) {
        std::vector<uint32_t> alive;
        for (auto id : g.m) { auto e = find(id); if (e != entt::null && !dying.count(id)) alive.push_back(id); }
        g.m = alive;
        Town* t = townById(g.town);
        if (g.m.empty()) {
            g.ret = 0;
            if (!g.pending) { g.pending = true; g.respawnAt = clock + (fastAll ? 8.0 : 90.0 + double(rng() % 90)); }
            else if (clock >= g.respawnAt && t && t->alive) spawnPatrol(gid, g);
            continue;
        }
        if (find(g.leader) == entt::null) g.leader = g.m.front();
        auto le = find(g.leader);
        Pos lp = reg.get<Pos>(le);
        int owner = reg.get<Owner>(le).id;
        // react to attackers: whoever recently hit a member becomes the group's target
        if (!g.ret) {
            for (auto id : g.m) {
                auto e = find(id);
                const Hit* h = reg.try_get<Hit>(e);
                if (!h || clock - h->t > 3.0) continue;
                auto atk = find(h->by);
                if (atk == entt::null || allied(owner, reg.get<Owner>(atk).id) || dying.count(h->by)) continue;
                for (auto m2 : g.m) {
                    auto me = find(m2);
                    if (me == entt::null || reg.all_of<Attack>(me)) continue;
                    if (order(me, posOf(atk), isBuilding(reg.get<Info>(atk).kind) ? h->by : 0)) reg.emplace_or_replace<Attack>(me, Attack{h->by});
                }
                break;
            }
            // hostile factions attack on sight (neutral / friendly factions are left alone by hostile()'s rules)
            if (!reg.all_of<Attack>(le)) {
                auto tg = nearestHostile(lp, owner, 11.f, true);
                if (tg != entt::null) {
                    uint32_t tu = reg.get<Uid>(tg).id;
                    bool bld = isBuilding(reg.get<Info>(tg).kind);
                    for (auto m2 : g.m) {
                        auto me = find(m2);
                        if (me == entt::null || reg.all_of<Attack>(me)) continue;
                        if (order(me, posOf(tg), bld ? tu : 0)) reg.emplace_or_replace<Attack>(me, Attack{tu});
                    }
                }
            }
            continue;
        }
        // retaliation bookkeeping: give up after a while, otherwise keep a fresh idea of where the perpetrator is
        if (clock > g.retUntil || !pl[g.ret].used) { g.ret = 0; g.tgt = 0; g.hasWp = false; g.wait = clock + 1.0; continue; }
        if (g.tgt && (find(g.tgt) == entt::null || dying.count(g.tgt))) g.tgt = 0;
        float bestNear = 1e9f, bestAny = 1e9f;
        uint32_t nearUid = 0, anyUid = 0;
        Pos nearPos{0, 0}, anyPos{0, 0};
        reg.view<Owner, Uid, Info>().each([&](entt::entity e, const Owner& o, const Uid& u, const Info&) {
            if (o.id != g.ret || dying.count(u.id)) return;
            Pos q = posOf(e);
            float d = std::hypot(q.x - lp.x, q.y - lp.y);
            if (d < bestAny) { bestAny = d; anyUid = u.id; anyPos = q; }
            for (auto id : g.m) { auto me = find(id); if (me != entt::null) { Pos mp = reg.get<Pos>(me); float dm = std::hypot(q.x - mp.x, q.y - mp.y); if (dm < bestNear) { bestNear = dm; nearUid = u.id; nearPos = q; } } }
        });
        if (!anyUid) { g.ret = 0; g.tgt = 0; continue; }          // the perpetrator has nothing left to hunt
        if (nearUid && bestNear <= 20.f) { g.tgt = nearUid; g.retPos = nearPos; }
        else {
            g.tgt = 0;
            if (std::hypot(lp.x - g.retPos.x, lp.y - g.retPos.y) < 5.f) g.retPos = anyPos;   // nothing here: hunt the nearest entity of that player
        }
    }
}
bool ephemeral(entt::entity e) const { return reg.all_of<Raider>(e) || reg.all_of<Patrol>(e); }   // hook for save(): not persisted
bool isPatrol(entt::entity e) const { return reg.all_of<Patrol>(e); }

// ---------------------------------------------------------------- hooks
void worldTick(float dt) {                                       // called once per server tick from update()
    clearTick(dt);
    if (tick % 20 == 7) { sheepSecond(); patrolSecond(); }
}
void worldSave(Writer& w) {
    w.put<uint32_t>(uint32_t(cleared.size()));
    for (auto k : cleared) w.put<int64_t>(k);
}
void worldLoad(Reader& r) {                                       // append-only section after the entity list (absent in older saves)
    if (r.p >= r.e) return;
    uint32_t n = r.get<uint32_t>();
    for (uint32_t i = 0; i < n && r.ok; i++) cleared.insert(r.get<int64_t>());
    clearVer++;
}
