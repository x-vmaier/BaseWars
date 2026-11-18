// Base mechanics & clan defense (included INSIDE `struct Server` in server.cpp).
//  * fire arrows / burning buildings / extinguishing        * per-building training queues (spawnTrained)
//  * clan alerts, support patrols, defense quota            * homeless clan members, town move, building relocation
// Everything here is driven by hooks in server.cpp: damage(), combat(), update(), defeat(), C_Build/C_Train, sendState, save/load.

// ---------------------------------------------------------------- components
struct BFlags { uint8_t dis = 0; float burn = 0; uint8_t burnBy = 0; };          // BLD_DISABLED (outside functional territory), burn timer
struct Moving { uint32_t to; };                                                  // old building that is being replaced (BLD_MOVING)
struct RelocNew { uint32_t from; bool town; float total; };                      // new building replacing `from` (construction time `total`)
struct QItem { Kind k; float t, total, wait = 0; };
struct TrainQ { std::vector<QItem> q; };
struct Support { Pos dest, home; uint8_t victim = 0; uint8_t phase = 0; bool defense = false; float calm = 0, travel = 0; int fails = 0; };
struct Extinguish { uint32_t target; float t = 0; };
struct BaseProf {
    uint8_t quota = 5;                                                           // defense quota: troops sent to every attacked clan mate
    bool homeless = false, testDone = false;
    double alertNext = 0, defNext = 0, supNext = 0, donateNote = 0;
    std::vector<uint64_t> friends;                                               // Steam friends reported at login (used when respawning)
};
BaseProf bp[MAX_PLAYERS + 1];
struct PendingAlert { int victim; Pos at; int attacker; };
std::vector<PendingAlert> pendingAlerts;
struct DefRoute { bool ok = false; float eta = 0; std::vector<Pos> path; };

static constexpr int MAX_QUEUE = 5;
static constexpr float BURN_TIME = 12.f, BURN_FRAC_PER_SEC = .016f, EXTINGUISH_PER_SEC = 3.f;
static constexpr int MAX_QUOTA = 20;

static float testSpeed() {                                                       // test hook: BW_BASESPEED=<x> speeds up queues, moves and relocations
    static const float v = std::getenv("BW_BASESPEED") ? std::max(1.f, float(std::atof(std::getenv("BW_BASESPEED")))) : 1.f;
    return v;
}
static float trainTime(Kind k) {
    switch (k) {
    case K_Pawn: return 6.f;
    case K_Warrior: return 9.f;
    case K_Archer: return 10.f;
    case K_Lancer: return 12.f;
    case K_Monk: return 11.f;
    case K_Ship: return 18.f;
    default: return 8.f;
    }
}
static bool isTrainer(Kind k) { return k == K_Archery || k == K_Harbour; }

bool inactive(entt::entity e) const { auto* b = reg.try_get<BFlags>(e); return b && b->dis; }     // non-functional building
uint8_t bflagsOf(entt::entity e) const {
    uint8_t f = 0;
    if (auto* b = reg.try_get<BFlags>(e)) { if (b->burn > 0) f |= BLD_BURNING; if (b->dis) f |= BLD_DISABLED; }
    if (reg.all_of<Moving>(e)) f |= BLD_MOVING;
    return f;
}
float buildTimeFor(entt::entity e, Kind k) const {                              // construction time; relocations take much longer
    if (auto* rn = reg.try_get<RelocNew>(e)) return std::max(1.f, rn->total / testSpeed());
    return KINFO[k].buildTime;
}
void releaseUnit(entt::entity e) {                                              // a player command takes the unit over
    reg.remove<Support>(e);
    reg.remove<Extinguish>(e);
}
bool townMoveActive(int id) {
    bool a = false;
    reg.view<RelocNew, Owner>().each([&](const RelocNew& rn, const Owner& o) { if (rn.town && o.id == id) a = true; });
    return a;
}

// ---------------------------------------------------------------- territory: which buildings still work?
void evalFunctional() {
    std::vector<Pos> ths[MAX_PLAYERS + 1], ops[MAX_PLAYERS + 1];
    std::vector<entt::entity> mine;
    reg.view<Info, Building, Owner>().each([&](entt::entity e, const Info& i, const Building& b, const Owner& o) {
        if (!isPlayer(o.id)) return;
        mine.push_back(e);
        if (i.kind == K_TownHall && !(b.progress < 1.f && reg.all_of<RelocNew>(e))) ths[o.id].push_back(footCenter(i.kind, b.tx, b.ty));
    });
    auto within = [](const std::vector<Pos>& v, Pos c, float r) {
        for (auto& a : v) if (std::max(std::abs(a.x - c.x), std::abs(a.y - c.y)) <= r) return true;
        return false;
    };
    auto center = [&](entt::entity e) { const auto& b = reg.get<Building>(e); return footCenter(reg.get<Info>(e).kind, b.tx, b.ty); };
    std::vector<uint8_t> want(mine.size(), 0);
    for (size_t n = 0; n < mine.size(); n++) {                                  // Outposts need a Town Hall within reach
        auto e = mine[n];
        if (reg.get<Info>(e).kind != K_Outpost) continue;
        int o = reg.get<Owner>(e).id;
        Pos c = center(e);
        bool ok = within(ths[o], c, OUTPOST_REACH) && !reg.all_of<Moving>(e);
        want[n] = ok ? 0 : 1;
        if (ok) ops[o].push_back(c);
    }
    int off[MAX_PLAYERS + 1]{}, on[MAX_PLAYERS + 1]{};
    for (size_t n = 0; n < mine.size(); n++) {
        auto e = mine[n];
        Kind k = reg.get<Info>(e).kind;
        int o = reg.get<Owner>(e).id;
        if (k == K_TownHall || k == K_Campfire) want[n] = 0;
        else if (k != K_Outpost) {
            Pos c = center(e);
            want[n] = (reg.all_of<Moving>(e) || !(within(ths[o], c, float(anchorRadius(K_TownHall))) || within(ops[o], c, float(anchorRadius(K_Outpost))))) ? 1 : 0;
        }
        auto* f = reg.try_get<BFlags>(e);
        bool cur = f && f->dis;
        if (bool(want[n]) == cur) continue;
        reg.get_or_emplace<BFlags>(e).dis = want[n];
        if (!reg.all_of<Moving>(e)) (want[n] ? off : on)[o]++;
    }
    for (int id = 1; id <= MAX_PLAYERS; id++) {
        if (off[id]) say(id, std::to_string(off[id]) + " building(s) are outside your functional territory and stopped working. Right-click them and choose Relocate.");
        if (on[id]) say(id, std::to_string(on[id]) + " building(s) work again.");
    }
}

// ---------------------------------------------------------------- production queues
int queuedUnits(int id) {
    int n = 0;
    reg.view<TrainQ, Owner>().each([&](const TrainQ& q, const Owner& o) { if (o.id == id) n += int(q.q.size()); });
    return n;
}
// Creates the unit that a finished queue entry produces (ships etc. may change what is spawned here). Null if there is no room.
entt::entity spawnTrained(entt::entity be, Kind k) {
    int id = reg.get<Owner>(be).id;
    Pos at;
    if (!spawnSpot(be, k == K_Ship, at)) return entt::null;
    auto u = makeUnit(k, uint8_t(id), at.x, at.y);
    if (k == K_Ship) fx(E_Splash, at.x, at.y);
    else fx(E_Dust, at.x, at.y);
    if (k == K_Pawn) {                                                          // new pawns join the least staffed resource building as hauler
        entt::entity bestB = entt::null;
        int bw = 3;
        reg.view<Info, Building, Owner, Uid>().each([&](entt::entity e, const Info& i, const Building& b, const Owner& o, const Uid& uid) {
            if (o.id != id || !isResourceBuilding(i.kind) || b.progress < 1.f || inactive(e)) return;
            int c = workersOf(uid.id);
            if (c < bw) { bw = c; bestB = e; }
        });
        if (bestB != entt::null) reg.emplace<Worker>(u, Worker{reg.get<Uid>(bestB).id});
    }
    return u;
}
void enqueueTrain(int id, entt::entity be, Kind k) {                            // C_Train: pay now, produce over time
    if (reg.get<Owner>(be).id != id || !complete(be) || !canTrain(reg.get<Info>(be).kind, k)) return;
    if (inactive(be) || reg.all_of<Moving>(be)) { say(id, "This building is not functional."); return; }
    if (reg.get<Info>(be).level < unitUnlockLevel(k)) { say(id, std::string(KINFO[k].name) + "s unlock at Archery level " + std::to_string(unitUnlockLevel(k)) + "."); return; }
    auto& tq = reg.get_or_emplace<TrainQ>(be);
    if (int(tq.q.size()) >= MAX_QUEUE) { say(id, "The training queue is full (" + std::to_string(MAX_QUEUE) + ")."); return; }
    if (!afford(id, KINFO[k].cost)) { say(id, "Not enough resources."); return; }
    if (unitCount(id) + queuedUnits(id) >= UNIT_CAP) { say(id, "Unit cap reached."); return; }
    spend(id, KINFO[k].cost);
    tq.q.push_back(QItem{k, 0.f, trainTime(k), 0.f});
}
void dequeueTrain(int id, entt::entity be, int idx) {
    auto* tq = reg.try_get<TrainQ>(be);
    if (!tq || reg.get<Owner>(be).id != id || idx < 0 || idx >= int(tq->q.size())) return;
    Kind k = tq->q[size_t(idx)].k;
    for (int r = 0; r < R_Count; r++) pl[id].res[r] += KINFO[k].cost[r];
    tq->q.erase(tq->q.begin() + idx);
    say(id, std::string("Training of ") + KINFO[k].name + " cancelled and refunded.");
}
void trainTick(entt::entity e, float dt) {
    auto* tq = reg.try_get<TrainQ>(e);
    if (!tq || tq->q.empty()) return;
    QItem& it = tq->q.front();
    int ow = reg.get<Owner>(e).id;
    if (it.t < it.total) it.t += dt * testSpeed();
    if (it.t < it.total) return;
    it.wait -= dt;
    if (it.wait > 0) return;
    it.wait = 1.f;
    if (unitCount(ow) >= UNIT_CAP) return;                                       // wait for room
    if (spawnTrained(e, it.k) != entt::null) {
        say(ow, std::string(KINFO[it.k].name) + " trained.");
        tq->q.erase(tq->q.begin());
    }
}
// called from the building loop for completed buildings; returns false when the building must not work (non-functional / relocating)
bool buildingTick(entt::entity e, float dt) {
    if (inactive(e)) return false;
    trainTick(e, dt);
    return true;
}

// ---------------------------------------------------------------- fire
void ignite(entt::entity b, int byOwner) {
    Kind k = reg.get<Info>(b).kind;
    if (!isBuilding(k) || k == K_Campfire) return;
    auto& f = reg.get_or_emplace<BFlags>(b);
    bool fresh = f.burn <= 0;
    f.burn = BURN_TIME;
    f.burnBy = uint8_t(byOwner);
    int o = reg.get<Owner>(b).id;
    if (fresh && isPlayer(o)) { Pos p = posOf(b); say(o, std::string("Your ") + KINFO[k].name + " is on fire! Idle units will try to extinguish it."); (void)p; }
}
// archers shoot fire arrows from troop level 3 (player level); returns after emitting the projectile effect
void archerShot(int ow, Pos from, Pos to, entt::entity tg) {
    bool fire = isPlayer(ow) && pl[ow].ulevel[K_Archer] >= 3 && isBuilding(reg.get<Info>(tg).kind) && reg.get<Info>(tg).kind != K_Campfire;
    if (fire) { fx(E_FireArrow, from.x, from.y - .4f, to.x, to.y); ignite(tg, ow); }
    else fx(E_Arrow, from.x, from.y - .4f, to.x, to.y);
}
bool canExtinguish(entt::entity e) {
    if (reg.any_of<Attack, AMove, Convoy, Haul, Raider, Support, Extinguish>(e)) return false;
    Kind k = reg.get<Info>(e).kind;
    if (!(k == K_Pawn || isArmy(k) || k == K_Monk)) return false;
    if (auto* w = reg.try_get<Worker>(e)) { if (w->phase != 0) return false; }
    return reg.get<Unit>(e).path.empty();
}
void burnSecond() {
    std::vector<entt::entity> burning;
    reg.view<BFlags>().each([&](entt::entity e, BFlags& f) { if (f.burn > 0) burning.push_back(e); });
    if (burning.empty()) return;
    std::unordered_map<uint32_t, int> cnt;
    reg.view<Extinguish>().each([&](const Extinguish& x) { cnt[x.target]++; });
    std::vector<entt::entity> cand;
    for (auto e : burning) {                                                    // damage over time
        if (!reg.valid(e)) continue;
        auto& f = reg.get<BFlags>(e);
        f.burn = std::max(0.f, f.burn - 1.f);
        damage(e, reg.get<Health>(e).maxhp * BURN_FRAC_PER_SEC, f.burnBy);
    }
    processKills();
    reg.view<Owner, Unit, Info, Pos>().each([&](entt::entity e, const Owner& o, const Unit&, const Info&, const Pos&) { if (isPlayer(o.id) && canExtinguish(e)) cand.push_back(e); });
    for (auto b : burning) {
        if (!reg.valid(b) || reg.get<BFlags>(b).burn <= 0) continue;
        int ow = reg.get<Owner>(b).id;
        if (!isPlayer(ow)) continue;
        uint32_t buid = reg.get<Uid>(b).id;
        int need = 3 - cnt[buid];
        Pos bc = posOf(b);
        std::vector<std::pair<float, size_t>> near2;
        for (size_t i = 0; i < cand.size(); i++) {
            if (!reg.valid(cand[i]) || reg.get<Owner>(cand[i]).id != ow) continue;
            Pos q = reg.get<Pos>(cand[i]);
            float d = std::hypot(q.x - bc.x, q.y - bc.y);
            if (d <= 26.f) near2.push_back({d, i});
        }
        std::sort(near2.begin(), near2.end());
        for (auto& [d, i] : near2) {
            if (need <= 0) break;
            auto e = cand[i];
            if (!order(e, bc, buid, 4000)) continue;
            reg.emplace_or_replace<Extinguish>(e, Extinguish{buid, 0.f});
            cand[i] = entt::null;
            need--;
        }
    }
}
// per unit and tick: units on firefighting duty (returns true while the unit is busy with it)
bool extinguishTick(entt::entity e, Unit& u, Pos& p, float dt) {
    auto* ex = reg.try_get<Extinguish>(e);
    if (!ex) return false;
    auto tgt = find(ex->target);
    auto* bf = tgt == entt::null ? nullptr : reg.try_get<BFlags>(tgt);
    if (tgt == entt::null || !bf || bf->burn <= 0) { reg.remove<Extinguish>(e); return false; }
    Pos bc = posOf(tgt);
    if (rectDist(p, tgt) <= 1.8f) {
        u.path.clear();
        u.i = 0;
        u.state = US_Extinguish;
        u.aim = uint8_t(int((std::atan2(bc.y - p.y, bc.x - p.x) + 3.14159f) / 6.28319f * 255.f));
        ex->t += dt;
        if (ex->t >= 1.f) {
            ex->t -= 1.f;
            bf->burn = std::max(0.f, bf->burn - EXTINGUISH_PER_SEC);
            fx(E_Extinguish, bc.x + float(int(rng() % 100) - 50) / 50.f, bc.y + .2f);
            if (bf->burn <= 0) say(reg.get<Owner>(tgt).id, std::string("The fire at your ") + KINFO[reg.get<Info>(tgt).kind].name + " is out.");
        }
        return true;
    }
    if (u.path.empty()) { if (!order(e, bc, ex->target, 4000)) reg.remove<Extinguish>(e); }
    return true;
}

// ---------------------------------------------------------------- clan alerts + defense dispatch
void baseOnDamage(entt::entity e, int owner, int attacker) {                       // from damage(): cheap gate, real work is deferred to baseTail()
    if (!isPlayer(owner) || attacker < 1 || attacker == owner || clock < bp[owner].alertNext || !hostile(attacker, owner)) return;
    bp[owner].alertNext = clock + 12.0;
    pendingAlerts.push_back({owner, posOf(e), attacker});
}
void sendAlert(int to, uint8_t type, Pos at, const std::string& text) {
    if (!isPlayer(to) || !pl[to].online) return;
    Writer w;
    w.put<uint8_t>(S_Alert).put<uint8_t>(type).put<float>(at.x).put<float>(at.y);
    w.str(text);
    net.send(pl[to].conn, w);
}
DefRoute planDefenseRoute(int /*member*/, Pos from, Pos to) {                     // land route from a member's home to the fight; the single place to extend with boats
    DefRoute r;
    std::vector<Pos> path;
    float straight = std::hypot(to.x - from.x, to.y - from.y);
    if (straight < 8.f) { r.ok = true; r.eta = straight / 2.7f; return r; }
    if (!findPath({int(std::floor(from.x)), int(std::floor(from.y))}, {int(std::floor(to.x)), int(std::floor(to.y))}, false, 0, path, 60000)) return r;
    float len = 0;
    Pos prev = from;
    for (auto& wp : path) { len += std::hypot(wp.x - prev.x, wp.y - prev.y); prev = wp; }
    float rest = std::hypot(to.x - prev.x, to.y - prev.y);
    if (std::getenv("BW_BASEDEBUG")) std::printf("[route] straight %.0f path %.0f remaining %.0f\n", straight, len, rest);
    if (rest > 15.f && rest > straight * .6f) return r;                         // no real progress on foot: unreachable (partial paths continue while repathing)
    r.ok = true;
    r.eta = (len + rest * 1.3f) / 2.7f;
    r.path = std::move(path);
    return r;
}
bool idleArmy(entt::entity e) {
    Kind k = reg.get<Info>(e).kind;
    if (!isArmy(k) || reg.any_of<Attack, AMove, Convoy, Haul, Raider, Support, Extinguish, Worker>(e)) return false;
    return reg.get<Unit>(e).path.empty();
}
std::vector<entt::entity> pickIdleArmy(int owner, Pos near2, int n) {
    std::vector<std::pair<float, entt::entity>> c;
    reg.view<Owner, Unit, Info, Pos>().each([&](entt::entity e, const Owner& o, const Unit&, const Info&, const Pos& p) {
        if (o.id == owner && idleArmy(e)) c.push_back({std::hypot(p.x - near2.x, p.y - near2.y), e});
    });
    std::sort(c.begin(), c.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<entt::entity> out;
    for (auto& [d, e] : c) { if (int(out.size()) >= n) break; out.push_back(e); }
    return out;
}
Pos homeOf(int id, Pos fallback) { auto th = townHallOf(id); return th == entt::null ? fallback : posOf(th); }
void sendPatrol(entt::entity e, Pos dest, int victim, bool defense) {
    int ow = reg.get<Owner>(e).id;
    Pos p = reg.get<Pos>(e);
    Support s;
    s.dest = dest;
    s.home = homeOf(ow, p);
    s.victim = uint8_t(victim);
    s.defense = defense;
    reg.remove<Attack>(e);
    reg.emplace_or_replace<Support>(e, s);
    reg.emplace_or_replace<AMove>(e, AMove{dest});
    order(e, dest, 0, 12000);
}
void dispatchDefense(int victim, Pos at, int attacker) {
    int c = pl[victim].clan;
    if (!c || clock < bp[victim].defNext) return;
    bp[victim].defNext = clock + 20.0;
    int total = 0;
    for (int m = 1; m <= MAX_PLAYERS && total < 60; m++) {
        if (m == victim || !pl[m].used || pl[m].clan != c || bp[m].quota == 0 || bp[m].homeless) continue;
        auto th = townHallOf(m);
        if (th == entt::null) continue;
        int existing = 0;
        reg.view<Support, Owner>().each([&](const Support& s, const Owner& o) { if (o.id == m && s.defense && s.victim == victim) existing++; });
        int need = int(bp[m].quota) - existing;
        if (need <= 0) continue;
        DefRoute rt = planDefenseRoute(m, posOf(th), at);
        if (std::getenv("BW_BASEDEBUG")) std::printf("[defense] %s -> victim %s: route ok=%d eta=%.0f need=%d\n", pl[m].name.c_str(), pl[victim].name.c_str(), int(rt.ok), rt.eta, need);
        if (!rt.ok) continue;
        auto us = pickIdleArmy(m, at, need);
        for (auto e : us) sendPatrol(e, at, victim, true);
        if (us.empty()) continue;
        total += int(us.size());
        say(m, "Defending " + pl[victim].name + " against " + ownerName(attacker) + ": " + std::to_string(us.size()) + " troops dispatched (ETA " + std::to_string(int(rt.eta)) + " s).");
        say(victim, pl[m].name + " sends " + std::to_string(us.size()) + " troops to defend you (ETA " + std::to_string(int(rt.eta)) + " s).");
    }
}
void alertFriends(int victim, Pos at, int attacker) {
    std::string txt = pl[victim].name + " is under attack by " + ownerName(attacker) + "!";
    for (int i = 1; i <= MAX_PLAYERS; i++) {
        if (i == victim || !pl[i].online) continue;
        if (!(sameClan(i, victim) || pl[i].allied[victim])) continue;
        sendAlert(i, 0, at, txt);
        say(i, txt + " (" + std::to_string(int(at.x)) + ", " + std::to_string(int(at.y)) + ")");
    }
}
void baseTail() {                                                                 // end of every tick: process deferred alerts
    if (pendingAlerts.empty()) return;
    auto v = std::move(pendingAlerts);
    pendingAlerts.clear();
    for (auto& a : v) {
        alertFriends(a.victim, a.at, a.attacker);
        dispatchDefense(a.victim, a.at, a.attacker);
    }
}
// manual support patrol (big map right-click on an allied / clan town or outpost)
void doSupport(int id, Pos dest, int n) {
    if (!std::isfinite(dest.x) || !std::isfinite(dest.y) || n < 1) return;
    n = std::min(n, MAX_QUOTA);
    if (bp[id].homeless || townHallOf(id) == entt::null) { say(id, "You need a town to send troops from."); return; }
    if (clock < bp[id].supNext) { say(id, "Your troops are still preparing the last patrol."); return; }
    bool ok = false;
    reg.view<Info, Building, Owner>().each([&](const Info& i, const Building& b, const Owner& o) {
        if ((i.kind != K_TownHall && i.kind != K_Outpost) || o.id == id || !isPlayer(o.id) || !allied(id, o.id)) return;
        Pos c = footCenter(i.kind, b.tx, b.ty);
        if (std::max(std::abs(c.x - dest.x), std::abs(c.y - dest.y)) <= 12.f) ok = true;
    });
    if (!ok) { say(id, "Support patrols can only go to allied or clan towns and outposts."); return; }
    auto us = pickIdleArmy(id, dest, n);
    if (us.empty()) { say(id, "No idle army units available for a patrol."); return; }
    for (auto e : us) sendPatrol(e, dest, 0, false);
    bp[id].supNext = clock + 6.0;
    say(id, "Support patrol of " + std::to_string(us.size()) + " troops sets out. They help fight, then return home.");
}
void supportSecond() {
    std::vector<entt::entity> v;
    reg.view<Support, Unit, Pos, Owner>().each([&](entt::entity e, const Support&, const Unit&, const Pos&, const Owner&) { v.push_back(e); });
    int budget = 8;                                                              // path searches per second
    for (auto e : v) {
        if (!reg.valid(e) || dying.count(reg.get<Uid>(e).id)) continue;
        auto& s = reg.get<Support>(e);
        auto& u = reg.get<Unit>(e);
        Pos p = reg.get<Pos>(e);
        int ow = reg.get<Owner>(e).id;
        float dDest = std::hypot(p.x - s.dest.x, p.y - s.dest.y), dHome = std::hypot(p.x - s.home.x, p.y - s.home.y);
        if (s.phase < 2) {
            bool hostileNear = nearestHostile(p, ow, 20.f, false) != entt::null;
            if (s.phase == 0) {
                s.travel += 1.f;
                if (dDest <= 8.f) { s.phase = 1; s.calm = 0; }
                else if (s.travel > 900.f || s.fails > 5) s.phase = 2;
                else if (u.path.empty() && !reg.all_of<Attack>(e) && budget > 0) {
                    budget--;
                    if (order(e, s.dest, 0, 12000)) reg.emplace_or_replace<AMove>(e, AMove{s.dest}); else s.fails++;
                }
            } else {
                s.calm = hostileNear ? 0.f : s.calm + 1.f;
                if (s.calm >= 20.f) s.phase = 2;                                 // the fight is over
                else if (dDest > 14.f && u.path.empty() && !reg.all_of<Attack>(e) && budget > 0) {
                    budget--;
                    if (order(e, s.dest, 0, 12000)) reg.emplace_or_replace<AMove>(e, AMove{s.dest});
                }
            }
            if (s.phase == 2) { reg.remove<AMove>(e); reg.remove<Attack>(e); u.path.clear(); u.i = 0; s.fails = 0; s.calm = 0; }
        }
        if (s.phase == 2) {                                                      // wander back home
            if (dHome <= 6.f || s.fails > 4 || reg.get<Info>(e).kind == K_Ship) { reg.remove<Support>(e); continue; }
            if (u.path.empty() && !reg.all_of<Attack>(e) && budget > 0) {
                budget--;
                float a = float(rng() % 628) / 100.f, rr = 1.5f + float(rng() % 300) / 100.f;
                if (!order(e, {s.home.x + std::cos(a) * rr, s.home.y + std::sin(a) * rr}, 0, 12000)) s.fails++;
            }
        }
    }
}

// ---------------------------------------------------------------- losing the home: clan members become homeless, others respawn near friends
std::vector<int> clanHalls(int victim) {                                          // clan mates that still have a Town Hall
    std::vector<int> v;
    int c = pl[victim].clan;
    if (!c) return v;
    for (int i = 1; i <= MAX_PLAYERS; i++) if (i != victim && pl[i].used && pl[i].clan == c && townHallOf(i) != entt::null) v.push_back(i);
    return v;
}
bool baseDefeat(int victim) {                                                     // true: handled (homeless clan member); false: normal respawn
    auto mates = clanHalls(victim);
    if (mates.empty()) return false;
    removeOwner(victim);
    for (int r = 0; r < R_Count; r++) pl[victim].res[r] = 0;
    std::string from;
    for (int m : mates) {                                                       // emergency donation: every mate gives a share of its stock
        std::string txt;
        for (int r = 0; r < R_Count; r++) {
            float give = std::min(std::floor(pl[m].res[r] * .15f), 200.f);
            if (give <= 0) continue;
            pl[m].res[r] -= give;
            pl[victim].res[r] += give;
            txt += std::to_string(int(give)) + " " + RES_NAME[r] + "  ";
        }
        if (txt.empty()) continue;
        say(m, "You donated to " + pl[victim].name + " (homeless): " + txt);
        say(victim, pl[m].name + " donated " + txt);
    }
    bp[victim].homeless = true;
    Pos hall{0, 0};
    for (int m : mates) { Pos c = posOf(townHallOf(m)); hall = c; reveal(victim, int(std::floor(c.x)), int(std::floor(c.y)), int(CLAN_TOWN_RADIUS)); }
    pl[victim].cam = hall;
    pl[victim].sentFog.clear();
    pl[victim].sentTerr.clear();
    say(victim, "Your Town Hall fell! You are homeless: place a new Town Hall (free) within " + std::to_string(int(CLAN_TOWN_RADIUS)) + " tiles of a clan Town Hall.");
    sendAlert(victim, 2, hall, "Your town was destroyed. Place a new Town Hall near your clan.");
    notifyClan(pl[victim].clan, pl[victim].name + " lost their town and is homeless. Resources were donated.");
    std::printf("%s was defeated (clan: homeless)\n", pl[victim].name.c_str());
    return true;
}
void respawnNormally(int id) {
    bp[id].homeless = false;
    removeOwner(id);
    resetProfile(id);
    spawnBase(id, bp[id].friends);
    pl[id].sentFog.clear();
    pl[id].sentTerr.clear();
    say(id, "Your clan has no town left: you start over near other players.");
}
void homelessSecond() {
    for (int id = 1; id <= MAX_PLAYERS; id++)
        if (pl[id].used && bp[id].homeless && clanHalls(id).empty()) respawnNormally(id);
}
bool inClanRadiusOf(int id, Pos c) {
    for (int m : clanHalls(id)) {
        Pos h = posOf(townHallOf(m));
        if (std::max(std::abs(h.x - c.x), std::abs(h.y - c.y)) <= CLAN_TOWN_RADIUS) return true;
    }
    return false;
}
void homelessBuild(int id, int tx, int ty) {                                        // C_Build K_TownHall
    if (!bp[id].homeless) { say(id, "You can only have one Town Hall. Right-click it and choose Move town to relocate."); return; }
    PlaceWorld w{*this, id};
    int err = checkPlace(w, K_TownHall, tx, ty, PM_HomelessHall);
    if (err) { say(id, std::string("Cannot build here: ") + PLACE_TEXT[err] + "."); return; }
    auto e = makeBuilding(K_TownHall, uint8_t(id), tx, ty, false);
    Pos c = posOf(e);
    bp[id].homeless = false;
    pl[id].cam = c;
    fx(E_Dust, c.x, c.y);
    say(id, "Your new hometown is being founded. Welcome back among your clan!");
    notifyClan(pl[id].clan, pl[id].name + " is founding a new town.");
}

// ---------------------------------------------------------------- moving the town / relocating buildings
void doMoveTown(int id, int tx, int ty) {
    auto th = townHallOf(id);
    if (th == entt::null || bp[id].homeless) { say(id, "You have no Town Hall to move."); return; }
    if (townMoveActive(id)) { say(id, "Your town is already being moved."); return; }
    if (!afford(id, TOWN_MOVE_COST)) { say(id, "Moving the town costs 500 Gold, 300 Wood and 200 Stone."); return; }
    PlaceWorld w{*this, id};
    int err = checkPlace(w, K_TownHall, tx, ty, PM_MoveHall);
    if (err) { say(id, std::string("Cannot move the town here: ") + PLACE_TEXT[err] + "."); return; }
    spend(id, TOWN_MOVE_COST);
    auto n = makeBuilding(K_TownHall, uint8_t(id), tx, ty, false);
    reg.emplace<RelocNew>(n, RelocNew{reg.get<Uid>(th).id, true, TOWN_MOVE_TIME});
    reg.emplace<Moving>(th, Moving{reg.get<Uid>(n).id});
    Pos c = posOf(n);
    fx(E_Dust, c.x, c.y);
    say(id, "Your new town is under construction (" + std::to_string(int(TOWN_MOVE_TIME)) + " s). Troops and pawns will move in when it is finished.");
}
void doRelocate(int id, uint32_t uid, int tx, int ty) {
    auto be = find(uid);
    if (be == entt::null || !reg.all_of<Building>(be) || reg.get<Owner>(be).id != id) return;
    Kind k = reg.get<Info>(be).kind;
    if (k == K_TownHall || k == K_Campfire || !complete(be)) return;
    if (!inactive(be) || reg.all_of<Moving>(be)) { say(id, "Only non-functional buildings can be relocated."); return; }
    if (townMoveActive(id)) { say(id, "Wait until the town move is finished."); return; }
    if (!afford(id, KINFO[k].cost)) { say(id, "Not enough resources to relocate."); return; }
    PlaceWorld w{*this, id};
    int err = checkPlace(w, k, tx, ty);
    if (err) { say(id, std::string("Cannot relocate here: ") + PLACE_TEXT[err] + "."); return; }
    spend(id, KINFO[k].cost);
    auto n = makeBuilding(k, uint8_t(id), tx, ty, false);
    float total = 60.f + 30.f * float(reg.get<Info>(be).level - 1);
    reg.emplace<RelocNew>(n, RelocNew{uid, false, total});
    reg.emplace<Moving>(be, Moving{reg.get<Uid>(n).id});
    Pos c = posOf(n);
    fx(E_Dust, c.x, c.y);
    say(id, std::string("Relocating your ") + KINFO[k].name + " (" + std::to_string(int(total)) + " s). Level and stock are kept.");
}
void migrateTown(int ow, entt::entity nth) {                                        // every troop and pawn walks into the new town (teleport with dust)
    std::vector<IV> spots;
    const auto& nb = reg.get<Building>(nth);
    Kind k = reg.get<Info>(nth).kind;
    for (int r = 1; r <= 6; r++)
        for (int y = nb.ty - r; y <= nb.ty + KINFO[k].fh - 1 + r; y++)
            for (int x = nb.tx - r; x <= nb.tx + KINFO[k].fw - 1 + r; x++) {
                if (std::max({nb.tx - x, x - (nb.tx + KINFO[k].fw - 1), nb.ty - y, y - (nb.ty + KINFO[k].fh - 1)}) != r) continue;
                if (walkable(x, y, false, 0)) spots.push_back({x, y});
            }
    if (spots.empty()) return;
    std::vector<entt::entity> us;
    reg.view<Owner, Unit, Info, Pos>().each([&](entt::entity e, const Owner& o, const Unit&, const Info& i, const Pos&) {
        if (o.id != ow || !isTroop(i.kind) || reg.any_of<Convoy, Haul>(e)) return;
        us.push_back(e);
    });
    size_t n = 0;
    for (auto e : us) {
        IV s = spots[(n * 7 + 3) % spots.size()];
        Pos old = reg.get<Pos>(e);
        if (n < 10) fx(E_Dust, old.x, old.y);
        reg.get<Pos>(e) = {float(s.x) + .5f, float(s.y) + .5f};
        auto& u = reg.get<Unit>(e);
        u.path.clear();
        u.i = 0;
        u.lastRev = {INT_MIN, INT_MIN};
        reg.remove<Attack>(e);
        reg.remove<AMove>(e);
        reg.remove<Support>(e);
        reg.remove<Extinguish>(e);
        if (n < 10) fx(E_Dust, float(s.x) + .5f, float(s.y) + .5f);
        n++;
    }
    say(ow, std::to_string(n) + " troops and pawns moved into your new town.");
}
void finishReloc(entt::entity n) {
    RelocNew rn = reg.get<RelocNew>(n);
    uint32_t nuid = reg.get<Uid>(n).id;
    reg.remove<RelocNew>(n);
    int ow = reg.get<Owner>(n).id;
    auto old = find(rn.from);
    auto& in = reg.get<Info>(n);
    auto& nb = reg.get<Building>(n);
    if (old != entt::null) {
        const auto& oi = reg.get<Info>(old);
        const auto& ob = reg.get<Building>(old);
        in.level = oi.level;                                                    // level and stock are carried over
        auto& h = reg.get<Health>(n);
        h.maxhp = KINFO[in.kind].hp * levelHp(in.level);
        h.hp = h.maxhp;
        for (int r = 0; r < R_Count; r++) nb.stock[r] = ob.stock[r];
        nb.haulPct = ob.haulPct;
        if (auto* tq = reg.try_get<TrainQ>(old)) reg.emplace_or_replace<TrainQ>(n, *tq);
        reg.view<Worker>().each([&](Worker& w) { if (w.home == rn.from) w.home = nuid; });
        reg.view<Haul>().each([&](Haul& hl) { if (hl.storage == rn.from) hl.storage = nuid; });
        Pos oc = posOf(old);
        fx(E_Dust, oc.x, oc.y);
        reg.remove<Moving>(old);
        destroyNow(old);                                                        // the old hall / building is removed (no defeat: another Town Hall exists)
    }
    Pos c = posOf(n);
    fx(E_Dust, c.x, c.y);
    if (rn.town) {
        pl[ow].cam = c;
        migrateTown(ow, n);
        say(ow, "Your town has moved! Buildings outside the new territory stopped working - relocate them.");
    } else say(ow, std::string("Your ") + KINFO[in.kind].name + " was relocated.");
    evalFunctional();
}
void relocSecond() {
    std::vector<entt::entity> done, stale;
    reg.view<RelocNew, Building>().each([&](entt::entity e, const RelocNew&, const Building& b) { if (b.progress >= 1.f) done.push_back(e); });
    for (auto e : done) finishReloc(e);
    reg.view<Moving>().each([&](entt::entity e, const Moving& m) { if (find(m.to) == entt::null) stale.push_back(e); });   // replacement was destroyed: cancel the move
    for (auto e : stale) reg.remove<Moving>(e);
    std::vector<entt::entity> orphan;                                           // old building destroyed while the replacement is under construction: keep building
    (void)orphan;
}

// ---------------------------------------------------------------- per second / per tick glue
void baseCommand(int id, uint8_t type, Reader& r) {
    switch (type) {
    case C_SetQuota: { int q = r.get<uint8_t>(); if (r.ok) bp[id].quota = uint8_t(std::min(q, MAX_QUOTA)); break; }
    case C_Support: {
        float x = r.get<float>(), y = r.get<float>();
        int n = r.get<uint8_t>();
        if (r.ok) doSupport(id, {x, y}, n);
        break;
    }
    case C_MoveTown: { int tx = r.get<int32_t>(), ty = r.get<int32_t>(); if (r.ok) doMoveTown(id, tx, ty); break; }
    case C_Relocate: { uint32_t uid = r.get<uint32_t>(); int tx = r.get<int32_t>(), ty = r.get<int32_t>(); if (r.ok) doRelocate(id, uid, tx, ty); break; }
    case C_Dequeue: {
        auto be = find(r.get<uint32_t>());
        int idx = r.get<uint8_t>();
        if (r.ok && be != entt::null && reg.all_of<Building>(be)) dequeueTrain(id, be, idx);
        break;
    }
    default: break;
    }
}
void sendQueues(int id) {
    Writer w;
    w.put<uint8_t>(S_Queue);
    size_t cp = w.b.size();
    w.put<uint8_t>(0);
    uint8_t n = 0;
    reg.view<TrainQ, Owner, Uid>().each([&](const TrainQ& q, const Owner& o, const Uid& uid) {
        if (o.id != id || q.q.empty() || n == 255) return;
        w.put<uint32_t>(uid.id).put<uint8_t>(uint8_t(q.q.size())).put<uint8_t>(uint8_t(std::clamp(q.q[0].t / std::max(.1f, q.q[0].total), 0.f, 1.f) * 100.f));
        for (auto& it : q.q) w.put<uint8_t>(it.k);
        n++;
    });
    w.b[cp] = n;
    net.send(pl[id].conn, w);
}
void sendBaseInfo(int id) {
    uint8_t moving = 0, pct = 0;
    reg.view<RelocNew, Building, Owner>().each([&](const RelocNew& rn, const Building& b, const Owner& o) {
        if (rn.town && o.id == id) { moving = 1; pct = uint8_t(std::min(1.f, b.progress) * 100.f); }
    });
    Writer w;
    w.put<uint8_t>(S_BaseInfo).put<uint8_t>(bp[id].quota).put<uint8_t>(bp[id].homeless ? 1 : 0).put<uint8_t>(moving).put<uint8_t>(pct);
    net.send(pl[id].conn, w);
}
void baseTestInit(int id) {                                                          // test hook BW_BASETEST: plenty of resources, level 3 troops
    static const bool on = std::getenv("BW_BASETEST") != nullptr;
    if (!on || bp[id].testDone) return;
    bp[id].testDone = true;
    for (float& r : pl[id].res) r = std::max(r, 6000.f);
    for (int k = K_Pawn; k <= K_Monk; k++) pl[id].ulevel[k] = 3;
}
void baseSecond() {
    for (int id = 1; id <= MAX_PLAYERS; id++) if (pl[id].used) baseTestInit(id);
    evalFunctional();
    relocSecond();
    burnSecond();
    supportSecond();
    homelessSecond();
    for (int id = 1; id <= MAX_PLAYERS; id++) if (pl[id].online) sendBaseInfo(id);
}

// ---------------------------------------------------------------- persistence (appended after the entity list of the savegame)
void saveBase(Writer& w) {
    w.put<uint32_t>(0x31455342u);                                                   // "BSE1"
    uint8_t np = 0;
    for (int i = 1; i <= MAX_PLAYERS; i++) if (pl[i].used) np++;
    w.put<uint8_t>(np);
    for (int i = 1; i <= MAX_PLAYERS; i++) {
        if (!pl[i].used) continue;
        w.put<uint8_t>(uint8_t(i)).put<uint8_t>(bp[i].quota).put<uint8_t>(bp[i].homeless ? 1 : 0);
        w.put<uint16_t>(uint16_t(std::min<size_t>(bp[i].friends.size(), 200)));
        for (size_t f = 0; f < bp[i].friends.size() && f < 200; f++) w.put<uint64_t>(bp[i].friends[f]);
    }
    std::vector<entt::entity> es;
    reg.view<Uid>().each([&](entt::entity e, const Uid&) { if (reg.any_of<BFlags, Moving, RelocNew, TrainQ, Support>(e)) es.push_back(e); });
    w.put<uint32_t>(uint32_t(es.size()));
    for (auto e : es) {
        w.put<uint32_t>(reg.get<Uid>(e).id);
        const BFlags* bf = reg.try_get<BFlags>(e);
        w.put<float>(bf ? bf->burn : 0.f).put<uint8_t>(bf ? bf->burnBy : 0);
        const Moving* mv = reg.try_get<Moving>(e);
        w.put<uint32_t>(mv ? mv->to : 0u);
        const RelocNew* rn = reg.try_get<RelocNew>(e);
        w.put<uint8_t>(rn ? 1 : 0);
        if (rn) w.put<uint32_t>(rn->from).put<uint8_t>(rn->town ? 1 : 0).put<float>(rn->total);
        const TrainQ* tq = reg.try_get<TrainQ>(e);
        w.put<uint8_t>(tq ? uint8_t(tq->q.size()) : 0);
        if (tq) for (auto& it : tq->q) w.put<uint8_t>(it.k).put<float>(it.t).put<float>(it.total);
        const Support* sp = reg.try_get<Support>(e);
        w.put<uint8_t>(sp ? 1 : 0);
        if (sp) w.put<float>(sp->dest.x).put<float>(sp->dest.y).put<float>(sp->home.x).put<float>(sp->home.y).put<uint8_t>(sp->victim).put<uint8_t>(sp->phase).put<uint8_t>(sp->defense ? 1 : 0);
    }
}
void loadBase(Reader& r) {
    if (r.get<uint32_t>() != 0x31455342u) { r.ok = false; return; }
    int np = r.get<uint8_t>();
    for (int n = 0; n < np && r.ok; n++) {
        int id = r.get<uint8_t>();
        uint8_t q = r.get<uint8_t>(), hl = r.get<uint8_t>();
        int nf = r.get<uint16_t>();
        std::vector<uint64_t> fr;
        for (int f = 0; f < nf && r.ok; f++) fr.push_back(r.get<uint64_t>());
        if (id < 1 || id > MAX_PLAYERS) continue;
        bp[id].quota = std::min<uint8_t>(q, MAX_QUOTA);
        bp[id].homeless = hl != 0;
        bp[id].friends = fr;
    }
    uint32_t ne = r.get<uint32_t>();
    for (uint32_t n = 0; n < ne && r.ok; n++) {
        auto e = find(r.get<uint32_t>());
        float burn = r.get<float>();
        uint8_t burnBy = r.get<uint8_t>();
        uint32_t mv = r.get<uint32_t>();
        bool hasRn = r.get<uint8_t>() != 0;
        RelocNew rn{0, false, 0};
        if (hasRn) { rn.from = r.get<uint32_t>(); rn.town = r.get<uint8_t>() != 0; rn.total = r.get<float>(); }
        int nq = r.get<uint8_t>();
        TrainQ tq;
        for (int i = 0; i < nq && r.ok; i++) { QItem it{Kind(r.get<uint8_t>()), 0, 0, 0}; it.t = r.get<float>(); it.total = r.get<float>(); if (it.k < K_Count) tq.q.push_back(it); }
        bool hasSp = r.get<uint8_t>() != 0;
        Support sp;
        if (hasSp) { sp.dest.x = r.get<float>(); sp.dest.y = r.get<float>(); sp.home.x = r.get<float>(); sp.home.y = r.get<float>(); sp.victim = r.get<uint8_t>(); sp.phase = r.get<uint8_t>(); sp.defense = r.get<uint8_t>() != 0; }
        if (!r.ok || e == entt::null) continue;
        if (burn > 0) { auto& f = reg.get_or_emplace<BFlags>(e); f.burn = burn; f.burnBy = burnBy; }
        if (mv) reg.emplace_or_replace<Moving>(e, Moving{mv});
        if (hasRn) reg.emplace_or_replace<RelocNew>(e, rn);
        if (!tq.q.empty()) reg.emplace_or_replace<TrainQ>(e, tq);
        if (hasSp && reg.all_of<Unit>(e)) reg.emplace_or_replace<Support>(e, sp);
    }
}
