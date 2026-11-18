// server_ships.inc - naval transport, ferry logistics and path-line data. Included INSIDE `struct Server` (server.cpp).
//
// ======================================================================================================
// PUBLIC API (usable from any other Server code, e.g. Agent B's planDefenseRoute)
// ======================================================================================================
//   TransportPlan planTransport(int owner, const std::vector<entt::entity>& group, Pos to, uint32_t destUid = 0, float boatFactor = .75f);
//       Estimates the best way for `group` (troop units of `owner`, all walking together) to reach `to` and decides whether one of
//       the owner's idle ships beats walking. Compares   land time = A* land path length / slowest unit speed  (partial or missing
//       path = infinity)   with   boat time = max(walk to the boarding shore, ship arrives there) + embark + water route + disembark
//       + walk from the landing shore.  `plan.useBoat` is true only when boatTime < boatFactor * landTime (default 75%).
//       `plan.deferred` is true when the planner has no A* budget left this tick: call again a little later (budget: 1 plan/tick).
//       Cheap early-outs: straight distance < FERRY_MIN_DIST tiles, or no free idle ship of the owner => returns immediately.
//       `destUid` = uid of a destination building so that the land path may end inside its footprint (0 = none).
//   bool startTransport(const TransportPlan& plan, const std::vector<entt::entity>& group, Pos dest, uint32_t destUid = 0);
//       Executes a plan with useBoat == true: group walks to plan.board, the ship sails to plan.boardWater, the units embark
//       (strict capacity), the ship sails to plan.landWater, the units disembark at plan.land and walk on to `dest`
//       (Haul pawns resume their own routine instead).  Returns false when the plan is stale (ship gone/busy).
//   TransportPlan fields: useBoat, deferred, ship (entity), shipUid, board/land (land shore tiles), boardWater/landWater
//       (ocean tiles next to them), etaBoat, etaLand (seconds; 1e9 = unreachable).
//   Haul pawns call this automatically from runHaul (see tryFerryHaul).
//
// COMPONENTS: Ship (passenger list, on every K_Ship), Aboard (on a unit while it is on a ship: unit is hidden/untargetable),
//   Boarding (unit walking to embark), Voyage (a ship executing a planned transport).
// PROTOCOL: C_Board [u8 n][u32 ids][u32 ship], C_Disembark [u32 ship], S_Paths (path lines), S_ShipInfo (cargo breakdown of own ships).
// S_State for ships: aux = tier, prog = troops aboard, stock = resources aboard. Units aboard: sent to allies only, prog == UNIT_ABOARD_PROG.
// Save: append-only section after the entity list: [u32 nShips]{u32 shipUid, u16 n, u32 passengerUid * n}. Ship tier = Info.level.
// ======================================================================================================

struct Ship { std::vector<uint32_t> pax; double idleSince = 0; };
struct Aboard { uint32_t ship; };
struct Boarding { uint32_t ship; bool meetSet = false; Pos meet{0, 0}; float repath = 0; int stuck = 0; };
struct Voyage {
    uint8_t phase = 0;                                   // 0 gather, 1 sail, 2 unload
    std::vector<uint32_t> units;
    IV board{0, 0}, boardWater{0, 0}, land{0, 0}, landWater{0, 0};
    Pos dest{0, 0};
    uint32_t destUid = 0;
    double start = 0, arrivedAt = -1, lastOrder = -100;
    int fails = 0;
    bool retried = false;
};
struct TransportPlan {
    bool useBoat = false, deferred = false;
    entt::entity ship = entt::null;
    uint32_t shipUid = 0;
    IV board{0, 0}, boardWater{0, 0}, land{0, 0}, landWater{0, 0};
    float etaBoat = 1e9f, etaLand = 1e9f;
};

static constexpr float FERRY_MIN_DIST = 14.f, FERRY_IDLE = 6.f, BOARD_REACH = 2.6f, INF_T = 1e9f;
int planBudget = 0;
std::unordered_map<uint32_t, uint64_t> pathSent[MAX_PLAYERS + 1];   // per player: uid -> hash of the path last sent

static Pos tileCenter(IV t) { return {float(t.x) + .5f, float(t.y) + .5f}; }
static IV tileOf(Pos p) { return {int(std::floor(p.x)), int(std::floor(p.y))}; }

float cargoTotal(entt::entity e) const {
    const Haul* h = reg.try_get<Haul>(e);
    if (!h) return 0.f;
    float t = 0;
    for (float c : h->cargo) t += c;
    return t;
}
// Passengers actually aboard (stale uids are purged).
void paxStats(entt::entity ship, int& troops, float& res, int* kinds = nullptr) {
    auto& sh = reg.get<Ship>(ship);
    uint32_t suid = reg.get<Uid>(ship).id;
    std::erase_if(sh.pax, [&](uint32_t uid) {
        auto e = find(uid);
        return e == entt::null || !reg.all_of<Aboard>(e) || reg.get<Aboard>(e).ship != suid;
    });
    troops = 0;
    res = 0;
    if (kinds) for (int i = 0; i < 5; i++) kinds[i] = 0;
    for (uint32_t uid : sh.pax) {
        auto e = find(uid);
        troops++;
        res += cargoTotal(e);
        Kind k = reg.get<Info>(e).kind;
        if (kinds && k >= K_Pawn && k <= K_Monk) kinds[k - K_Pawn]++;
    }
}
// Free capacity, counting units that are already walking to embark.
void shipFree(entt::entity ship, int& troopsFree, float& resFree, bool reserved = true) {
    int t;
    float r;
    paxStats(ship, t, r);
    int lvl = reg.get<Info>(ship).level;
    troopsFree = shipTroopCap(lvl) - t;
    resFree = float(shipResCap(lvl)) - r;
    if (!reserved) return;
    uint32_t suid = reg.get<Uid>(ship).id;
    reg.view<Boarding>().each([&](entt::entity e, const Boarding& b) {
        if (b.ship != suid) return;
        troopsFree--;
        resFree -= cargoTotal(e);
    });
}
void initShip(entt::entity ship, int harbourLevel) {          // called when a Harbour builds a ship: empty boat, tier = harbour level
    auto& in = reg.get<Info>(ship);
    in.level = uint8_t(shipTier(harbourLevel));
    auto& h = reg.get<Health>(ship);
    h.hp = h.maxhp = KINFO[K_Ship].hp * shipHpMul(in.level);
    if (!reg.all_of<Ship>(ship)) reg.emplace<Ship>(ship, Ship{{}, clock});
}
void shipDestroyed(entt::entity ship) {                       // passengers go down with the ship
    auto* sh = reg.try_get<Ship>(ship);
    if (!sh) return;
    std::vector<uint32_t> pax = sh->pax;
    int ow = reg.get<Owner>(ship).id;
    reg.remove<Ship>(ship);
    int n = 0;
    for (uint32_t uid : pax) {
        auto e = find(uid);
        if (e == entt::null) continue;
        n++;
        destroyNow(e);
    }
    if (n) say(ow, "Your ship sank with " + std::to_string(n) + " unit(s) aboard!");
}

// ---------------------------------------------------------------- embark / disembark
void embark(entt::entity e, entt::entity ship) {
    auto& u = reg.get<Unit>(e);
    reg.remove<Attack>(e);
    reg.remove<AMove>(e);
    reg.remove<Boarding>(e);
    u.path.clear();
    u.i = 0;
    u.state = US_Idle;
    reg.emplace_or_replace<Aboard>(e, Aboard{reg.get<Uid>(ship).id});
    reg.get<Ship>(ship).pax.push_back(reg.get<Uid>(e).id);
    reg.get<Pos>(e) = reg.get<Pos>(ship);
}
// Free land tiles around `center` (ring search, nearest first).
std::vector<IV> freeLandTiles(Pos center, int rmax) {
    std::vector<std::pair<float, IV>> c;
    IV t = tileOf(center);
    for (int dy = -rmax; dy <= rmax; dy++)
        for (int dx = -rmax; dx <= rmax; dx++) {
            int x = t.x + dx, y = t.y + dy;
            if (!walkable(x, y, false, 0)) continue;
            c.push_back({std::hypot(float(x) + .5f - center.x, float(y) + .5f - center.y), IV{x, y}});
        }
    std::sort(c.begin(), c.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<IV> out;
    for (auto& p : c) out.push_back(p.second);
    return out;
}
// Put units that are aboard `ship` onto land tiles near `center`; returns the units that were placed.
std::vector<entt::entity> landUnits(entt::entity ship, const std::vector<entt::entity>& us, Pos center, int rmax) {
    std::vector<entt::entity> placed;
    auto tiles = freeLandTiles(center, rmax);
    if (tiles.empty()) return placed;
    auto& sh = reg.get<Ship>(ship);
    size_t n = 0;
    for (auto e : us) {
        if (!reg.valid(e) || !reg.all_of<Aboard>(e)) continue;
        IV t = tiles[n % tiles.size()];
        float j = float(n / tiles.size()) * .3f;                 // more units than tiles: spread inside the tile
        Pos p{float(t.x) + .5f + j - .15f, float(t.y) + .5f + (n % 2 ? j : -j) * .5f};
        reg.get<Pos>(e) = p;
        reg.remove<Aboard>(e);
        auto& u = reg.get<Unit>(e);
        u.path.clear();
        u.i = 0;
        uint32_t uid = reg.get<Uid>(e).id;
        std::erase(sh.pax, uid);
        placed.push_back(e);
        n++;
    }
    return placed;
}
std::vector<entt::entity> passengers(entt::entity ship) {
    std::vector<entt::entity> v;
    for (uint32_t uid : reg.get<Ship>(ship).pax) { auto e = find(uid); if (e != entt::null && reg.all_of<Aboard>(e)) v.push_back(e); }
    return v;
}
void cancelVoyage(entt::entity ship) {
    auto* v = reg.try_get<Voyage>(ship);
    if (!v) return;
    for (uint32_t uid : v->units) {
        auto e = find(uid);
        if (e == entt::null) continue;
        if (auto* b = reg.try_get<Boarding>(e)) if (b->meetSet) { reg.remove<Boarding>(e); reg.get<Unit>(e).path.clear(); }
    }
    reg.remove<Voyage>(ship);
}
void runBoarding(entt::entity e, Unit& u, Boarding& bd, float dt) {
    auto ship = find(bd.ship);
    int ow = reg.get<Owner>(e).id;
    if (ship == entt::null || !reg.all_of<Ship>(ship) || reg.get<Owner>(ship).id != ow || (bd.meetSet && !reg.all_of<Voyage>(ship))) { reg.remove<Boarding>(e); u.path.clear(); return; }
    Pos p = reg.get<Pos>(e), sp = reg.get<Pos>(ship);
    if (std::hypot(p.x - sp.x, p.y - sp.y) <= BOARD_REACH) {
        int tf;
        float rf;
        shipFree(ship, tf, rf, false);
        float need = cargoTotal(e);
        if (tf >= 1 && rf + 1e-3f >= need) embark(e, ship);
        else { say(ow, "The ship is full."); reg.remove<Boarding>(e); u.path.clear(); }
        return;
    }
    bd.repath -= dt;
    if (bd.repath > 0) return;
    bd.repath = 1.5f + float(reg.get<Uid>(e).id % 10) * .1f;
    if (bd.meetSet) {
        if (u.path.empty() && !order(e, bd.meet) && ++bd.stuck > 4) { reg.remove<Boarding>(e); u.path.clear(); }
        return;
    }
    if (order(e, sp)) bd.stuck = 0;
    else if (++bd.stuck >= 4) { say(ow, "Cannot reach the ship: move it next to the coast."); reg.remove<Boarding>(e); u.path.clear(); }
}

// ---------------------------------------------------------------- shore search & transport planner
// Land tiles that touch the ocean (4-neighbourhood), nearest to `around` first, at least 5 tiles apart.
void shoreTiles(Pos around, int maxR, size_t want, std::vector<IV>& out) {
    out.clear();
    IV c = tileOf(around);
    int firstR = -1;
    for (int r = 0; r <= maxR; r++) {
        if (out.size() >= want || (firstR >= 0 && r > firstR + 6)) break;
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                int x = c.x + dx, y = c.y + dy;
                if (!walkable(x, y, false, 0)) continue;
                if (biome(x + 1, y) != B_Ocean && biome(x - 1, y) != B_Ocean && biome(x, y + 1) != B_Ocean && biome(x, y - 1) != B_Ocean) continue;
                bool apart = true;
                for (auto& o : out) if (std::abs(o.x - x) < 5 && std::abs(o.y - y) < 5) apart = false;
                if (!apart || out.size() >= want) continue;
                out.push_back({x, y});
                if (firstR < 0) firstR = r;
            }
    }
}
IV waterNextTo(IV s) {                                       // the ocean neighbour with the most ocean around it (open water)
    IV best{s.x, s.y};
    int bs = -1;
    static const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto& d : d4) {
        int x = s.x + d[0], y = s.y + d[1];
        if (biome(x, y) != B_Ocean) continue;
        int sc = 0;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (biome(x + dx, y + dy) == B_Ocean) sc++;
        if (sc > bs) { bs = sc; best = {x, y}; }
    }
    return bs < 0 ? IV{INT_MIN, INT_MIN} : best;
}
static float pathLen(Pos start, const std::vector<Pos>& p) {
    float l = 0;
    Pos c = start;
    for (auto& q : p) { l += std::hypot(q.x - c.x, q.y - c.y); c = q; }
    return l;
}
// Route length in tiles from `a` to `b`; INF when there is no complete route.
float routeLen(Pos a, IV b, bool water, uint32_t ignore, int maxNodes, float tol = 2.6f) {
    std::vector<Pos> p;
    if (tileOf(a).x == b.x && tileOf(a).y == b.y) return 0.f;
    if (!findPath(tileOf(a), b, water, ignore, p, maxNodes)) return INF_T;
    Pos last = p.back(), g = tileCenter(b);
    if (std::hypot(last.x - g.x, last.y - g.y) > tol) return INF_T;
    return pathLen(a, p);
}
TransportPlan planTransport(int owner, const std::vector<entt::entity>& group, Pos to, uint32_t destUid = 0, float boatFactor = .75f) {
    TransportPlan plan;
    if (group.empty() || !isPlayer(owner)) return plan;
    Pos from{0, 0};
    int n = 0;
    float speed = 1e9f, cargo = 0;
    for (auto e : group) {
        if (!reg.valid(e) || !reg.all_of<Unit, Pos, Info>(e) || reg.all_of<Aboard>(e)) continue;
        Kind k = reg.get<Info>(e).kind;
        if (!isTroop(k)) return plan;
        Pos p = reg.get<Pos>(e);
        from.x += p.x;
        from.y += p.y;
        n++;
        speed = std::min(speed, KINFO[k].speed * unitSpeedMul(ulv(owner, k)));
        cargo += cargoTotal(e);
    }
    if (!n) return plan;
    from.x /= float(n);
    from.y /= float(n);
    if (std::hypot(from.x - to.x, from.y - to.y) < FERRY_MIN_DIST) return plan;
    struct Cand { entt::entity e; Pos p; float shipSpeed; };
    std::vector<Cand> ships;
    reg.view<Ship, Owner, Info, Pos, Unit>().each([&](entt::entity e, const Ship&, const Owner& o, const Info& in, const Pos& p, const Unit& u) {
        if (o.id != owner || in.kind != K_Ship || reg.all_of<Voyage>(e) || !u.path.empty() || clock - reg.get<Ship>(e).idleSince < FERRY_IDLE || dying.count(reg.get<Uid>(e).id)) return;
        int tf;
        float rf;
        shipFree(e, tf, rf);
        if (reg.get<Ship>(e).pax.size() || tf < n || rf + 1e-3f < cargo || tf < shipTroopCap(in.level)) return;   // fully empty, unreserved ships only
        ships.push_back({e, p, KINFO[K_Ship].speed});
    });
    if (ships.empty()) return plan;
    if (planBudget <= 0) { plan.deferred = true; return plan; }
    planBudget--;

    // land alternative
    IV fromT = tileOf(from), toT = tileOf(to);
    float landLen = routeLen(from, toT, false, destUid, 12000, 4.2f);
    plan.etaLand = landLen >= INF_T * .5f ? INF_T : landLen / speed;
    if (plan.etaLand < INF_T * .5f && plan.etaLand < 6.f) return plan;

    std::vector<IV> bCands, lCands;
    shoreTiles(from, 40, 3, bCands);
    shoreTiles(to, 40, 3, lCands);
    if (bCands.empty() || lCands.empty()) return plan;
    std::sort(ships.begin(), ships.end(), [&](const Cand& a, const Cand& b) { return std::hypot(a.p.x - from.x, a.p.y - from.y) < std::hypot(b.p.x - from.x, b.p.y - from.y); });
    if (ships.size() > 2) ships.resize(2);

    // boarding: nearest shore the ship can sail to
    float bestBoat = INF_T;
    for (auto& bs : bCands) {
        IV bw = waterNextTo(bs);
        if (bw.x == INT_MIN) continue;
        float walk1 = routeLen(from, bs, false, 0, 5000);
        if (walk1 >= INF_T * .5f) continue;
        for (auto& sc : ships) {
            float sail0 = routeLen(sc.p, bw, true, 0, 15000);
            if (sail0 >= INF_T * .5f) continue;
            float gather = std::max(walk1 / speed, sail0 / sc.shipSpeed) + 2.f;
            // landing: shore near the destination reachable from the boarding water
            for (auto& ls : lCands) {
                IV lw = waterNextTo(ls);
                if (lw.x == INT_MIN || (ls.x == bs.x && ls.y == bs.y)) continue;
                float sail = routeLen(tileCenter(bw), lw, true, 0, 25000);
                if (sail >= INF_T * .5f) continue;
                float walk2 = routeLen(tileCenter(ls), toT, false, destUid, 5000, 4.2f);
                if (walk2 >= INF_T * .5f) continue;
                float eta = gather + sail / sc.shipSpeed + 2.f + walk2 / speed;
                if (eta < bestBoat) {
                    bestBoat = eta;
                    plan.ship = sc.e;
                    plan.shipUid = reg.get<Uid>(sc.e).id;
                    plan.board = bs;
                    plan.boardWater = bw;
                    plan.land = ls;
                    plan.landWater = lw;
                }
            }
            break;                                           // the nearest ship that can reach this shore is enough
        }
    }
    plan.etaBoat = bestBoat;
    plan.useBoat = bestBoat < INF_T * .5f && bestBoat < boatFactor * plan.etaLand;
    static const bool dbg = std::getenv("BW_SHIPDEBUG") != nullptr;
    if (dbg) std::printf("[ferry] plan owner %d group %d from %.0f,%.0f to %.0f,%.0f: land %.1fs boat %.1fs -> %s\n", owner, n, from.x, from.y, to.x, to.y, plan.etaLand, plan.etaBoat, plan.useBoat ? "BOAT" : "walk");
    return plan;
}
bool startTransport(const TransportPlan& plan, const std::vector<entt::entity>& group, Pos dest, uint32_t destUid = 0) {
    if (!plan.useBoat || plan.ship == entt::null || !reg.valid(plan.ship) || !reg.all_of<Ship>(plan.ship) || reg.all_of<Voyage>(plan.ship)) return false;
    if (!reg.get<Ship>(plan.ship).pax.empty()) return false;
    Voyage v;
    v.board = plan.board; v.boardWater = plan.boardWater; v.land = plan.land; v.landWater = plan.landWater;
    v.dest = dest; v.destUid = destUid; v.start = clock;
    if (!order(plan.ship, tileCenter(plan.boardWater))) return false;
    Pos meet = tileCenter(plan.board);
    for (auto e : group) {
        if (!reg.valid(e) || !reg.all_of<Unit>(e) || reg.all_of<Aboard>(e)) continue;
        reg.remove<Attack>(e);
        reg.remove<AMove>(e);
        if (!order(e, meet)) continue;
        Boarding b;
        b.ship = plan.shipUid;
        b.meetSet = true;
        b.meet = meet;
        reg.emplace_or_replace<Boarding>(e, b);
        v.units.push_back(reg.get<Uid>(e).id);
    }
    if (v.units.empty()) { reg.get<Unit>(plan.ship).path.clear(); return false; }
    reg.emplace<Voyage>(plan.ship, v);
    return true;
}
// Haul pawns: 0 = walk, 1 = a voyage was started, 2 = planner budget exhausted (retry soon)
int tryFerryHaul(entt::entity e, Haul& h, entt::entity target) {
    int ow = reg.get<Owner>(e).id;
    std::vector<entt::entity> g{e};
    Pos to = posOf(target);
    uint32_t tuid = reg.get<Uid>(target).id;
    TransportPlan plan = planTransport(ow, g, to, tuid);
    if (plan.deferred) return 2;
    h.boatPhase = h.phase;
    if (!plan.useBoat) return 0;
    if (!startTransport(plan, g, to, tuid)) return 0;
    say(ow, "A hauling party boards a ship.");
    return 1;
}

// ---------------------------------------------------------------- per-tick ship logic
void tickVoyage(entt::entity ship, Voyage& v) {
    auto& su = reg.get<Unit>(ship);
    Pos sp = reg.get<Pos>(ship);
    std::erase_if(v.units, [&](uint32_t uid) { auto e = find(uid); return e == entt::null || (!reg.all_of<Aboard>(e) && !reg.all_of<Boarding>(e)); });
    if (v.phase == 0) {
        int aboard = 0, waiting = 0;
        for (uint32_t uid : v.units) (reg.all_of<Aboard>(find(uid)) ? aboard : waiting)++;
        Pos wc = tileCenter(v.boardWater);
        bool there = std::hypot(sp.x - wc.x, sp.y - wc.y) < 2.2f;
        if (!there && su.path.empty() && clock - v.lastOrder > 3.0) {
            v.lastOrder = clock;
            if (!order(ship, wc) && ++v.fails > 3) { cancelVoyage(ship); return; }
        }
        if (there && v.arrivedAt < 0) v.arrivedAt = clock;
        bool timeout = (v.arrivedAt >= 0 && clock - v.arrivedAt > 25.0) || clock - v.start > 150.0;
        if (waiting == 0 || timeout) {
            for (uint32_t uid : v.units) {
                auto e = find(uid);
                if (reg.all_of<Aboard>(e)) continue;
                reg.remove<Boarding>(e);
                reg.get<Unit>(e).path.clear();
            }
            std::erase_if(v.units, [&](uint32_t uid) { return !reg.all_of<Aboard>(find(uid)); });
            if (v.units.empty()) { reg.remove<Voyage>(ship); return; }
            v.phase = 1;
            v.lastOrder = clock;
            if (!order(ship, tileCenter(v.landWater))) v.phase = 2;
        }
    } else if (v.phase == 1) {
        if (su.path.empty()) {
            Pos lc = tileCenter(v.landWater);
            if (std::hypot(sp.x - lc.x, sp.y - lc.y) < 3.f || v.retried) v.phase = 2;
            else { v.retried = true; if (!order(ship, lc)) v.phase = 2; }
        }
        if (clock - v.start > 400.0) v.phase = 2;
    }
    if (v.phase == 2) {
        auto us = passengers(ship);
        auto placed = landUnits(ship, us, tileCenter(v.land), 4);
        if (placed.empty() && !us.empty()) placed = landUnits(ship, us, sp, 3);
        for (auto e : placed) if (!reg.all_of<Haul>(e)) order(e, v.dest, v.destUid);
        reg.get<Ship>(ship).idleSince = clock - FERRY_IDLE;       // our own ferry: immediately available for the next run
        reg.remove<Voyage>(ship);
    }
}
void shipsTick(float dt) {
    planBudget = 1;
    std::vector<entt::entity> ships;
    reg.view<Info, Unit, Pos, Owner>().each([&](entt::entity e, const Info& in, const Unit&, const Pos&, const Owner&) { if (in.kind == K_Ship) ships.push_back(e); });
    for (auto e : ships) {
        if (!reg.valid(e) || dying.count(reg.get<Uid>(e).id)) continue;
        if (!reg.all_of<Ship>(e)) reg.emplace<Ship>(e, Ship{{}, clock});
        auto& sh = reg.get<Ship>(e);
        auto& su = reg.get<Unit>(e);
        Pos sp = reg.get<Pos>(e);
        if (!su.path.empty() || reg.all_of<Voyage>(e)) sh.idleSince = clock;
        for (uint32_t uid : sh.pax) { auto q = find(uid); if (q != entt::null && reg.all_of<Aboard>(q)) reg.get<Pos>(q) = sp; }
        if (auto* v = reg.try_get<Voyage>(e)) { tickVoyage(e, *v); continue; }
        if (sh.pax.empty() || !su.path.empty() || clock - sh.idleSince < 6.0) continue;
        // orphans: hauling pawns aboard a ship that no longer serves a voyage must get off
        std::vector<entt::entity> orphans;
        for (auto q : passengers(e)) if (reg.all_of<Haul>(q)) orphans.push_back(q);
        if (orphans.empty()) continue;
        if (!landUnits(e, orphans, sp, 2).empty()) continue;
        std::vector<IV> sh2;
        shoreTiles(sp, 30, 1, sh2);
        if (!sh2.empty()) { IV w = waterNextTo(sh2[0]); if (w.x != INT_MIN) order(e, tileCenter(w)); }
        sh.idleSince = clock - 4.0;
    }
    (void)dt;
}

// ---------------------------------------------------------------- commands
bool shipPacket(int id, uint8_t type, Reader& r) {
    if (type == C_Board) {
        int n = r.get<uint8_t>();
        std::vector<uint32_t> ids;
        for (int i = 0; i < n; i++) ids.push_back(r.get<uint32_t>());
        auto ship = find(r.get<uint32_t>());
        if (!r.ok || ship == entt::null || !reg.all_of<Ship>(ship) || reg.get<Owner>(ship).id != id) return true;
        if (reg.all_of<Voyage>(ship)) { say(id, "That ship is busy on a supply run."); return true; }
        int tf;
        float rf;
        shipFree(ship, tf, rf);
        int lvl = reg.get<Info>(ship).level, taken = 0, over = 0;
        Pos sp = reg.get<Pos>(ship);
        for (uint32_t uid : ids) {
            auto e = find(uid);
            if (e == entt::null || !reg.all_of<Unit, Owner, Info, Pos>(e) || reg.get<Owner>(e).id != id || !isTroop(reg.get<Info>(e).kind)) continue;
            if (reg.all_of<Worker>(e) || reg.all_of<Convoy>(e) || reg.all_of<Haul>(e) || reg.all_of<Raider>(e) || reg.all_of<Aboard>(e)) continue;
            if (taken >= tf) { over++; continue; }
            reg.remove<Attack>(e);
            reg.remove<AMove>(e);
            Boarding b;
            b.ship = reg.get<Uid>(ship).id;
            reg.emplace_or_replace<Boarding>(e, b);
            auto& u = reg.get<Unit>(e);
            u.path.clear();
            if (std::hypot(reg.get<Pos>(e).x - sp.x, reg.get<Pos>(e).y - sp.y) > BOARD_REACH && !order(e, sp)) {
                reg.remove<Boarding>(e);
                say(id, "Cannot reach the ship: move it next to the coast.");
                continue;
            }
            taken++;
        }
        if (over) say(id, std::string("The ") + SHIP_NAME[shipTier(lvl) - 1] + " only carries " + std::to_string(shipTroopCap(lvl)) + " troops: " + std::to_string(over) + " could not board.");
        return true;
    }
    if (type == C_Disembark) {
        auto ship = find(r.get<uint32_t>());
        if (!r.ok || ship == entt::null || !reg.all_of<Ship>(ship) || reg.get<Owner>(ship).id != id) return true;
        cancelVoyage(ship);
        auto us = passengers(ship);
        if (us.empty()) return true;
        auto placed = landUnits(ship, us, reg.get<Pos>(ship), 2);
        if (placed.empty()) say(id, "Move the ship next to a coastline to disembark.");
        else say(id, std::to_string(placed.size()) + " unit(s) disembarked.");
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- outgoing
// Called from sendState for every record: false hides it. Fills the ship/boarded fields.
bool shipStateHook(int viewer, entt::entity e, int ownerId, const Info& in, uint8_t& aux, uint8_t& prog, uint16_t& stock) {
    if (reg.all_of<Aboard>(e)) {
        if (!allied(viewer, ownerId)) return false;
        prog = UNIT_ABOARD_PROG;
        return true;
    }
    if (in.kind == K_Ship && reg.all_of<Ship>(e)) {
        int t;
        float rs;
        paxStats(e, t, rs);
        aux = uint8_t(shipTier(in.level));
        prog = uint8_t(std::min(t, 254));
        stock = uint16_t(std::min(rs, 65000.f));
    }
    return true;
}
void sendShipInfo(int id) {
    Writer w;
    w.put<uint8_t>(S_ShipInfo);
    size_t cp = w.b.size();
    w.put<uint8_t>(0);
    uint8_t n = 0;
    reg.view<Ship, Owner, Uid, Info>().each([&](entt::entity e, const Ship&, const Owner& o, const Uid& uid, const Info& in) {
        if (o.id != id || n >= 60) return;
        int t, kinds[5];
        float rs;
        paxStats(e, t, rs, kinds);
        float res[R_Count]{};
        for (auto q : passengers(e)) if (auto* h = reg.try_get<Haul>(q)) for (int r = 0; r < R_Count; r++) res[r] += h->cargo[r];
        w.put<uint32_t>(uid.id).put<uint8_t>(uint8_t(shipTier(in.level)));
        for (int k : kinds) w.put<uint8_t>(uint8_t(k));
        for (float v : res) w.put<uint16_t>(uint16_t(std::min(v, 65000.f)));
        w.put<uint8_t>(reg.all_of<Voyage>(e) ? 1 : 0);
        n++;
    });
    w.b[cp] = n;
    net.send(pl[id].conn, w);
}
// Path lines: waypoints of every unit / ship of the viewer and of players sharing vision with the viewer (never anybody else).
void sendPaths(int id) {
    auto& sent = pathSent[id];
    if (!pl[id].online) { sent.clear(); return; }
    struct C { uint32_t uid; float d; std::vector<IV> pts; uint64_t h; };
    std::vector<C> cs;
    Pos cam = pl[id].cam;
    reg.view<Unit, Owner, Pos, Uid>().each([&](entt::entity e, const Unit& u, const Owner& o, const Pos& p, const Uid& uid) {
        if (!isPlayer(o.id) || (o.id != id && !shares(id, o.id)) || u.i >= u.path.size() || reg.all_of<Aboard>(e)) return;
        cs.push_back({uid.id, std::hypot(p.x - cam.x, p.y - cam.y), {}, 0});
        C& c = cs.back();
        size_t n = u.path.size(), stride = std::max<size_t>(3, (n + 11) / 12);
        for (size_t j = u.i; j < n; j++)
            if (j == n - 1 || (j + 1) % stride == 0) c.pts.push_back({int(std::floor(u.path[j].x)), int(std::floor(u.path[j].y))});
        if (c.pts.size() > 14) c.pts.resize(14);
        uint64_t h = 1469598103934665603ull ^ uid.id;
        for (auto& q : c.pts) { h = (h ^ uint64_t(uint32_t(q.x))) * 1099511628211ull; h = (h ^ uint64_t(uint32_t(q.y))) * 1099511628211ull; }
        c.h = h;
    });
    std::sort(cs.begin(), cs.end(), [](const C& a, const C& b) { return a.d < b.d; });
    if (cs.size() > 60) cs.resize(60);
    std::unordered_set<uint32_t> active;
    for (auto& c : cs) active.insert(c.uid);
    Writer w;
    w.put<uint8_t>(S_Paths);
    size_t cp = w.b.size();
    w.put<uint16_t>(0);
    uint16_t n = 0;
    constexpr uint16_t MAXE = 48;
    for (auto it = sent.begin(); it != sent.end() && n < MAXE;) {     // paths that ended: clear on the client
        if (active.count(it->first)) { ++it; continue; }
        w.put<uint32_t>(it->first).put<uint8_t>(0);
        n++;
        it = sent.erase(it);
    }
    for (auto& c : cs) {
        if (n >= MAXE) break;
        auto f = sent.find(c.uid);
        if (f != sent.end() && f->second == c.h) continue;
        sent[c.uid] = c.h;
        w.put<uint32_t>(c.uid).put<uint8_t>(uint8_t(c.pts.size()));
        for (auto& q : c.pts) w.put<int32_t>(q.x).put<int32_t>(q.y);
        n++;
    }
    if (!n) return;
    std::memcpy(&w.b[cp], &n, 2);
    net.send(pl[id].conn, w);
}

// ---------------------------------------------------------------- persistence (append-only section after the entity list)
void shipsSave(Writer& w) {
    std::vector<entt::entity> ships;
    reg.view<Ship>().each([&](entt::entity e, const Ship& s) { if (!s.pax.empty()) ships.push_back(e); });
    w.put<uint32_t>(uint32_t(ships.size()));
    for (auto e : ships) {
        int t;
        float rs;
        paxStats(e, t, rs);
        const auto& pax = reg.get<Ship>(e).pax;
        w.put<uint32_t>(reg.get<Uid>(e).id).put<uint16_t>(uint16_t(pax.size()));
        for (uint32_t u : pax) w.put<uint32_t>(u);
    }
}
void shipsLoad(Reader& r) {
    reg.view<Info>().each([&](entt::entity e, const Info& in) { if (in.kind == K_Ship && !reg.all_of<Ship>(e)) reg.emplace<Ship>(e, Ship{{}, clock}); });
    if (r.p >= r.e) return;                                  // older savegame without a ship section
    uint32_t n = r.get<uint32_t>();
    for (uint32_t i = 0; i < n && r.ok; i++) {
        uint32_t suid = r.get<uint32_t>();
        int cnt = r.get<uint16_t>();
        auto ship = find(suid);
        for (int j = 0; j < cnt && r.ok; j++) {
            uint32_t uid = r.get<uint32_t>();
            auto e = find(uid);
            if (ship == entt::null || e == entt::null || !reg.all_of<Unit>(e) || !reg.all_of<Ship>(ship)) continue;
            reg.emplace_or_replace<Aboard>(e, Aboard{suid});
            reg.get<Ship>(ship).pax.push_back(uid);
            reg.get<Pos>(e) = reg.get<Pos>(ship);
        }
    }
}

// Test hook: BW_SPAWN=x,y puts the first new base at the clearing nearest to that point.
bool testSpawn(IV& c) {
    static const char* s = std::getenv("BW_SPAWN");
    if (!s) return false;
    int x = 0, y = 0;
    if (std::sscanf(s, "%d,%d", &x, &y) != 2) return false;
    for (int r = 0; r < 60; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                if (clearingOk(x + dx, y + dy)) { c = {x + dx, y + dy}; return true; }
            }
    return false;
}
