// Authoritative server: infinite biome world, persistence, economy with physical supply lines, combat, naval units, alliances.
#include "net.hpp"
#include "shared.hpp"

#include <entt/entt.hpp>

#include <chrono>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <queue>
#include <random>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifdef near
#undef near
#endif

namespace {

volatile std::sig_atomic_t g_quit = 0;
using Bits = std::array<uint8_t, 32>;
constexpr uint32_t SAVE_MAGIC = 0x35535742u;  // "BWS5"

struct Uid { uint32_t id; };
struct Owner { uint8_t id; };                                   // 0 = neutral
struct Health { float hp, maxhp; uint8_t lastHit; };
struct Info { Kind kind; uint8_t level; };
struct Building { int tx, ty; float progress, stock[R_Count], respawn, cd; uint8_t haulPct = 75; };   // storage buildings keep their stock here
struct Unit { std::vector<Pos> path; size_t i = 0; uint8_t state = US_Idle, aim = 0; float cd = 0, repath = 0; IV lastRev{INT_MIN, INT_MIN}; };
struct Attack { uint32_t target; };
struct AMove { Pos dest; };
struct Worker { uint32_t home; uint8_t phase = 0; float cargo[R_Count]{}; float timer = 0; uint32_t dest = 0; };  // 0 wait, 1 loading, 2 carry, 3 return
struct Wander { float timer; };
struct Convoy { uint8_t from, to; float cargo[R_Count]; float repath = 0, bestDist = 1e9f; int stuck = 0; };   // tribute pawn walking to a clan mate
struct Haul { uint32_t storage; uint8_t phase = 0; float cargo[R_Count]{}; float repath = 0, bestDist = 1e9f; int stuck = 0; uint8_t boatPhase = 255; };   // pawn troop fetching stock from an outpost / storage house
struct Raider { uint8_t target; double until; float repath = 0; };                                                         // faction raiding party
struct Home { float x, y, helpUntil = 0; };                                                                    // faction units idle around their town
struct TownTag { uint16_t town; };
struct Hit { uint32_t by = 0; double t = -100; };                                                              // last attacker
struct Clan { bool used = false; std::string name; uint8_t leader = 0; };
struct Town { uint16_t id; uint8_t faction; int cx, cy; uint32_t th; bool alive; double respawnAt, nextSpawn, lastAlert; };

struct Profile {
    bool used = false, online = false;
    uint64_t conn = 0;                                           // transport connection id (ENet peer or Steam connection)
    uint64_t steamId = 0;
    std::string uuid, name;
    uint64_t lastSeen = 0;
    float res[R_Count]{};
    bool allied[MAX_PLAYERS + 1]{}, pending[MAX_PLAYERS + 1]{};      // pending[x] = I invited x
    Pos cam{0, 0};
    std::unordered_map<int64_t, Bits> expl;                          // permanently explored tiles, per chunk
    std::unordered_map<int64_t, Bits> sentFog;
    std::unordered_set<int64_t> sentTerr;
    double lastWarn = -100;
    uint8_t clan = 0, ulevel[K_Count]{};
    double raidNext[FACTION_COUNT]{}, lastLootWarn = -100;
    float rep[FACTION_COUNT]{};
    double helpCool[FACTION_COUNT]{};
};

struct Eff { uint8_t type; float x, y, x2, y2; };

uint64_t nowSec() { return uint64_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()); }
int64_t tkey(int x, int y) { return chunkKey(x, y); }

struct Server {
    ServerNet net;
    std::unordered_map<uint64_t, int> connPlayer;               // connection -> player id
    double lastPublish = -100;
    entt::registry reg;
    Profile pl[MAX_PLAYERS + 1];
    std::unordered_map<uint32_t, entt::entity> byUid;
    std::unordered_map<int64_t, uint32_t> occ;                        // tile -> building uid
    std::unordered_map<int64_t, std::array<uint8_t, CH * CH>> terr;   // generated terrain chunks
    std::unordered_set<int64_t> spawned;                              // chunks whose NPCs exist
    std::unordered_set<uint32_t> dying;
    std::vector<std::pair<entt::entity, int>> toKill;
    std::vector<Eff> effects;
    uint32_t seed, tick = 0, nextUid = 1;
    std::mt19937 rng;
    std::string savePath;
    double clock = 0, lastSave = 0;
    Clan clans[MAX_CLANS + 1];
    std::vector<Town> towns;
    std::unordered_set<int64_t> regionsDone;
    float facDonated[FACTION_COUNT]{};
    struct Spot { double until; uint8_t by; };
    std::unordered_map<uint8_t, std::unordered_map<uint32_t, Spot>> clanSpot;   // clan -> convoys its members have spotted

    Server(uint16_t port, uint32_t sd, std::string path, std::string serverName) : seed(sd), rng(sd), savePath(std::move(path)) {
        bool loaded = load();
        if (!net.start(port, MAX_PLAYERS + 4, serverName)) { std::puts("Failed to bind port"); std::exit(1); }
        std::printf("Server listening on UDP port %d (seed %u, %s, steam %s)\n", port, seed, loaded ? "savegame loaded" : "fresh world", net.steamActive() ? "on" : "off");
    }
    ~Server() { save(); }

    // ---------------------------------------------------------------- terrain
    int biome(int x, int y) {
        int64_t k = chunkKey(chunkOf(x), chunkOf(y));
        auto it = terr.find(k);
        if (it == terr.end()) {
            std::array<uint8_t, CH * CH> c;
            for (int j = 0; j < CH; j++)
                for (int i = 0; i < CH; i++) c[size_t(j * CH + i)] = genBiome(chunkOf(x) * CH + i, chunkOf(y) * CH + j, seed);
            it = terr.emplace(k, c).first;
        }
        return it->second[size_t(localIdx(x, y))];
    }
    bool cliff(int x, int y) { return isCliffTile(biome(x, y), biome(x, y - 1)); }
    bool walkable(int x, int y, bool water, uint32_t ignore) {
        int b = biome(x, y);
        if (water) return b == B_Ocean;
        if (!isLand(b) || isCliffTile(b, biome(x, y - 1))) return false;
        auto it = occ.find(tkey(x, y));
        return it == occ.end() || it->second == ignore;
    }

    // ---------------------------------------------------------------- helpers
    static bool isPlayer(int o) { return o >= 1 && o <= MAX_PLAYERS; }
    bool sameClan(int a, int b) const { return isPlayer(a) && isPlayer(b) && pl[a].clan && pl[a].clan == pl[b].clan; }
    bool shares(int a, int b) const { return a != b && isPlayer(a) && isPlayer(b) && (pl[a].allied[b] || sameClan(a, b)); }   // shared vision
    bool allied(int a, int b) const { return a == b || shares(a, b); }
    float repOf(int player, int owner) const { return isPlayer(player) && isFactionOwner(owner) ? pl[player].rep[owner - FACTION_BASE] : 0.f; }
    bool hostile(int a, int b) const {
        if (a < 1 || b < 1 || a == b) return false;
        if (isFactionOwner(a) && isFactionOwner(b)) return false;
        if (isFactionOwner(a)) return repOf(b, a) <= float(HOSTILE_REP);
        if (isFactionOwner(b)) return repOf(a, b) <= float(HOSTILE_REP);
        return !allied(a, b);
    }
    int ulv(int owner, Kind k) const { return isPlayer(owner) ? std::max<int>(1, pl[owner].ulevel[k]) : 1; }
    float totalRes(int id) const { float t = 0; for (float v : pl[id].res) t += v; return t; }
    float stockOf(entt::entity e) const {                                // the Town Hall stores the player's resources
        const auto& i = reg.get<Info>(e);
        int o = reg.get<Owner>(e).id;
        if (i.kind == K_TownHall && isPlayer(o)) return totalRes(o);
        float t = 0;
        for (float v : reg.get<Building>(e).stock) t += v;
        return t;
    }
    float capOf(entt::entity e) const { const auto& i = reg.get<Info>(e); return inactive(e) ? 0.f : storageCap(i.kind, i.level); }   // non-functional storage holds nothing
    // puts as much of `cargo` as fits into storage building `st`; returns the amount stored
    float storeCargo(entt::entity st, float* cargo) {
        int o = reg.get<Owner>(st).id;
        float tot = 0;
        for (int r = 0; r < R_Count; r++) tot += cargo[r];
        float room = capOf(st) - stockOf(st);
        if (tot <= 0 || room <= 0) return 0;
        float f = std::min(1.f, room / tot), stored = 0;
        bool th = reg.get<Info>(st).kind == K_TownHall;
        for (int r = 0; r < R_Count; r++) {
            float add = cargo[r] * f;
            if (th) pl[o].res[r] += add; else reg.get<Building>(st).stock[r] += add;
            cargo[r] -= add;
            stored += add;
        }
        return stored;
    }
    std::string ownerName(int o) const { return isFactionOwner(o) ? FACTION_NAME[o - FACTION_BASE] : (isPlayer(o) ? pl[o].name : std::string("Wildlife")); }
    int colorOf(int id) const { return (id - 1) % TEAM_COLORS; }

    void say(int id, const std::string& s) {
        if (id < 1 || id > MAX_PLAYERS || !pl[id].online) return;
        Writer w;
        w.put<uint8_t>(S_Text);
        w.str(s);
        net.send(pl[id].conn, w);
    }
    void fx(EffectType t, float x, float y, float x2 = 0, float y2 = 0) { effects.push_back({t, x, y, x2, y2}); }

    entt::entity find(uint32_t uid) const {
        auto it = byUid.find(uid);
        return it == byUid.end() || !reg.valid(it->second) ? entt::entity(entt::null) : it->second;
    }
    Pos posOf(entt::entity e) const {
        const auto& i = reg.get<Info>(e);
        if (isBuilding(i.kind)) { const auto& b = reg.get<Building>(e); return footCenter(i.kind, b.tx, b.ty); }
        return reg.get<Pos>(e);
    }
    float rectDist(Pos p, entt::entity e) const {
        const auto& i = reg.get<Info>(e);
        if (!isBuilding(i.kind)) { Pos q = reg.get<Pos>(e); return std::hypot(p.x - q.x, p.y - q.y); }
        const auto& b = reg.get<Building>(e);
        float dx = std::max({float(b.tx) - p.x, 0.f, p.x - float(b.tx + KINFO[i.kind].fw)});
        float dy = std::max({float(b.ty) - p.y, 0.f, p.y - float(b.ty + KINFO[i.kind].fh)});
        return std::hypot(dx, dy);
    }
    bool complete(entt::entity e) const { return !reg.all_of<Building>(e) || reg.get<Building>(e).progress >= 1.f; }

    // pooled resources: own first, then allies ("shared economy")
    bool afford(int id, const float* cost) const {
        for (int r = 0; r < R_Count; r++) {
            float have = pl[id].res[r];
            for (int a = 1; a <= MAX_PLAYERS; a++) if (a != id && pl[a].used && pl[id].allied[a]) have += pl[a].res[r];
            if (have + 1e-3f < cost[r]) return false;
        }
        return true;
    }
    void spend(int id, const float* cost) {
        for (int r = 0; r < R_Count; r++) {
            float need = cost[r], take = std::min(need, pl[id].res[r]);
            pl[id].res[r] -= take;
            need -= take;
            for (int a = 1; a <= MAX_PLAYERS && need > 0; a++)
                if (a != id && pl[a].used && pl[id].allied[a]) { float t = std::min(need, pl[a].res[r]); pl[a].res[r] -= t; need -= t; }
        }
    }

    // ---------------------------------------------------------------- fog of war
    bool bitAt(const std::unordered_map<int64_t, Bits>& m, int x, int y) const {
        auto it = m.find(chunkKey(chunkOf(x), chunkOf(y)));
        if (it == m.end()) return false;
        int i = localIdx(x, y);
        return it->second[size_t(i >> 3)] >> (i & 7) & 1;
    }
    bool explored(int id, int x, int y) const {
        if (bitAt(pl[id].expl, x, y)) return true;
        for (int a = 1; a <= MAX_PLAYERS; a++) if (a != id && pl[a].used && shares(a, id) && bitAt(pl[a].expl, x, y)) return true;
        return false;
    }
    void reveal(int id, int cx, int cy, int r) {
        if (!isPlayer(id)) return;
        auto& m = pl[id].expl;
        for (int y = cy - r; y <= cy + r; y++)
            for (int x = cx - r; x <= cx + r; x++) {
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r + r) continue;
                Bits& b = m.try_emplace(chunkKey(chunkOf(x), chunkOf(y)), Bits{}).first->second;
                int i = localIdx(x, y);
                b[size_t(i >> 3)] |= uint8_t(1 << (i & 7));
            }
    }

    // ---------------------------------------------------------------- entity creation
    entt::entity makeBase(Kind k, uint8_t owner, uint32_t uid = 0) {
        auto e = reg.create();
        if (!uid) uid = nextUid++;
        nextUid = std::max(nextUid, uid + 1);
        byUid[uid] = e;
        reg.emplace<Uid>(e, Uid{uid});
        reg.emplace<Owner>(e, Owner{owner});
        reg.emplace<Info>(e, Info{k, 1});
        reg.emplace<Health>(e, Health{KINFO[k].hp, KINFO[k].hp, 0});
        return e;
    }
    entt::entity makeUnit(Kind k, uint8_t owner, float x, float y) {
        auto e = makeBase(k, owner);
        reg.emplace<Pos>(e, Pos{x, y});
        reg.emplace<Unit>(e);
        { auto& h = reg.get<Health>(e); h.hp = h.maxhp = KINFO[k].hp * unitHpMul(ulv(owner, k)); }
        if (k == K_Sheep) reg.emplace<Wander>(e, Wander{float(rng() % 300) / 100.f});
        return e;
    }
    entt::entity makeBuilding(Kind k, uint8_t owner, int tx, int ty, bool done) {
        auto e = makeBase(k, owner);
        reg.emplace<Building>(e, Building{tx, ty, done ? 1.f : 0.f, {0, 0, 0, 0}, 0.f, 0.f});
        for (int y = ty; y < ty + KINFO[k].fh; y++)
            for (int x = tx; x < tx + KINFO[k].fw; x++) occ[tkey(x, y)] = reg.get<Uid>(e).id;
        markFootprint(k, tx, ty);                                       // [world] a footprint never needs clearing again
        if (done) reveal(owner, int(std::floor(footCenter(k, tx, ty).x)), int(std::floor(footCenter(k, tx, ty).y)), KINFO[k].vision);
        return e;
    }
    void destroyNow(entt::entity e) {
        if (!reg.valid(e)) return;
        if (reg.all_of<Ship>(e)) shipDestroyed(e);
        uint32_t uid = reg.get<Uid>(e).id;
        if (reg.all_of<Building>(e)) {
            const auto& b = reg.get<Building>(e);
            Kind k = reg.get<Info>(e).kind;
            markFootprint(k, b.tx, b.ty);                               // [world] rubble does not regrow trees
            for (int y = b.ty; y < b.ty + KINFO[k].fh; y++)
                for (int x = b.tx; x < b.tx + KINFO[k].fw; x++) occ.erase(tkey(x, y));
        }
        byUid.erase(uid);
        dying.erase(uid);
        reg.destroy(e);
    }

    // ---------------------------------------------------------------- finding a fresh clearing
    bool clearingOk(int cx, int cy) {
        for (int y = cy - 6; y <= cy + 6; y++)
            for (int x = cx - 6; x <= cx + 6; x++) {
                int b = biome(x, y);
                if (!isLand(b) || b == B_Highlands || b == B_Snow || b == B_Desert || isCliffTile(b, biome(x, y - 1))) return false;
            }
        for (int a = 1; a <= MAX_PLAYERS; a++)
            if (pl[a].used && bitAt(pl[a].expl, cx, cy)) return false;
        bool nearBase = false;
        reg.view<Info, Building>().each([&](const Info& i, const Building& b) {
            if (i.kind != K_TownHall) return;
            if (std::max(std::abs(b.tx - cx), std::abs(b.ty - cy)) < 70) nearBase = true;
        });
        return !nearBase;
    }
    IV findClearing(const Pos* anchor = nullptr) {
        if (anchor)                                                     // new bases appear close to an existing player, but outside their base
            for (int n = 0; n < 6000; n++) {
                float ang = float(rng() % 6283) / 1000.f, r = float(SPAWN_MIN_DIST + int(rng() % uint32_t(SPAWN_RADIUS - SPAWN_MIN_DIST)));
                int x = int(anchor->x + std::cos(ang) * r), y = int(anchor->y + std::sin(ang) * r);
                if (clearingOk(x, y)) return {x, y};
            }
        int ths = 0;
        reg.view<Info>().each([&](const Info& i) { if (i.kind == K_TownHall) ths++; });
        for (int pass = 0; pass < 3; pass++)
            for (int n = 0; n < 12000; n++) {
                float ang = float(rng() % 6283) / 1000.f, r = (ths ? 90.f : 0.f) + float(n) * .35f + float(pass) * 400.f;
                int x = int(std::cos(ang) * r), y = int(std::sin(ang) * r);
                if (clearingOk(x, y)) return {x, y};
            }
        return {0, 0};
    }
    // Where should a newcomer (or a player who lost their base) appear? Near Steam friends, then allies / clan mates, then any player.
    bool spawnAnchor(int id, const std::vector<uint64_t>& friends, Pos& out) {
        std::vector<Pos> friendsAt, alliesAt, anyAt;
        reg.view<Info, Building, Owner>().each([&](const Info& i, const Building& b, const Owner& o) {
            if (i.kind != K_TownHall || !isPlayer(o.id) || o.id == id) return;
            Pos c = footCenter(i.kind, b.tx, b.ty);
            bool isFriend = pl[o.id].steamId != 0 && std::find(friends.begin(), friends.end(), pl[o.id].steamId) != friends.end();
            (isFriend ? friendsAt : (allied(id, o.id) ? alliesAt : anyAt)).push_back(c);
        });
        auto& pick = !friendsAt.empty() ? friendsAt : (!alliesAt.empty() ? alliesAt : anyAt);
        if (pick.empty()) return false;
        out = pick[rng() % pick.size()];
        return true;
    }
    void spawnBase(int id, const std::vector<uint64_t>& friends = {}) {
        Pos anchor;
        IV c = spawnAnchor(id, friends, anchor) ? findClearing(&anchor) : findClearing();
        testSpawn(c);   // BW_SPAWN test hook
        auto th = makeBuilding(K_TownHall, uint8_t(id), c.x - 2, c.y - 1, true);
        Pos p = posOf(th);
        pl[id].cam = p;
        for (int i = 0; i < 2; i++) makeUnit(K_Pawn, uint8_t(id), p.x - 1.f + float(i) * 2.f, p.y + 2.6f);
        makeUnit(K_Warrior, uint8_t(id), p.x, p.y + 3.2f);
        fx(E_Dust, p.x, p.y);
    }
    void resetProfile(int id) {
        for (int r = 0; r < R_Count; r++) pl[id].res[r] = START_RES[r] * (std::getenv("BW_RICH") ? 20.f : 1.f);   // BW_RICH: test hook
    }

    // ---------------------------------------------------------------- pathfinding (A*, 8-dir, bounded, partial paths allowed)
    bool findPath(IV s, IV g, bool water, uint32_t ignore, std::vector<Pos>& out, int maxNodes = 7000) {
        out.clear();
        if (!walkable(g.x, g.y, water, ignore)) {
            bool f = false;
            for (int r = 1; r <= 8 && !f; r++)
                for (int dy = -r; dy <= r && !f; dy++)
                    for (int dx = -r; dx <= r && !f; dx++)
                        if (std::max(std::abs(dx), std::abs(dy)) == r && walkable(g.x + dx, g.y + dy, water, ignore)) { g = {g.x + dx, g.y + dy}; f = true; }
            if (!f) return false;
        }
        if (s.x == g.x && s.y == g.y) return true;
        struct PN { float g; int64_t par; bool closed; };
        std::unordered_map<int64_t, PN> nodes;
        using QE = std::pair<float, int64_t>;
        std::priority_queue<QE, std::vector<QE>, std::greater<>> q;
        auto h = [&](int x, int y) {
            float dx = float(std::abs(x - g.x)), dy = float(std::abs(y - g.y));
            return dx + dy - 0.586f * std::min(dx, dy);
        };
        int64_t sk = tkey(s.x, s.y), gk = tkey(g.x, g.y), best = sk;
        float bestH = h(s.x, s.y);
        nodes[sk] = {0, -1, false};
        q.push({bestH, sk});
        int expanded = 0;
        bool found = false;
        while (!q.empty() && expanded < maxNodes) {
            int64_t k = q.top().second;
            q.pop();
            PN& cur = nodes[k];
            if (cur.closed) continue;
            cur.closed = true;
            expanded++;
            if (k == gk) { found = true; break; }
            int x = keyCx(k), y = keyCy(k);
            float cg = cur.g;
            float ch = h(x, y);
            if (ch < bestH) { bestH = ch; best = k; }
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++) {
                    if (!dx && !dy) continue;
                    int nx = x + dx, ny = y + dy;
                    if (!walkable(nx, ny, water, ignore)) continue;
                    if (dx && dy && (!walkable(x + dx, y, water, ignore) || !walkable(x, y + dy, water, ignore))) continue;
                    int64_t nk = tkey(nx, ny);
                    float ng = cg + (dx && dy ? 1.414f : 1.f);
                    auto it = nodes.find(nk);
                    if (it == nodes.end() || ng < it->second.g) { nodes[nk] = {ng, k, false}; q.push({ng + h(nx, ny), nk}); }
                }
        }
        int64_t end = found ? gk : best;
        if (end == sk) return false;
        for (int64_t k = end; k != sk && k != -1; k = nodes[k].par) out.push_back({float(keyCx(k)) + .5f, float(keyCy(k)) + .5f});
        std::reverse(out.begin(), out.end());
        return !out.empty();
    }
    bool order(entt::entity e, Pos goal, uint32_t ignore = 0, int maxNodes = 7000) {
        auto& u = reg.get<Unit>(e);
        Pos p = reg.get<Pos>(e);
        bool water = reg.get<Info>(e).kind == K_Ship;
        std::vector<Pos> path;
        if (!findPath({int(std::floor(p.x)), int(std::floor(p.y))}, {int(std::floor(goal.x)), int(std::floor(goal.y))}, water, ignore, path, maxNodes)) return false;
        u.path = std::move(path);
        u.i = 0;
        return true;
    }

    // ---------------------------------------------------------------- combat helpers
    void addRep(int player, int f, float d) {
        if (!isPlayer(player)) return;
        float& r = pl[player].rep[f];
        float old = r;
        r = std::clamp(r + d, -100.f, 100.f);
        if (old > float(HOSTILE_REP) && r <= float(HOSTILE_REP)) say(player, std::string(FACTION_NAME[f]) + " now considers you hostile!");
        if (old < float(FRIENDLY_REP) && r >= float(FRIENDLY_REP)) say(player, std::string(FACTION_NAME[f]) + " now counts you as a friend and will come to your aid.");
        if (old <= float(HOSTILE_REP) && r > float(HOSTILE_REP)) say(player, std::string(FACTION_NAME[f]) + " no longer wants you dead.");
    }
    Town* townById(int id) { for (auto& t : towns) if (t.id == id) return &t; return nullptr; }
    void alertTown(entt::entity victim, uint32_t by) {                    // a faction town defends itself against whoever hit it
        if (!by) return;
        auto* tt = reg.try_get<TownTag>(victim);
        Town* t = tt ? townById(tt->town) : nullptr;
        auto atk = find(by);
        if (!t || atk == entt::null || clock - t->lastAlert < 3.0) return;
        t->lastAlert = clock;
        bool bld = isBuilding(reg.get<Info>(atk).kind);
        Pos ap = posOf(atk);
        std::vector<entt::entity> us;
        reg.view<TownTag, Unit, Info>().each([&](entt::entity e, const TownTag& g, const Unit&, const Info& i) { if (g.town == t->id && isArmy(i.kind)) us.push_back(e); });
        for (auto e : us) {
            if (!order(e, ap, bld ? by : 0)) continue;
            reg.emplace_or_replace<Attack>(e, Attack{by});
        }
    }
    // Whoever damages a storage building (Town Hall, Outpost, Storage House) takes resources in proportion to the damage dealt.
    void lootStorage(entt::entity e, float amt, int attacker) {
        Kind k = reg.get<Info>(e).kind;
        if (!isStorage(k) || !complete(e)) return;
        int o = reg.get<Owner>(e).id;
        float hpBefore = reg.get<Health>(e).hp + amt, frac = std::clamp(amt / std::max(1.f, hpBefore), 0.f, 1.f);
        float* stock = (k == K_TownHall && isPlayer(o)) ? pl[o].res : reg.get<Building>(e).stock;
        float taken[R_Count], tot = 0;
        for (int r = 0; r < R_Count; r++) { taken[r] = stock[r] * frac; stock[r] -= taken[r]; tot += taken[r]; }
        if (tot <= 0) return;
        if (isPlayer(attacker)) {
            std::vector<int> mates;
            for (int i = 1; i <= MAX_PLAYERS; i++) if (i != attacker && pl[i].used && sameClan(attacker, i)) mates.push_back(i);
            float mine = mates.empty() ? 1.f : .5f;              // a clan shares half of its raiders' loot
            for (int r = 0; r < R_Count; r++) {
                pl[attacker].res[r] += taken[r] * mine;
                for (int m : mates) pl[m].res[r] += taken[r] * (1.f - mine) / float(mates.size());
            }
        } else if (isFactionOwner(attacker)) facDonated[attacker - FACTION_BASE] += tot;
        if (isPlayer(o) && pl[o].online && clock - pl[o].lastLootWarn > 10) { pl[o].lastLootWarn = clock; say(o, std::string("Your ") + KINFO[k].name + " is being looted by " + ownerName(attacker) + "!"); }
    }
    void damage(entt::entity e, float amt, int attacker, uint32_t by = 0) {
        auto& h = reg.get<Health>(e);
        h.hp -= amt;
        h.lastHit = uint8_t(attacker);
        int o = reg.get<Owner>(e).id;
        baseOnDamage(e, o, attacker);                                    // clan alerts / defense dispatch (server_base.inc)
        if (by) reg.emplace_or_replace<Hit>(e, Hit{by, clock});
        if (isFactionOwner(o) && isPlayer(attacker)) {
            addRep(attacker, o - FACTION_BASE, -amt * .06f);
            alertTown(e, by);
        }
        if (attacker > 0 && attacker != o && hostile(attacker, o)) lootStorage(e, amt, attacker);
        if (isPlayer(o) && attacker != o && pl[o].online && clock - pl[o].lastWarn > 8) { pl[o].lastWarn = clock; say(o, "Your forces are under attack!"); }
        uint32_t uid = reg.get<Uid>(e).id;
        if (h.hp <= 0 && dying.insert(uid).second) toKill.push_back({e, attacker});
    }
    void processKills() {
        for (size_t n = 0; n < toKill.size(); n++) {
            auto [e, killer] = toKill[n];
            if (!reg.valid(e)) continue;
            Kind k = reg.get<Info>(e).kind;
            int owner = reg.get<Owner>(e).id;
            Pos p = posOf(e);
            if (isBuilding(k)) fx(E_Explosion, p.x, p.y);
            auto raid = [&](const float* cargo) {
                float tot = 0;
                for (int r = 0; r < R_Count; r++) tot += cargo[r];
                if (tot <= 0) return;
                if (isPlayer(killer) && killer != owner && !sameClan(killer, owner)) {
                    for (int r = 0; r < R_Count; r++) pl[killer].res[r] += cargo[r];
                    say(killer, "You raided a supply party: +" + std::to_string(int(tot)) + " resources!");
                }
                say(owner, "Your convoy was destroyed!");
            };
            if (const Worker* w = reg.try_get<Worker>(e)) raid(w->cargo);
            if (const Convoy* c = reg.try_get<Convoy>(e)) raid(c->cargo);
            if (const Haul* hl = reg.try_get<Haul>(e)) raid(hl->cargo);
            if (k == K_Sheep && isPlayer(killer) && !huntKill(e, killer)) { pl[killer].res[R_Meat] += 12; say(killer, "Hunted a sheep: +12 meat."); }
            worldOnKill(e, k, owner, killer, p);                         // [world] patrol loot + retaliation
            if (isFactionOwner(owner) && isPlayer(killer)) addRep(killer, owner - FACTION_BASE, k == K_TownHall ? -40.f : (isBuilding(k) ? -10.f : -4.f));
            uint16_t town = reg.all_of<TownTag>(e) ? reg.get<TownTag>(e).town : uint16_t(0);
            destroyNow(e);
            if (k == K_TownHall && isFactionOwner(owner)) townDestroyed(town, killer);
            else if (k == K_TownHall && isPlayer(owner)) {
                bool other = false;
                reg.view<Info, Owner>().each([&](const Info& i, const Owner& o) { if (i.kind == K_TownHall && o.id == owner) other = true; });
                if (!other) defeat(owner, killer);
            }
        }
        toKill.clear();
    }
    void removeOwner(int id) {
        std::vector<entt::entity> v;
        reg.view<Owner>().each([&](entt::entity e, const Owner& o) { if (o.id == id) v.push_back(e); });
        for (auto e : v) {
            if (isBuilding(reg.get<Info>(e).kind)) { Pos p = posOf(e); fx(E_Explosion, p.x, p.y); }
            destroyNow(e);
        }
    }
    void defeat(int victim, int killer) {
        if (isPlayer(killer) && killer != victim && pl[killer].used && !sameClan(killer, victim)) {
            float tot = 0;
            for (int r = 0; r < R_Count; r++) { float l = std::floor(pl[victim].res[r] * .5f); pl[killer].res[r] += l; tot += l; }
            say(killer, "You destroyed " + pl[victim].name + "'s Town Hall! Loot: " + std::to_string(int(tot)) + " resources.");
        }
        if (baseDefeat(victim)) return;                                    // clan members become homeless (server_base.inc)
        removeOwner(victim);
        resetProfile(victim);
        spawnBase(victim, bp[victim].friends);                            // respawn near Steam friends / allies
        pl[victim].sentFog.clear();
        pl[victim].sentTerr.clear();
        say(victim, "Your Town Hall fell! You rebuild at a fresh clearing elsewhere in the world.");
        std::printf("%s was defeated\n", pl[victim].name.c_str());
    }
    entt::entity nearestHostile(Pos p, int owner, float range, bool buildings) {
        entt::entity best = entt::null;
        float bd = range;
        reg.view<Owner, Info>().each([&](entt::entity e, const Owner& o, const Info& i) {
            if (!hostile(owner, o.id) || (isBuilding(i.kind) && !buildings) || dying.count(reg.get<Uid>(e).id) || reg.all_of<Aboard>(e)) return;
            float d = rectDist(p, e);
            if (d < bd) { bd = d; best = e; }
        });
        return best;
    }

    // ---------------------------------------------------------------- connections
    void sendFog(int id);
    void sendRoster();
    void sendMap(int id);
    bool load();
    void save();
    void run();

    void onHello(uint64_t conn, Reader& r) {
        std::string uuid = r.str(), name = r.str();
        uint64_t steamId = r.get<uint64_t>();
        std::vector<uint64_t> friends;
        int nf = r.get<uint16_t>();
        for (int i = 0; i < nf && i < 200; i++) friends.push_back(r.get<uint64_t>());
        if (!r.ok || uuid.size() < 8 || uuid.size() > 64) { net.kick(conn, false); return; }
        for (char& c : name) if (c < 32 || c == 127) c = '?';
        if (name.empty()) name = "Player";
        if (name.size() > 20) name.resize(20);
        int id = 0;
        for (int i = 1; i <= MAX_PLAYERS; i++) if (pl[i].used && pl[i].uuid == uuid) id = i;
        if (id && pl[id].online) {
            Writer w; w.put<uint8_t>(S_Text); w.str("This profile is already connected.");
            net.send(conn, w);
            net.kick(conn, true);
            return;
        }
        bool fresh = false;
        if (!id) {
            for (int i = 1; i <= MAX_PLAYERS && !id; i++) if (!pl[i].used) id = i;
            if (!id) {                                           // recycle the longest-offline profile
                uint64_t oldest = UINT64_MAX;
                for (int i = 1; i <= MAX_PLAYERS; i++) if (!pl[i].online && pl[i].lastSeen < oldest) { oldest = pl[i].lastSeen; id = i; }
                if (!id) { net.kick(conn, false); return; }
                leaveClan(id);
                removeOwner(id);
                for (int a = 0; a <= MAX_PLAYERS; a++) pl[a].allied[id] = pl[a].pending[id] = false;
                pl[id] = Profile{};
                bp[id] = BaseProf{};
            }
            fresh = true;
            pl[id].used = true;
            pl[id].uuid = uuid;
            resetProfile(id);
            for (int f = 0; f < FACTION_COUNT; f++)                  // about a quarter of the factions are aggressive towards each newcomer
                if (hash2(int(std::hash<std::string>{}(uuid) & 0x7fffffff), f, seed + 71) % 100 < 25) pl[id].rep[f] = -50.f;
            if (const char* ag = std::getenv("BW_AGGRO")) { int v = std::atoi(ag); for (int f = 0; f < FACTION_COUNT; f++) if (v >= FACTION_COUNT || v == f) pl[id].rep[f] = -50.f; }      // test hook
        }
        Profile& p = pl[id];
        p.name = name;
        p.online = true;
        p.conn = conn;
        p.steamId = steamId;
        bp[id].friends = friends;
        connPlayer[conn] = id;
        p.lastSeen = nowSec();
        p.sentFog.clear();
        p.sentTerr.clear();
        if (fresh) spawnBase(id, friends);
        else {
            bool hasTH = false;
            reg.view<Info, Owner>().each([&](const Info& i, const Owner& o) { if (i.kind == K_TownHall && o.id == id) hasTH = true; });
            if (!hasTH && !bp[id].homeless) { resetProfile(id); spawnBase(id, friends); say(id, "Your base was destroyed while you were away. You start over."); }
        }
        if (const char* rv = std::getenv("BW_REVEAL")) {          // test hook: reveal a large disc around the base
            Pos c = p.cam;
            reveal(id, int(c.x), int(c.y), std::atoi(rv));
        }
        Writer w;
        w.put<uint8_t>(S_Welcome).put<uint8_t>(uint8_t(id)).put<uint32_t>(seed).put<float>(p.cam.x).put<float>(p.cam.y);
        w.str(p.name);
        net.send(conn, w);
        sendFog(id);
        sendClearedFull(id);                                     // [world] cleared surface features
        worldTestSpawn(id, fresh);
        sendRoster();
        say(id, fresh ? "Welcome, " + p.name + "! Press B to open the building menu." : "Welcome back, " + p.name + "! Your base is just as you left it.");
        std::printf("%s (%d) %s\n", p.name.c_str(), id, fresh ? "joined" : "rejoined");
    }
    void onDisconnect(uint64_t conn) {
        auto it = connPlayer.find(conn);
        int id = it == connPlayer.end() ? 0 : it->second;
        connPlayer.erase(conn);
        if (id < 1 || id > MAX_PLAYERS || pl[id].conn != conn) return;
        pl[id].online = false;
        pl[id].conn = 0;
        pl[id].lastSeen = nowSec();
        std::printf("%s left\n", pl[id].name.c_str());
        sendRoster();
    }

    int unitCount(int id) {
        int n = 0;
        reg.view<Owner, Unit>().each([&](const Owner& o, const Unit&) { if (o.id == id) n++; });
        return n;
    }
    int workersOf(uint32_t home) {
        int n = 0;
        reg.view<Worker>().each([&](const Worker& w) { if (w.home == home) n++; });
        return n;
    }

    struct PlaceWorld {
        Server& s;
        int me;
        int biome(int x, int y) { return s.biome(x, y); }
        bool explored(int x, int y) { return s.explored(me, x, y); }
        bool occupied(int x, int y) { return s.occ.count(tkey(x, y)) > 0; }
        template <class F> void anchors(F f) {
            s.reg.view<Info, Building, Owner>().each([&](entt::entity e, const Info& i, const Building& b, const Owner& o) {
                if ((i.kind == K_TownHall || i.kind == K_Outpost) && !s.inactive(e)) f(footCenter(i.kind, b.tx, b.ty), i.kind, o.id == me, s.allied(me, o.id) && o.id != me);
            });
        }
        bool campfireNear(Pos c, float r) {
            bool hit = false;
            s.reg.view<Info, Building>().each([&](const Info& i, const Building& b) {
                if (i.kind != K_Campfire) return;
                Pos q = footCenter(i.kind, b.tx, b.ty);
                if (std::hypot(q.x - c.x, q.y - c.y) < r) hit = true;
            });
            return hit;
        }
        bool inClanRadius(Pos c) { return s.inClanRadiusOf(me, c); }
    };

    bool spawnSpot(entt::entity b, bool water, Pos& out) {
        const auto& bi = reg.get<Building>(b);
        Kind k = reg.get<Info>(b).kind;
        int fw = KINFO[k].fw, fh = KINFO[k].fh;
        for (int r = 1; r <= 4; r++)
            for (int y = bi.ty + fh - 1 + r; y >= bi.ty - r; y--)
                for (int x = bi.tx - r; x <= bi.tx + fw - 1 + r; x++) {
                    if (std::max({bi.tx - x, x - (bi.tx + fw - 1), bi.ty - y, y - (bi.ty + fh - 1)}) != r) continue;
                    if (walkable(x, y, water, 0)) { out = {float(x) + .5f, float(y) + .5f}; return true; }
                }
        return false;
    }

    // ---------------------------------------------------------------- commands
    void onPacket(uint64_t conn, const std::vector<uint8_t>& data) {
        Reader r{data.data(), data.data() + data.size()};
        uint8_t type = r.get<uint8_t>();
        auto cp = connPlayer.find(conn);
        int id = cp == connPlayer.end() ? 0 : cp->second;
        if (type == C_Hello) { if (id < 1) onHello(conn, r); return; }
        if (id < 1 || !r.ok) return;
        Profile& p = pl[id];
        auto readUnits = [&](bool combatOnly) {
            std::vector<entt::entity> us;
            int n = r.get<uint8_t>();
            for (int i = 0; i < n; i++) {
                auto e = find(r.get<uint32_t>());
                if (e == entt::null || !reg.all_of<Unit, Owner, Info>(e) || reg.get<Owner>(e).id != id || reg.all_of<Worker>(e) || reg.all_of<Convoy>(e) || reg.all_of<Haul>(e) || reg.all_of<Raider>(e) || reg.all_of<Aboard>(e)) continue;   // units aboard a ship cannot be commanded
                Kind k = reg.get<Info>(e).kind;
                if (combatOnly && (k == K_Ship || k == K_Monk)) continue;
                releaseUnit(e);                                      // a manual command ends patrols / firefighting
                if (k == K_Pawn) huntPause(e);                          // [world] a commanded pawn stops hunting for a while
                reg.remove<Boarding>(e);                         // any new order cancels embarking
                if (k == K_Ship) cancelVoyage(e);                // ... and takes a ship off a supply run
                us.push_back(e);
            }
            return us;
        };
        switch (type) {
        case C_Cam: { float x = r.get<float>(), y = r.get<float>(); if (r.ok && std::isfinite(x) && std::isfinite(y)) p.cam = {x, y}; break; }
        case C_Build: {
            auto k = Kind(r.get<uint8_t>());
            int tx = r.get<int32_t>(), ty = r.get<int32_t>();
            if (!r.ok || k >= K_Count || !isBuilding(k)) return;
            if (k == K_TownHall) { homelessBuild(id, tx, ty); return; }
            if (k != K_Campfire && townMoveActive(id)) { say(id, "Wait until the town move is finished before building."); return; }
            PlaceWorld w{*this, id};
            int err = checkPlace(w, k, tx, ty);
            if (err) { say(id, std::string("Cannot build here: ") + PLACE_TEXT[err] + "."); return; }
            if (!worldBuildOk(id, k, tx, ty)) return;                    // [world] surface features must be cleared first
            if (!afford(id, KINFO[k].cost)) { say(id, "Not enough resources."); return; }
            spend(id, KINFO[k].cost);
            auto e = makeBuilding(k, uint8_t(id), tx, ty, false);
            Pos c = posOf(e);
            fx(E_Dust, c.x, c.y);
            break;
        }
        case C_Train: {
            auto be = find(r.get<uint32_t>());
            auto k = Kind(r.get<uint8_t>());
            if (!r.ok || be == entt::null || k >= K_Count || isBuilding(k) || !reg.all_of<Building>(be)) return;
            enqueueTrain(id, be, k);                                // queued production (server_base.inc)
            break;
        }
        case C_ClanCreate: {
            std::string nm = r.str();
            for (char& ch : nm) if (ch < 32 || ch == 127) ch = '?';
            if (!r.ok || nm.size() < 3 || nm.size() > 16) { say(id, "Clan names need 3 to 16 characters."); return; }
            if (p.clan) { say(id, "Leave your current clan first."); return; }
            int slot = 0;
            for (int c = 1; c <= MAX_CLANS; c++) {
                if (clans[c].used && clans[c].name == nm) { say(id, "That clan name is taken."); return; }
                if (!clans[c].used && !slot) slot = c;
            }
            if (!slot) { say(id, "The server has no free clan slots."); return; }
            clans[slot] = Clan{true, nm, uint8_t(id)};
            p.clan = uint8_t(slot);
            say(id, "Clan '" + nm + "' founded. Friends can join it from the Clans tab.");
            sendRoster();
            break;
        }
        case C_ClanJoin: {
            int c = r.get<uint8_t>();
            if (!r.ok || c < 1 || c > MAX_CLANS || !clans[c].used) return;
            if (p.clan) { say(id, "Leave your current clan first."); return; }
            p.clan = uint8_t(c);
            notifyClan(c, p.name + " joined the clan.");
            sendRoster();
            break;
        }
        case C_ClanLeave: leaveClan(id); break;
        case C_ClanLeader: {
            int to = r.get<uint8_t>();
            int c = p.clan;
            if (!r.ok || !c || clans[c].leader != id || !isPlayer(to) || to == id || pl[to].clan != c) { say(id, "Only the clan leader can hand over leadership to a clan member."); return; }
            clans[c].leader = uint8_t(to);
            notifyClan(c, pl[to].name + " is now the clan leader. Their Town Hall is the clan hall.");
            sendRoster();
            break;
        }
        case C_SetHaul: {
            auto be = find(r.get<uint32_t>());
            int pct = r.get<uint8_t>();
            if (!r.ok || be == entt::null || !reg.all_of<Building>(be) || reg.get<Owner>(be).id != id) return;
            Kind k = reg.get<Info>(be).kind;
            if (k != K_Outpost && k != K_Storage) return;
            reg.get<Building>(be).haulPct = uint8_t(std::clamp(pct, 10, 100));
            break;
        }
        case C_UpgradeUnit: {
            auto k = Kind(r.get<uint8_t>());
            if (!r.ok || !isTroop(k)) return;
            int cur = std::max<int>(1, p.ulevel[k]), to = cur + 1;
            if (to > MAX_UNIT_LEVEL) { say(id, "Already at the maximum troop level."); return; }
            int archery = 0;
            reg.view<Info, Building, Owner>().each([&](const Info& i, const Building& b, const Owner& o) { if (i.kind == K_Archery && o.id == id && b.progress >= 1.f) archery = std::max<int>(archery, i.level); });
            if (archery < troopUpgradeNeedsArchery(to)) { say(id, "Upgrade your Archery to level " + std::to_string(troopUpgradeNeedsArchery(to)) + " first."); return; }
            float cost[R_Count];
            for (int i = 0; i < R_Count; i++) cost[i] = std::ceil(KINFO[k].cost[i] * 2.5f * float(cur) + (i == R_Gold ? 30.f * float(cur) : 0.f));
            if (!afford(id, cost)) { say(id, "Not enough resources."); return; }
            spend(id, cost);
            float oldMul = unitHpMul(cur);
            p.ulevel[k] = uint8_t(to);
            float ratio = unitHpMul(to) / oldMul;
            reg.view<Info, Owner, Health>().each([&](const Info& i, const Owner& o, Health& h) { if (i.kind == k && o.id == id) { h.maxhp *= ratio; h.hp *= ratio; } });
            say(id, std::string(KINFO[k].name) + "s upgraded to level " + std::to_string(to) + ".");
            break;
        }
        case C_Chat: {
            int ch = r.get<uint8_t>();
            std::string text = r.str();
            for (char& c : text) if (c < 32 || c == 127) c = ' ';
            if (!r.ok || ch > 2 || text.empty()) return;
            if (text.size() > 160) text.resize(160);
            if (ch == 1 && !p.clan) { say(id, "You are not in a clan."); return; }
            Writer w;
            w.put<uint8_t>(S_Chat).put<uint8_t>(uint8_t(ch)).put<uint8_t>(uint8_t(id));
            w.str(p.name);
            w.str(text);
            for (int i = 1; i <= MAX_PLAYERS; i++) {
                if (!pl[i].online) continue;
                bool to = ch == 0 || i == id || (ch == 1 && pl[i].clan == p.clan) || (ch == 2 && pl[id].allied[i]);
                if (to) net.send(pl[i].conn, w);
            }
            break;
        }
        case C_Transfer: {
            int to = r.get<uint8_t>();
            float amt[R_Count];
            float tot = 0;
            for (float& a : amt) { a = r.get<float>(); if (!std::isfinite(a) || a < 0 || a > 500) return; tot += a; }
            if (!r.ok || tot <= 0 || !isPlayer(to) || to == id || !pl[to].used || !sameClan(id, to)) { say(id, "You can only send resources to members of your clan."); return; }
            for (int i = 0; i < R_Count; i++) if (p.res[i] + 1e-3f < amt[i]) { say(id, "Not enough resources."); return; }
            entt::entity src = entt::null;
            reg.view<Info, Building, Owner>().each([&](entt::entity b, const Info& i, const Building& bb, const Owner& o) { if (src == entt::null && i.kind == K_TownHall && o.id == id && bb.progress >= 1.f) src = b; });
            Pos at;
            if (src == entt::null || !spawnSpot(src, false, at)) { say(id, "No room at your Town Hall to send a convoy."); return; }
            for (int i = 0; i < R_Count; i++) p.res[i] -= amt[i];
            auto u = makeUnit(K_Pawn, uint8_t(id), at.x, at.y);
            Convoy cv{uint8_t(id), uint8_t(to), {amt[0], amt[1], amt[2], amt[3]}};
            reg.emplace<Convoy>(u, cv);
            fx(E_Dust, at.x, at.y);
            say(id, "A convoy with " + std::to_string(int(tot)) + " resources set out for " + pl[to].name + ". Other players can see and raid it!");
            say(to, "A convoy from " + p.name + " is on its way.");
            break;
        }
        case C_Donate: {
            int f = r.get<uint8_t>(), res = r.get<uint8_t>();
            float amount = r.get<float>();
            if (!r.ok || f < 0 || f >= FACTION_COUNT || res >= R_Count || !std::isfinite(amount) || amount <= 0 || amount > 500) return;
            if (p.res[res] + 1e-3f < amount) { say(id, "Not enough resources."); return; }
            static const float worth[R_Count] = {1.f, 2.f, 1.5f, 1.f};
            p.res[res] -= amount;
            facDonated[f] += amount * worth[res];
            addRep(id, f, amount * worth[res] * .08f);
            say(id, std::string("You gave ") + std::to_string(int(amount)) + " " + RES_NAME[res] + " to the " + FACTION_NAME[f] + ". They now see you as " + repTier(int(std::lround(p.rep[f]))) + ".");
            break;
        }
        case C_Upgrade: {
            auto be = find(r.get<uint32_t>());
            if (!r.ok || be == entt::null || !reg.all_of<Building>(be) || reg.get<Owner>(be).id != id || !complete(be)) return;
            auto& in = reg.get<Info>(be);
            if (inactive(be)) { say(id, "This building is not functional."); return; }
            if (in.level >= MAX_LEVEL) { say(id, "Already at the maximum level."); return; }
            float cost[R_Count];
            for (int i = 0; i < R_Count; i++) cost[i] = std::ceil(KINFO[in.kind].cost[i] * upgradeCostMul(in.level) * .7f);
            if (in.kind == K_Campfire || !afford(id, cost)) { say(id, in.kind == K_Campfire ? "Campfires cannot be upgraded." : "Not enough resources."); return; }
            spend(id, cost);
            auto& h = reg.get<Health>(be);
            float old = h.maxhp;
            in.level++;
            h.maxhp = KINFO[in.kind].hp * levelHp(in.level);
            h.hp += h.maxhp - old;
            Pos c = posOf(be);
            fx(E_Dust, c.x, c.y);
            say(id, std::string(KINFO[in.kind].name) + " upgraded to level " + std::to_string(in.level) + ".");
            break;
        }
        case C_Move: {
            auto us = readUnits(false);
            float tx = r.get<float>(), ty = r.get<float>();
            if (!r.ok || !std::isfinite(tx) || !std::isfinite(ty)) return;
            int fail = 0, n = 0;
            for (auto e : us) {
                reg.remove<Attack>(e);
                reg.remove<AMove>(e);
                float a = float(n++) * 2.4f, rad = n > 1 ? .35f * std::sqrt(float(n)) : 0.f;
                if (!order(e, {tx + std::cos(a) * rad, ty + std::sin(a) * rad})) { fail++; reg.get<Unit>(e).path.clear(); }
            }
            if (fail) say(id, "Some units cannot reach that spot.");
            break;
        }
        case C_AttackMove: {
            auto us = readUnits(true);
            float tx = r.get<float>(), ty = r.get<float>();
            if (!r.ok || !std::isfinite(tx) || !std::isfinite(ty)) return;
            for (auto e : us) {
                reg.remove<Attack>(e);
                if (order(e, {tx, ty})) reg.emplace_or_replace<AMove>(e, AMove{{tx, ty}});
            }
            break;
        }
        case C_Attack: {
            auto us = readUnits(true);
            auto tg = find(r.get<uint32_t>());
            if (!r.ok || us.empty() || tg == entt::null) return;
            int to = reg.get<Owner>(tg).id;
            if (to == id) return;
            if (isPlayer(to) && sameClan(id, to)) { say(id, "You cannot attack your own clan. Leave the clan first."); return; }
            if (isPlayer(to) && allied(id, to)) { breakAlliance(id, to); }
            Pos tp = posOf(tg);
            uint32_t tuid = reg.get<Uid>(tg).id;
            bool bld = isBuilding(reg.get<Info>(tg).kind);
            int fail = 0;
            for (auto e : us) {
                reg.remove<AMove>(e);
                if (!order(e, tp, bld ? tuid : 0)) { fail++; continue; }
                reg.emplace_or_replace<Attack>(e, Attack{tuid});
            }
            if (fail) say(id, "Some units cannot reach the target.");
            if (isPlayer(to)) say(to, p.name + " is attacking you!");
            break;
        }
        case C_Ally: {
            int to = r.get<uint8_t>(), act = r.get<uint8_t>();
            if (!r.ok || to == id || to < 1 || to > MAX_PLAYERS || !pl[to].used) return;
            if (act == AA_Break) { if (p.allied[to]) breakAlliance(id, to); return; }
            if (p.allied[to]) { say(id, "Already allied."); return; }
            if (pl[to].pending[id]) {
                p.allied[to] = pl[to].allied[id] = true;
                p.pending[to] = pl[to].pending[id] = false;
                say(id, "You are now allied with " + pl[to].name + ": shared vision and economy.");
                say(to, p.name + " accepted your alliance.");
            } else {
                p.pending[to] = true;
                say(id, "Alliance invite sent to " + pl[to].name + ".");
                say(to, p.name + " invites you to an alliance. Right-click one of their buildings to accept.");
            }
            sendRoster();
            break;
        }
        case C_Clear: onClear(id, r); break;                            // [world]
        default: shipPacket(id, type, r); baseCommand(id, type, r); break;   // [ships] / [base] command sets (each ignores the other's types)
        }
    }
    void breakAlliance(int a, int b) {
        pl[a].allied[b] = pl[b].allied[a] = false;
        pl[a].pending[b] = pl[b].pending[a] = false;
        say(a, "Alliance with " + pl[b].name + " ended.");
        say(b, pl[a].name + " ended the alliance!");
        sendRoster();
    }

    // ---------------------------------------------------------------- simulation
    void spawnChunkNpcs(int cx, int cy) {
        if (!spawned.insert(chunkKey(cx, cy)).second) return;
        int b = biome(cx * CH + 8, cy * CH + 8);
        if (b != B_Plains && b != B_Flowers) return;
        int n = int(hash2(cx, cy, seed + 901) % 4);
        for (int i = 0; i < n; i++) {
            int x = cx * CH + int(hash2(cx, cy * 7 + i, seed + 5) % CH), y = cy * CH + int(hash2(cx * 5 + i, cy, seed + 6) % CH);
            if (walkable(x, y, false, 0) && (b == biome(x, y))) makeUnit(K_Sheep, 0, float(x) + .5f, float(y) + .5f);
        }
    }

    // ---------------------------------------------------------------- NPC factions (villages & towns)
    bool townSiteOk(int cx, int cy) {
        for (int y = cy - 7; y <= cy + 7; y++)
            for (int x = cx - 7; x <= cx + 7; x++) {
                int b = biome(x, y);
                if (!isLand(b) || b == B_Highlands || isCliffTile(b, biome(x, y - 1)) || occ.count(tkey(x, y))) return false;
            }
        bool nearTh = false;
        reg.view<Info, Building>().each([&](const Info& i, const Building& b) {
            if (i.kind == K_TownHall && std::max(std::abs(b.tx - cx), std::abs(b.ty - cy)) < 45) nearTh = true;
        });
        return !nearTh;
    }
    bool footprintFree(Kind k, int tx, int ty) {
        for (int y = ty; y < ty + KINFO[k].fh; y++) for (int x = tx; x < tx + KINFO[k].fw; x++) if (!walkable(x, y, false, 0)) return false;
        return true;
    }
    void buildTown(Town& t) {
        uint8_t owner = uint8_t(FACTION_BASE + t.faction);
        struct Piece { Kind k; int dx, dy; };
        static const Piece pieces[] = {{K_TownHall, -2, -1}, {K_Storage, 3, -1}, {K_Outpost, -5, -3}, {K_Outpost, 3, 3}, {K_Lumber, -5, 1}, {K_Lumber, 0, 3}};
        for (const Piece& pc : pieces) {
            if (!footprintFree(pc.k, t.cx + pc.dx, t.cy + pc.dy)) continue;
            auto e = makeBuilding(pc.k, owner, t.cx + pc.dx, t.cy + pc.dy, true);
            reg.emplace<TownTag>(e, TownTag{t.id});
            if (pc.k == K_TownHall) t.th = reg.get<Uid>(e).id;
        }
        static const Kind roster[] = {K_Warrior, K_Warrior, K_Warrior, K_Archer, K_Archer, K_Pawn, K_Pawn};
        int i = 0;
        for (Kind k : roster) spawnTownUnit(t, k, i++);
        t.alive = true;
        t.nextSpawn = clock + 25;
    }
    void spawnTownUnit(const Town& t, Kind k, int i) {
        float a = float(i) * 0.9f + float(t.id);
        float x = float(t.cx) + .5f + std::cos(a) * 3.2f, y = float(t.cy) + .5f + 2.4f + std::sin(a) * 1.4f;
        if (!walkable(int(std::floor(x)), int(std::floor(y)), false, 0)) { x = float(t.cx) + .5f; y = float(t.cy) + 3.5f; }
        auto u = makeUnit(k, uint8_t(FACTION_BASE + t.faction), x, y);
        reg.emplace<TownTag>(u, TownTag{t.id});
        reg.emplace<Home>(u, Home{float(t.cx) + .5f, float(t.cy) + 3.f, 0});
        reg.emplace<Wander>(u, Wander{float(rng() % 400) / 100.f});
    }
    void processRegion(int rx, int ry) {
        if (!regionsDone.insert(chunkKey(rx, ry)).second) return;
        if (hash2(rx, ry, seed + 1111) % 100 >= 85) return;
        int span = REGION_CHUNKS * CH, x0 = rx * span, y0 = ry * span;
        for (int n = 0; n < 40; n++) {
            int x = x0 + 14 + int(hash2(rx * 40 + n, ry, seed + 21) % uint32_t(span - 28)), y = y0 + 14 + int(hash2(rx, ry * 40 + n, seed + 22) % uint32_t(span - 28));
            if (!townSiteOk(x, y)) continue;
            Town t{uint16_t(towns.size() + 1), uint8_t(hash2(x, y, seed + 33) % FACTION_COUNT), x, y, 0, true, 0, 0, -100};
            towns.push_back(t);
            buildTown(towns.back());
            std::printf("Town %d (%s) founded at %d,%d\n", t.id, FACTION_NAME[t.faction], x, y);
            return;
        }
    }
    void townDestroyed(uint16_t town, int killer) {
        Town* t = townById(town);
        if (!t || !t->alive) return;
        t->alive = false;
        t->respawnAt = clock + 600;
        std::vector<entt::entity> all;
        reg.view<TownTag>().each([&](entt::entity e, const TownTag& g) { if (g.town == town) all.push_back(e); });
        for (auto e : all) {
            if (isBuilding(reg.get<Info>(e).kind)) { Pos p = posOf(e); fx(E_Explosion, p.x, p.y); }
            destroyNow(e);
        }
        if (isPlayer(killer)) {
            for (int r = 0; r < R_Count; r++) pl[killer].res[r] += 120;
            say(killer, std::string("You razed a ") + FACTION_NAME[t->faction] + " town! Loot: 120 of every resource.");
        }
    }
    void townTick() {
        for (auto& t : towns) {
            if (!t.alive) { if (clock >= t.respawnAt) buildTown(t); continue; }
            int army = 0;
            reg.view<TownTag, Info>().each([&](entt::entity pe, const TownTag& g, const Info& i) { if (g.town == t.id && isArmy(i.kind) && !isPatrol(pe)) army++; });
            int desired = std::min(9, 5 + int(facDonated[t.faction] / 300.f));
            if (army < desired && clock >= t.nextSpawn) { spawnTownUnit(t, hash2(t.id, int(clock), seed) & 1 ? K_Warrior : K_Archer, army); t.nextSpawn = clock + 25; }
        }
        std::vector<entt::entity> hurt;                                 // faction buildings and units mend themselves when left alone
        reg.view<Owner, Health>().each([&](entt::entity e, const Owner& o, const Health& h) { if (isFactionOwner(o.id) && h.hp < h.maxhp) hurt.push_back(e); });
        for (auto e : hurt) {
            auto* hit = reg.try_get<Hit>(e);
            if (hit && clock - hit->t < 20) continue;
            auto& h = reg.get<Health>(e);
            h.hp = std::min(h.maxhp, h.hp + h.maxhp * .02f);
        }
    }
    void factionAI(entt::entity e, Unit& u, Pos& p, float) {
        auto* hm = reg.try_get<Home>(e);
        if (!hm) return;
        bool busy = reg.all_of<Attack>(e) || reg.all_of<AMove>(e);
        float dh = std::hypot(p.x - hm->x, p.y - hm->y);
        if (clock < hm->helpUntil) return;
        if (busy && dh > 24.f) { reg.remove<Attack>(e); reg.remove<AMove>(e); order(e, {hm->x, hm->y}); }
        else if (!busy && u.path.empty() && dh > 5.f) order(e, {hm->x, hm->y});
    }
    void callHelp() {                                                   // friendly factions send troops when a player is attacked
        struct Req { int victim; uint32_t by; Pos at; };
        std::vector<Req> reqs;
        reg.view<Hit, Owner>().each([&](entt::entity e, const Hit& h, const Owner& o) {
            if (!isPlayer(o.id) || clock - h.t > 3.0 || reqs.size() >= 6) return;
            reqs.push_back({o.id, h.by, posOf(e)});
        });
        for (auto& rq : reqs) {
            auto atk = find(rq.by);
            if (atk == entt::null) continue;
            int aow = reg.get<Owner>(atk).id;
            if (!hostile(rq.victim, aow)) continue;
            bool bld = isBuilding(reg.get<Info>(atk).kind);
            Pos ap = posOf(atk);
            for (int f = 0; f < FACTION_COUNT; f++) {
                if (pl[rq.victim].rep[f] < float(FRIENDLY_REP) || aow == FACTION_BASE + f || clock < pl[rq.victim].helpCool[f]) continue;
                std::vector<std::pair<float, entt::entity>> cand;
                reg.view<Owner, Unit, Info, Home>().each([&](entt::entity e, const Owner& o, const Unit&, const Info& i, const Home&) {
                    if (o.id != FACTION_BASE + f || !isArmy(i.kind) || reg.all_of<Attack>(e)) return;
                    Pos q = reg.get<Pos>(e);
                    cand.push_back({std::hypot(q.x - rq.at.x, q.y - rq.at.y), e});
                });
                std::sort(cand.begin(), cand.end(), [](auto& a, auto& b) { return a.first < b.first; });
                int want = std::min(8, 2 + int(pl[rq.victim].rep[f] / 15.f)), sent = 0;
                for (auto& [d, e] : cand) {
                    if (sent >= want || d > 160.f) break;
                    if (!order(e, ap, bld ? rq.by : 0)) continue;
                    reg.emplace_or_replace<Attack>(e, Attack{rq.by});
                    reg.get<Home>(e).helpUntil = float(clock + 45);
                    sent++;
                }
                pl[rq.victim].helpCool[f] = clock + 15;
                if (sent) say(rq.victim, std::string(FACTION_NAME[f]) + " sends " + std::to_string(sent) + " troops to your aid!");
            }
        }
    }

    // ---------------------------------------------------------------- clans, convoys
    void notifyClan(int clan, const std::string& msg) { for (int i = 1; i <= MAX_PLAYERS; i++) if (pl[i].used && pl[i].clan == clan) say(i, msg); }
    void leaveClan(int id) {
        int c = pl[id].clan;
        if (!c) return;
        pl[id].clan = 0;
        if (bp[id].homeless) respawnNormally(id);                         // a homeless member without a clan restarts normally
        notifyClan(c, pl[id].name + " left the clan.");
        say(id, "You left the clan.");
        int next = 0;
        for (int i = 1; i <= MAX_PLAYERS; i++) if (pl[i].used && pl[i].clan == c) { next = i; break; }
        if (!next) clans[c] = Clan{};
        else if (clans[c].leader == id) { clans[c].leader = uint8_t(next); say(next, "You are now the clan leader."); }
        sendRoster();
    }
    void refundConvoy(entt::entity e, const Convoy& cv, const std::string& why) {
        for (int r = 0; r < R_Count; r++) if (isPlayer(cv.from)) pl[cv.from].res[r] += cv.cargo[r];
        say(cv.from, "Convoy turned back: " + why + ". Resources returned.");
        destroyNow(e);
    }
    void runConvoy(entt::entity e, Unit& u, Convoy& cv, float dt) {
        Pos p = reg.get<Pos>(e);
        entt::entity th = entt::null;
        float bd = 1e9f;
        reg.view<Info, Building, Owner>().each([&](entt::entity b, const Info& i, const Building& bb, const Owner& o) {
            if (i.kind != K_TownHall || o.id != cv.to || bb.progress < 1.f) return;
            Pos c = footCenter(i.kind, bb.tx, bb.ty);
            float d = std::hypot(c.x - p.x, c.y - p.y);
            if (d < bd) { bd = d; th = b; }
        });
        if (th == entt::null || !isPlayer(cv.to) || !pl[cv.to].used || !sameClan(cv.from, cv.to)) { refundConvoy(e, cv, "the recipient is no longer reachable"); return; }
        if (rectDist(p, th) <= 1.6f) {
            float tot = 0;
            for (int r = 0; r < R_Count; r++) { pl[cv.to].res[r] += cv.cargo[r]; tot += cv.cargo[r]; }
            say(cv.to, "A convoy from " + ownerName(cv.from) + " arrived with " + std::to_string(int(tot)) + " resources.");
            say(cv.from, "Your convoy reached " + ownerName(cv.to) + ".");
            destroyNow(e);
            return;
        }
        cv.repath -= dt;
        if (u.path.empty() && cv.repath <= 0) {
            cv.repath = 3.f;
            float d = rectDist(p, th);
            if (d < cv.bestDist - 2.f) { cv.bestDist = d; cv.stuck = 0; }
            else if (++cv.stuck >= 8) { refundConvoy(e, cv, "no land route"); return; }
            if (!order(e, posOf(th), reg.get<Uid>(th).id, 40000)) cv.stuck += 2;
        }
    }
    void updateSpotting() {                                             // convoys seen by a clan member are shown to the whole clan
        std::vector<entt::entity> cvs;
        reg.view<Convoy>().each([&](entt::entity e, const Convoy&) { cvs.push_back(e); });
        reg.view<Haul>().each([&](entt::entity e, const Haul&) { cvs.push_back(e); });
        for (auto& [c, m] : clanSpot) std::erase_if(m, [&](auto& kv) { return kv.second.until < clock; });
        if (cvs.empty()) return;
        std::vector<Source> srcs[MAX_PLAYERS + 1];
        for (int pid = 1; pid <= MAX_PLAYERS; pid++) if (pl[pid].used && pl[pid].clan) srcs[pid] = sourcesFor(pid);
        for (auto e : cvs) {
            int owner = reg.get<Owner>(e).id;
            Pos p = reg.get<Pos>(e);
            uint32_t uid = reg.get<Uid>(e).id;
            for (int pid = 1; pid <= MAX_PLAYERS; pid++) {
                if (!pl[pid].used || !pl[pid].clan || allied(pid, owner) || !seen(srcs[pid], p.x, p.y)) continue;
                auto& m = clanSpot[pl[pid].clan];
                bool fresh = m.find(uid) == m.end();
                m[uid] = {clock + 6.0, uint8_t(pid)};
                if (fresh) notifyClan(pl[pid].clan, "Enemy convoy of " + ownerName(owner) + " spotted by " + pl[pid].name + " at (" + std::to_string(int(p.x)) + ", " + std::to_string(int(p.y)) + ")!");
            }
        }
    }
    // ---------------------------------------------------------------- hauling troops (outpost / storage house -> Town Hall)
    entt::entity townHallOf(int id) {
        entt::entity th = entt::null;
        reg.view<Info, Building, Owner>().each([&](entt::entity b, const Info& i, const Building& bb, const Owner& o) { if (th == entt::null && i.kind == K_TownHall && o.id == id && bb.progress >= 1.f) th = b; });
        return th;
    }
    void spawnHauls() {
        std::unordered_set<uint32_t> busy;
        reg.view<Haul>().each([&](const Haul& h) { busy.insert(h.storage); });
        std::vector<entt::entity> blds;
        reg.view<Info, Building, Owner>().each([&](entt::entity e, const Info& i, const Building& b, const Owner& o) { if ((i.kind == K_Outpost || i.kind == K_Storage) && b.progress >= 1.f && isPlayer(o.id) && !inactive(e)) blds.push_back(e); });
        for (auto e : blds) {
            uint32_t uid = reg.get<Uid>(e).id;
            if (busy.count(uid)) continue;
            auto& b = reg.get<Building>(e);
            int ow = reg.get<Owner>(e).id;
            float stock = stockOf(e), cap = capOf(e);
            if (stock < 20.f || stock / cap * 100.f < float(b.haulPct)) continue;
            auto th = townHallOf(ow);
            Pos at;
            if (th == entt::null || !spawnSpot(th, false, at)) continue;
            auto u = makeUnit(K_Pawn, uint8_t(ow), at.x, at.y);
            Haul h;
            h.storage = uid;
            reg.emplace<Haul>(u, h);
            say(ow, std::string("A hauling party leaves the Town Hall for your ") + KINFO[reg.get<Info>(e).kind].name + ".");
        }
    }
    void runHaul(entt::entity e, Unit& u, Haul& h, float dt) {
        if (auto* bd = reg.try_get<Boarding>(e)) { runBoarding(e, u, *bd, dt); return; }   // walking to a ferry
        Pos p = reg.get<Pos>(e);
        int ow = reg.get<Owner>(e).id;
        auto st = find(h.storage);
        auto th = townHallOf(ow);
        auto giveUp = [&]() {                                           // return whatever we carry to the storage house if we can
            if (st != entt::null) storeCargo(st, h.cargo);
            destroyNow(e);
        };
        if (th == entt::null) { giveUp(); return; }
        entt::entity target = h.phase == 0 ? st : th;
        if (target == entt::null) { destroyNow(e); return; }
        float d = rectDist(p, target);
        if (d <= 1.6f) {
            u.path.clear();
            h.stuck = 0;
            h.bestDist = 1e9f;
            if (h.phase == 0) {
                auto& sb = reg.get<Building>(st);
                float tot = 0;
                for (float v : sb.stock) tot += v;
                float f = tot > 0 ? std::min(1.f, HAULER_CARRY / tot) : 0.f;
                for (int r = 0; r < R_Count; r++) { h.cargo[r] = sb.stock[r] * f; sb.stock[r] -= h.cargo[r]; }
                h.phase = 1;
            } else {
                storeCargo(th, h.cargo);
                giveUp();
            }
            return;
        }
        h.repath -= dt;
        if (u.path.empty() && h.repath <= 0) {
            h.repath = 3.f;
            if (h.boatPhase != h.phase) {                               // long trip: would one of our ships be clearly faster?
                int ferry = tryFerryHaul(e, h, target);
                if (ferry == 2) { h.repath = .6f; return; }
                if (ferry == 1) return;
            }
            if (d < h.bestDist - 2.f) { h.bestDist = d; h.stuck = 0; }
            else if (++h.stuck >= 8) { giveUp(); return; }
            if (!order(e, posOf(target), reg.get<Uid>(target).id, 40000)) h.stuck += 2;
        }
    }

    // ---------------------------------------------------------------- faction raids
    void spawnRaid(int player, int f) {
        auto th = townHallOf(player);
        if (th == entt::null) return;
        Pos tp = posOf(th);
        const Town* best = nullptr;
        float bd = 400.f;
        for (auto& t : towns) {
            if (!t.alive || t.faction != f) continue;
            float d = std::hypot(float(t.cx) - tp.x, float(t.cy) - tp.y);
            if (d < bd) { bd = d; best = &t; }
        }
        if (!best) return;
        int n = 3 + std::min(6, int(clock / 900.0)) + int(rng() % 2);
        int sent = 0;
        static const Kind mix[] = {K_Warrior, K_Archer, K_Lancer, K_Warrior, K_Archer};
        for (int i = 0; i < n; i++) {
            Kind k = mix[i % 5];
            float a = float(i) * 0.9f;
            float x = float(best->cx) + .5f + std::cos(a) * 3.2f, y = float(best->cy) + 3.5f + std::sin(a) * 1.2f;
            if (!walkable(int(std::floor(x)), int(std::floor(y)), false, 0)) { x = float(best->cx) + .5f; y = float(best->cy) + 3.5f; }
            auto u = makeUnit(k, uint8_t(FACTION_BASE + f), x, y);
            reg.emplace<Raider>(u, Raider{uint8_t(player), clock + 600.0, 0});
            if (order(u, tp, 0, 40000)) { reg.emplace_or_replace<AMove>(u, AMove{tp}); sent++; }
            else destroyNow(u);
        }
        if (sent) say(player, std::string("A raiding party of the ") + FACTION_NAME[f] + " marches on your base from (" + std::to_string(best->cx) + ", " + std::to_string(best->cy) + ")!");
    }
    void runRaider(entt::entity e, Unit& u, Raider& rd, float dt) {
        if (clock > rd.until || !isPlayer(rd.target) || townHallOf(rd.target) == entt::null) { destroyNow(e); return; }
        Pos p = reg.get<Pos>(e);
        auto th = townHallOf(rd.target);
        Pos tp = posOf(th);
        rd.repath -= dt;
        if (!reg.all_of<Attack>(e) && u.path.empty() && rd.repath <= 0 && std::hypot(p.x - tp.x, p.y - tp.y) > 3.f) {
            rd.repath = 4.f;
            order(e, tp, 0, 40000);
            reg.emplace_or_replace<AMove>(e, AMove{tp});
        }
    }
    void raidTick() {
        for (int id = 1; id <= MAX_PLAYERS; id++) {
            if (!pl[id].used || townHallOf(id) == entt::null) continue;
            for (int f = 0; f < FACTION_COUNT; f++) {
                if (pl[id].rep[f] > float(HOSTILE_REP)) { pl[id].raidNext[f] = 0; continue; }       // peace: no raids
                static const bool fastRaids = std::getenv("BW_RAIDNOW") != nullptr;      // test hook
                if (pl[id].raidNext[f] <= 0) { pl[id].raidNext[f] = clock + (fastRaids ? 6.0 : 180.0 + double(rng() % 120)); continue; }
                if (clock >= pl[id].raidNext[f]) { spawnRaid(id, f); pl[id].raidNext[f] = clock + 360.0 + double(rng() % 420); }
            }
        }
    }

    bool spottedForClan(int id, uint32_t uid) const {
        if (!isPlayer(id) || !pl[id].clan) return false;
        auto it = clanSpot.find(pl[id].clan);
        if (it == clanSpot.end()) return false;
        auto f = it->second.find(uid);
        return f != it->second.end() && f->second.until >= clock;
    }

    void update(float dt) {
        tick++;
        clock += dt;
        std::vector<entt::entity> units, blds;
        reg.view<Info>().each([&](entt::entity e, const Info& i) { (isBuilding(i.kind) ? blds : units).push_back(e); });
        std::unordered_map<uint32_t, int> workerCount;
        reg.view<Worker>().each([&](const Worker& w) { workerCount[w.home]++; });

        // construction, production, towers
        for (auto e : blds) {
            auto& b = reg.get<Building>(e);
            auto& in = reg.get<Info>(e);
            int ow = reg.get<Owner>(e).id;
            Pos c = footCenter(in.kind, b.tx, b.ty);
            if (b.progress < 1.f) {
                b.progress = std::min(1.f, b.progress + dt / buildTimeFor(e, in.kind));
                if (b.progress >= 1.f) {
                    reveal(ow, int(std::floor(c.x)), int(std::floor(c.y)), KINFO[in.kind].vision);
                    fx(E_Dust, c.x, c.y);
                    say(ow, std::string(KINFO[in.kind].name) + " completed.");
                }
                continue;
            }
            if (!buildingTick(e, dt)) continue;                         // non-functional buildings do nothing (server_base.inc)
            if (isResourceBuilding(in.kind) && isPlayer(ow)) {
                Yield y = yieldOf(in.kind, biome(int(std::floor(c.x)), int(std::floor(c.y))), in.kind == K_Harbour);
                float f = levelMul(in.level) * mineMul(e, in.kind, c) * (pl[ow].online ? 1.f : OFFLINE_PROD) * dt, tot = 0;   // [world] gold deposits boost mines
                for (int r = 0; r < R_Count; r++) tot += b.stock[r];
                if (tot < 150) for (int r = 0; r < R_Count; r++) b.stock[r] += y.r[r] * f;
                int cnt = workerCount[reg.get<Uid>(e).id];
                if (cnt < 1 || (cnt < 3 && tot > 60)) {
                    b.respawn -= dt;
                    if (b.respawn <= 0 && unitCount(ow) < UNIT_CAP) {
                        Pos at;
                        if (spawnSpot(e, false, at)) {
                            auto u = makeUnit(K_Pawn, uint8_t(ow), at.x, at.y);
                            reg.emplace<Worker>(u, Worker{reg.get<Uid>(e).id});
                            workerCount[reg.get<Uid>(e).id]++;
                        }
                        b.respawn = cnt < 1 ? 6.f : 25.f;
                    }
                }
            }
            if (ow > 0 && KINFO[in.kind].dps > 0) {                  // defensive fire (Town Hall, Outpost)
                float range = KINFO[in.kind].range;
                entt::entity best = nearestHostile(c, ow, range, false);
                b.cd -= dt;
                if (best != entt::null) {
                    damage(best, KINFO[in.kind].dps * levelMul(in.level) * dt, ow, reg.get<Uid>(e).id);
                    if (b.cd <= 0) { b.cd = .9f; Pos t = reg.get<Pos>(best); fx(E_Arrow, c.x, c.y - 1.2f, t.x, t.y); }
                }
            }
        }

        // units
        for (auto e : units) {
            if (!reg.valid(e) || dying.count(reg.get<Uid>(e).id) || reg.all_of<Aboard>(e)) continue;
            auto& u = reg.get<Unit>(e);
            Kind k = reg.get<Info>(e).kind;
            int ow = reg.get<Owner>(e).id;
            Pos& p = reg.get<Pos>(e);
            u.state = US_Idle;
            u.cd -= dt;
            u.repath -= dt;
            if (reg.all_of<Wander>(e)) {
                auto& wd = reg.get<Wander>(e);
                wd.timer -= dt;
                if (wd.timer <= 0 && u.path.empty()) {
                    wd.timer = 2.f + float(rng() % 400) / 100.f;
                    int nx = int(std::floor(p.x)) + int(rng() % 7) - 3, ny = int(std::floor(p.y)) + int(rng() % 7) - 3;
                    if (walkable(nx, ny, false, 0) && biome(nx, ny) != B_Highlands) { u.path = {{float(nx) + .5f, float(ny) + .5f}}; u.i = 0; }
                }
            }
            if (extinguishTick(e, u, p, dt)) continue;                  // firefighting duty (server_base.inc)
            if (isFactionOwner(ow)) factionAI(e, u, p, dt);
            if (auto* wk = reg.try_get<Worker>(e)) { runWorker(e, u, *wk, dt); }
            else if (auto* cv = reg.try_get<Convoy>(e)) { runConvoy(e, u, *cv, dt); }
            else if (auto* hl = reg.try_get<Haul>(e)) { runHaul(e, u, *hl, dt); }
            else if (auto* bd = reg.try_get<Boarding>(e)) { runBoarding(e, u, *bd, dt); }
            else if (auto* rd = reg.try_get<Raider>(e)) { runRaider(e, u, *rd, dt); if (reg.valid(e) && KINFO[k].dps > 0) combat(e, u, k, ow, p, dt); }
            else if (k == K_Pawn && isPlayer(ow) && huntTick(e, u, ow, p, dt)) {}   // [world] free pawns hunt sheep
            else if (isPatrol(e)) { runPatrol(e, u, reg.get<Patrol>(e), dt); if (reg.valid(e) && KINFO[k].dps > 0) combat(e, u, k, ow, p, dt); }   // [world] faction patrol
            else if (k == K_Monk) {                                  // heal the most wounded friendly unit in range
                entt::entity best = entt::null;
                float bn = 1e9f;
                for (auto o : units) {
                    if (o == e || !reg.valid(o) || reg.all_of<Aboard>(o) || !allied(ow, reg.get<Owner>(o).id)) continue;
                    auto& h = reg.get<Health>(o);
                    if (h.hp >= h.maxhp - .5f) continue;
                    Pos q = reg.get<Pos>(o);
                    float d = std::hypot(p.x - q.x, p.y - q.y);
                    if (d <= KINFO[K_Monk].range && h.hp / h.maxhp < bn) { bn = h.hp / h.maxhp; best = o; }
                }
                if (best != entt::null) {
                    auto& h = reg.get<Health>(best);
                    h.hp = std::min(h.maxhp, h.hp + KINFO[K_Monk].dps * unitDmgMul(ulv(ow, K_Monk)) * dt);
                    u.state = US_Heal;
                    u.path.clear();
                    Pos q = reg.get<Pos>(best);
                    u.aim = uint8_t(int((std::atan2(q.y - p.y, q.x - p.x) + 3.14159f) / 6.28319f * 255.f));
                    if (u.cd <= 0) { u.cd = 1.f; fx(E_Heal, q.x, q.y); }
                }
            } else if (KINFO[k].dps > 0 && ow > 0) {
                combat(e, u, k, ow, p, dt);
            }
        }
        processKills();

        // movement
        for (auto e : units) {
            if (!reg.valid(e)) continue;
            auto& u = reg.get<Unit>(e);
            Pos& p = reg.get<Pos>(e);
            Kind k = reg.get<Info>(e).kind;
            if (u.i >= u.path.size()) { if (!u.path.empty()) u.path.clear(); u.i = 0; if (reg.all_of<AMove>(e) && u.state != US_Attack) reg.remove<AMove>(e); continue; }
            float step = KINFO[k].speed * unitSpeedMul(ulv(reg.get<Owner>(e).id, k)) * dt;
            Pos t = u.path[u.i];
            float dx = t.x - p.x, dy = t.y - p.y, d = std::hypot(dx, dy);
            u.aim = uint8_t(int((std::atan2(dy, dx) + 3.14159f) / 6.28319f * 255.f));
            if (u.state == US_Idle) u.state = US_Move;
            if (d <= step) { p = t; u.i++; }
            else { p.x += dx / d * step; p.y += dy / d * step; }
        }

        shipsTick(dt);

        if (tick % 20 == 0) {
            // cold attrition in the snow unless a campfire warms the spot
            std::vector<Pos> fires;
            std::vector<int> fireOwner;
            for (auto e : blds) if (reg.get<Info>(e).kind == K_Campfire && complete(e)) { fires.push_back(posOf(e)); fireOwner.push_back(reg.get<Owner>(e).id); }
            auto warm = [&](Pos p, int owner) {
                for (size_t i = 0; i < fires.size(); i++)
                    if (allied(owner, fireOwner[i]) && std::hypot(p.x - fires[i].x, p.y - fires[i].y) <= WARMTH_RADIUS) return true;
                return false;
            };
            std::vector<entt::entity> all = units;
            all.insert(all.end(), blds.begin(), blds.end());
            for (auto e : all) {
                if (!reg.valid(e)) continue;
                int ow = reg.get<Owner>(e).id;
                if (ow == 0 || isFactionOwner(ow)) continue;
                Pos p = posOf(e);
                if (biome(int(std::floor(p.x)), int(std::floor(p.y))) != B_Snow || warm(p, ow)) continue;
                auto& h = reg.get<Health>(e);
                if (isBuilding(reg.get<Info>(e).kind)) h.hp = std::max(h.maxhp * .25f, h.hp - h.maxhp * COLD_BUILDING_FRAC);
                else damage(e, COLD_UNIT_DPS, 0);
            }
            processKills();
            // wake up NPC chunks around every player-owned entity
            for (auto e : all) {
                if (!reg.valid(e) || !isPlayer(reg.get<Owner>(e).id)) continue;
                Pos p = posOf(e);
                int cx = chunkOf(int(std::floor(p.x))), cy = chunkOf(int(std::floor(p.y)));
                for (int y = cy - 2; y <= cy + 2; y++) for (int x = cx - 2; x <= cx + 2; x++) spawnChunkNpcs(x, y);
                int rx = floorDiv(cx, REGION_CHUNKS), ry = floorDiv(cy, REGION_CHUNKS);
                for (int y = ry - 1; y <= ry + 1; y++) for (int x = rx - 1; x <= rx + 1; x++) processRegion(x, y);
            }
            baseSecond();
            townTick();
            callHelp();
            updateSpotting();
            spawnHauls();
            raidTick();
        }
        if (tick % 4 == 0)                                             // exploration by online players' units
            for (auto e : units) {
                if (!reg.valid(e)) continue;
                int ow = reg.get<Owner>(e).id;
                if (!isPlayer(ow) || !pl[ow].online) continue;
                Pos p = reg.get<Pos>(e);
                IV t{int(std::floor(p.x)), int(std::floor(p.y))};
                auto& u = reg.get<Unit>(e);
                if (u.lastRev.x == t.x && u.lastRev.y == t.y) continue;
                u.lastRev = t;
                reveal(ow, t.x, t.y, KINFO[reg.get<Info>(e).kind].vision);
            }
        if (tick % 4 == 0)
            for (auto e : blds) {   // keep building vision fresh for players who just (re)joined
                if (!reg.valid(e)) continue;                     // (baseSecond may have removed relocated buildings)
                int ow = reg.get<Owner>(e).id;
                if (!isPlayer(ow) || !pl[ow].online || !complete(e)) continue;
                if (tick % 40 != 0) continue;
                Pos c = posOf(e);
                reveal(ow, int(std::floor(c.x)), int(std::floor(c.y)), KINFO[reg.get<Info>(e).kind].vision);
            }

        worldTick(dt);                                                  // [world] clearing jobs, sheep repopulation, patrols
        baseTail();
        for (int id = 1; id <= MAX_PLAYERS; id++) {
            if (!pl[id].online) continue;
            if (tick % 10 == 0) sendQueues(id);
            if (tick % 2 == 0) sendState(id);
            if (tick % 8 == 0) sendPaths(id);
            if (tick % 10 == 3 && pl[id].online) sendShipInfo(id);
            if (tick % 10 == 0) sendFog(id);
        }
        if (tick % 2 == 0) effects.clear();
        if (tick % 20 == 0) { sendRoster(); for (int id = 1; id <= MAX_PLAYERS; id++) if (pl[id].online) sendMap(id); }
        if (clock - lastSave > 30) { save(); lastSave = clock; }
    }

    void combat(entt::entity e, Unit& u, Kind k, int ow, Pos& p, float dt) {
        entt::entity tg = entt::null;
        bool explicitOrder = false;
        if (auto* a = reg.try_get<Attack>(e)) {
            tg = find(a->target);
            explicitOrder = true;
            if (tg == entt::null || dying.count(a->target) || allied(ow, reg.get<Owner>(tg).id) || reg.all_of<Aboard>(tg)) { reg.remove<Attack>(e); tg = entt::null; explicitOrder = false; }
        }
        bool amove = reg.all_of<AMove>(e);
        if (tg == entt::null && (isArmy(k) || amove) && u.repath <= 0) {
            float aggro = amove ? 7.f : 5.f;
            tg = nearestHostile(p, ow, aggro, amove);
            if (tg == entt::null) u.repath = .3f;
        }
        if (tg == entt::null) return;
        Pos tp = posOf(tg);
        float d = rectDist(p, tg);
        u.aim = uint8_t(int((std::atan2(tp.y - p.y, tp.x - p.x) + 3.14159f) / 6.28319f * 255.f));
        if (d <= KINFO[k].range) {
            u.path.clear();
            u.i = 0;
            u.state = US_Attack;
            damage(tg, KINFO[k].dps * unitDmgMul(ulv(ow, k)) * damageVs(k, reg.get<Info>(tg).kind) * armorOf(reg.get<Info>(tg).kind) * dt, ow, reg.get<Uid>(e).id);
            if (k == K_Archer && u.cd <= 0) { u.cd = .8f; archerShot(ow, p, tp, tg); }   // fire arrows at troop level 3
        } else if (u.repath <= 0 || u.path.empty()) {
            u.repath = 1.f;
            bool bld = isBuilding(reg.get<Info>(tg).kind);
            if (!order(e, tp, bld ? reg.get<Uid>(tg).id : 0) && explicitOrder) reg.remove<Attack>(e);
            if (!explicitOrder && !reg.all_of<Attack>(e)) { /* auto-acquired targets are re-evaluated each pass */ }
        }
        if (!explicitOrder && tg != entt::null && !reg.all_of<Attack>(e) && d > KINFO[k].range && !u.path.empty()) u.state = US_Move;
    }

    void runWorker(entt::entity e, Unit& u, Worker& w, float dt) {
        Pos p = reg.get<Pos>(e);
        auto home = find(w.home);
        if (home == entt::null) { reg.remove<Worker>(e); return; }
        auto& hb = reg.get<Building>(home);
        int ow = reg.get<Owner>(e).id;
        auto toward = [&](entt::entity b) { return order(e, posOf(b), reg.get<Uid>(b).id); };
        auto findStorage = [&]() {                              // nearest own storage that still has room
            entt::entity best = entt::null;
            float bd = 1e9f;
            reg.view<Info, Building, Owner>().each([&](entt::entity st, const Info& i, const Building& sb, const Owner& so) {
                if (!isStorage(i.kind) || sb.progress < 1.f || so.id != ow || capOf(st) - stockOf(st) < 1.f) return;
                Pos c = footCenter(i.kind, sb.tx, sb.ty);
                float d = std::hypot(p.x - c.x, p.y - c.y);
                if (d < bd) { bd = d; best = st; }
            });
            return best;
        };
        switch (w.phase) {
        case 0: {
            if (hb.progress < 1.f) return;
            if (rectDist(p, home) > 1.6f) { if (u.path.empty()) toward(home); return; }
            float tot = 0;
            for (float s : hb.stock) tot += s;
            if (tot >= 8.f && findStorage() != entt::null) {
                w.dest = 0;
                float f = std::min(1.f, 30.f / tot);
                for (int r = 0; r < R_Count; r++) { w.cargo[r] = hb.stock[r] * f; hb.stock[r] -= w.cargo[r]; }
                w.phase = 1;
                w.timer = 1.f;
            }
            break;
        }
        case 1:
            u.state = US_Work;
            u.path.clear();
            w.timer -= dt;
            if (w.timer <= 0) w.phase = 2;
            break;
        case 2: {
            auto st = find(w.dest);
            if (st == entt::null || !complete(st) || capOf(st) - stockOf(st) < 1.f) { st = findStorage(); w.dest = st == entt::null ? 0 : reg.get<Uid>(st).id; u.path.clear(); }
            if (st == entt::null) return;                       // every storage is full: wait with the cargo
            if (rectDist(p, st) <= 1.5f) {
                storeCargo(st, w.cargo);
                float left = 0;
                for (float c : w.cargo) left += c;
                if (left < .5f) { for (float& c : w.cargo) c = 0; w.phase = 3; w.dest = 0; }
                else w.dest = 0;                                // partly full: try the next nearest storage
                u.path.clear();
                return;
            }
            if (u.path.empty() && !toward(st)) w.timer = 0;
            break;
        }
        default:
            if (rectDist(p, home) <= 1.6f) { w.phase = 0; u.path.clear(); }
            else if (u.path.empty()) toward(home);
            break;
        }
    }

    // ---------------------------------------------------------------- networking out
    struct Source { float x, y, r; };
    std::vector<Source> sourcesFor(int id) {
        std::vector<Source> s;
        reg.view<Owner, Info>().each([&](entt::entity e, const Owner& o, const Info& i) {
            if (o.id == 0 || !allied(id, o.id) || !complete(e)) return;
            Pos p = posOf(e);
            s.push_back({p.x, p.y, float(KINFO[i.kind].vision) + (isBuilding(i.kind) ? float(KINFO[i.kind].fw) * .5f : 0.f)});
        });
        return s;
    }
    static bool seen(const std::vector<Source>& s, float x, float y, float pad = 0) {
        for (auto& v : s) if ((x - v.x) * (x - v.x) + (y - v.y) * (y - v.y) <= (v.r + pad) * (v.r + pad)) return true;
        return false;
    }

    void sendState(int id) {
        Profile& p = pl[id];
        auto src = sourcesFor(id);
        Writer w;
        w.put<uint8_t>(S_State);
        for (int r = 0; r < R_Count; r++) w.put<float>(p.res[r]);
        for (int f = 0; f < FACTION_COUNT; f++) w.put<int8_t>(int8_t(std::lround(p.rep[f])));
        for (int k = K_Pawn; k <= K_Monk; k++) w.put<uint8_t>(uint8_t(std::max<int>(1, p.ulevel[k])));
        size_t cp = w.b.size();
        w.put<uint16_t>(0);
        uint16_t n = 0;
        reg.view<Uid, Owner, Info, Health>().each([&](entt::entity e, const Uid& uid, const Owner& o, const Info& in, const Health& h) {
            Pos ps = posOf(e);
            static const bool seeAll = std::getenv("BW_SEEALL") != nullptr;      // test hook: show every entity
            if (!seeAll && !allied(id, o.id) && !seen(src, ps.x, ps.y, isBuilding(in.kind) ? 2.f : 0.f) && !((reg.all_of<Convoy>(e) || reg.all_of<Haul>(e)) && spottedForClan(id, uid.id))) return;
            uint8_t state = 0, aux = 0, aim = 0, prog = 100;
            uint16_t stock = 0;
            if (const Unit* u = reg.try_get<Unit>(e)) {
                state = u->state;
                if (reg.all_of<Convoy>(e) || reg.all_of<Haul>(e)) state |= UNIT_CONVOY_FLAG;
                if (reg.all_of<Support>(e)) state |= UNIT_SUPPORT_FLAG;
                aim = u->aim;
                if (const Worker* wk = reg.try_get<Worker>(e)) {
                    float best = 0;
                    for (int r = 0; r < R_Count; r++) if (wk->cargo[r] > best) { best = wk->cargo[r]; aux = uint8_t(r + 1); }
                } else aux = worldAux(e);                               // [world] hunter pawns carrying meat
            } else {
                const Building& b = reg.get<Building>(e);
                state = bflagsOf(e);                                   // BLD_BURNING / BLD_DISABLED / BLD_MOVING
                aux = in.level;
                aim = b.haulPct;
                prog = uint8_t(std::min(1.f, b.progress) * 100.f);
                float tot = 0;
                for (float s : b.stock) tot += s;
                stock = uint16_t(std::min(tot, 65000.f));
            }
            if (!shipStateHook(id, e, o.id, in, aux, prog, stock)) return;
            w.put<uint32_t>(uid.id).put<uint8_t>(in.kind).put<uint8_t>(o.id).put<float>(ps.x).put<float>(ps.y);
            w.put<uint16_t>(uint16_t(std::max(0.f, h.hp))).put<uint16_t>(uint16_t(h.maxhp));
            w.put<uint8_t>(state).put<uint8_t>(aux).put<uint8_t>(aim).put<uint8_t>(prog).put<uint16_t>(stock);
            n++;
        });
        std::memcpy(&w.b[cp], &n, 2);
        size_t ep = w.b.size();
        w.put<uint16_t>(0);
        uint16_t ne = 0;
        for (auto& f : effects)
            if (seen(src, f.x, f.y, 3.f) || seen(src, f.x2, f.y2, 3.f)) { w.put<uint8_t>(f.type).put<float>(f.x).put<float>(f.y).put<float>(f.x2).put<float>(f.y2); ne++; }
        std::memcpy(&w.b[ep], &ne, 2);
        net.send(p.conn, w);
    }

#include "server_world.inc"
#include "server_ships.inc"
#include "server_base.inc"   // base mechanics: fire, queues, clan defense, relocation
};

void Server::sendFog(int id) {
    Profile& p = pl[id];
    std::unordered_map<int64_t, Bits> uni = p.expl;
    for (int a = 1; a <= MAX_PLAYERS; a++)
        if (a != id && pl[a].used && shares(a, id))
            for (auto& [k, b] : pl[a].expl) { Bits& u = uni.try_emplace(k, Bits{}).first->second; for (size_t i = 0; i < 32; i++) u[i] |= b[i]; }
    std::vector<std::pair<int64_t, Bits>> fogOut;
    std::vector<int64_t> terrOut;
    for (auto& [k, b] : uni) {
        auto it = p.sentFog.find(k);
        if (it != p.sentFog.end() && it->second == b) continue;
        if (fogOut.size() >= 150) break;
        fogOut.push_back({k, b});
        p.sentFog[k] = b;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int64_t nk = chunkKey(keyCx(k) + dx, keyCy(k) + dy);
                if (p.sentTerr.insert(nk).second) terrOut.push_back(nk);
            }
    }
    if (fogOut.empty()) return;
    Writer w;
    w.put<uint8_t>(S_Fog).put<uint16_t>(uint16_t(terrOut.size()));
    for (auto k : terrOut) {
        int cx = keyCx(k), cy = keyCy(k);
        biome(cx * CH, cy * CH);
        w.put<int32_t>(cx).put<int32_t>(cy);
        w.bytes(terr[k].data(), CH * CH);
    }
    w.put<uint16_t>(uint16_t(fogOut.size()));
    for (auto& [k, b] : fogOut) { w.put<int32_t>(keyCx(k)).put<int32_t>(keyCy(k)); w.bytes(b.data(), 32); }
    net.send(p.conn, w);
}

void Server::sendRoster() {
    Pos thPos[MAX_PLAYERS + 1];
    bool hasTh[MAX_PLAYERS + 1]{};
    reg.view<Info, Building, Owner>().each([&](const Info& i, const Building& b, const Owner& o) {
        if (i.kind == K_TownHall && isPlayer(o.id) && !hasTh[o.id]) { hasTh[o.id] = true; thPos[o.id] = footCenter(i.kind, b.tx, b.ty); }
    });
    for (int id = 1; id <= MAX_PLAYERS; id++) {
        if (!pl[id].online) continue;
        Writer w;
        w.put<uint8_t>(S_Roster);
        uint8_t n = 0;
        size_t cp = w.b.size();
        w.put<uint8_t>(0);
        for (int o = 1; o <= MAX_PLAYERS; o++) {
            if (!pl[o].used) continue;
            uint8_t flags = uint8_t((pl[o].online ? 1 : 0) | (pl[id].allied[o] ? 2 : 0) | (pl[o].pending[id] ? 4 : 0) | (pl[id].pending[o] ? 8 : 0));
            w.put<uint8_t>(uint8_t(o)).put<uint8_t>(flags).put<uint8_t>(pl[o].clan);
            w.str(pl[o].name);
            bool known = hasTh[o] && (o == id || shares(id, o) || explored(id, int(std::floor(thPos[o].x)), int(std::floor(thPos[o].y))));
            w.put<uint8_t>(known ? 1 : 0).put<float>(known ? thPos[o].x : 0.f).put<float>(known ? thPos[o].y : 0.f);
            n++;
        }
        w.b[cp] = n;
        uint8_t nc = 0;
        size_t cc = w.b.size();
        w.put<uint8_t>(0);
        for (int c = 1; c <= MAX_CLANS; c++) {
            if (!clans[c].used) continue;
            int members = 0;
            for (int o = 1; o <= MAX_PLAYERS; o++) if (pl[o].used && pl[o].clan == c) members++;
            w.put<uint8_t>(uint8_t(c));
            w.str(clans[c].name);
            w.put<uint8_t>(uint8_t(members)).put<uint8_t>(clans[c].leader);
            nc++;
        }
        w.b[cc] = nc;
        net.send(pl[id].conn, w);
    }
}

void Server::sendMap(int id) {                                          // faction towns the player has discovered
    Writer w;
    w.put<uint8_t>(S_Map);
    uint8_t n = 0;
    size_t cp = w.b.size();
    w.put<uint8_t>(0);
    for (auto& t : towns) {
        if (!t.alive || !explored(id, t.cx, t.cy)) continue;
        w.put<uint8_t>(t.faction).put<float>(float(t.cx) + .5f).put<float>(float(t.cy) + .5f);
        if (++n == 255) break;
    }
    w.b[cp] = n;
    net.send(pl[id].conn, w);
}

}  // namespace

// ---------------------------------------------------------------- persistence (binary savegame)
namespace {

void Server::save() {
    Writer w;
    w.put<uint32_t>(SAVE_MAGIC).put<uint32_t>(seed).put<uint32_t>(nextUid).put<double>(clock);
    uint32_t np = 0;
    for (int i = 1; i <= MAX_PLAYERS; i++) if (pl[i].used) np++;
    w.put<uint32_t>(np);
    for (int i = 1; i <= MAX_PLAYERS; i++) {
        if (!pl[i].used) continue;
        const Profile& p = pl[i];
        w.put<uint8_t>(uint8_t(i));
        w.str(p.uuid);
        w.str(p.name);
        w.put<uint64_t>(p.lastSeen);
        for (float r : p.res) w.put<float>(r);
        for (int a = 0; a <= MAX_PLAYERS; a++) w.put<uint8_t>(uint8_t((p.allied[a] ? 1 : 0) | (p.pending[a] ? 2 : 0)));
        w.put<float>(p.cam.x).put<float>(p.cam.y);
        w.put<uint64_t>(p.steamId);
        w.put<uint8_t>(p.clan);
        for (float rp : p.rep) w.put<float>(rp);
        for (int k = K_Pawn; k <= K_Monk; k++) w.put<uint8_t>(uint8_t(std::max<int>(1, p.ulevel[k])));
        w.put<uint32_t>(uint32_t(p.expl.size()));
        for (auto& [k, b] : p.expl) { w.put<int64_t>(k); w.bytes(b.data(), 32); }
    }
    for (int c = 1; c <= MAX_CLANS; c++) { w.put<uint8_t>(clans[c].used ? 1 : 0); if (clans[c].used) { w.str(clans[c].name); w.put<uint8_t>(clans[c].leader); } }
    for (float d : facDonated) w.put<float>(d);
    w.put<uint32_t>(uint32_t(regionsDone.size()));
    for (auto k : regionsDone) w.put<int64_t>(k);
    w.put<uint32_t>(uint32_t(towns.size()));
    for (auto& t : towns) w.put<uint16_t>(t.id).put<uint8_t>(t.faction).put<int32_t>(t.cx).put<int32_t>(t.cy).put<uint32_t>(t.th).put<uint8_t>(t.alive ? 1 : 0).put<double>(t.respawnAt);
    w.put<uint32_t>(uint32_t(spawned.size()));
    for (auto k : spawned) w.put<int64_t>(k);
    uint32_t saved = 0;
    reg.view<Uid>().each([&](entt::entity e, const Uid&) { if (!ephemeral(e)) saved++; });
    w.put<uint32_t>(saved);
    reg.view<Uid, Owner, Info, Health>().each([&](entt::entity e, const Uid& uid, const Owner& o, const Info& in, const Health& h) {
        if (ephemeral(e)) return;
        w.put<uint32_t>(uid.id).put<uint8_t>(in.kind).put<uint8_t>(o.id).put<uint8_t>(in.level).put<float>(h.hp).put<float>(h.maxhp);
        w.put<uint16_t>(reg.all_of<TownTag>(e) ? reg.get<TownTag>(e).town : uint16_t(0));
        if (const Building* b = reg.try_get<Building>(e)) {
            w.put<uint8_t>(1).put<int32_t>(b->tx).put<int32_t>(b->ty).put<float>(b->progress);
            for (float s : b->stock) w.put<float>(s);
            w.put<float>(b->respawn);
            w.put<uint8_t>(b->haulPct);
        } else {
            Pos p = reg.get<Pos>(e);
            w.put<uint8_t>(0).put<float>(p.x).put<float>(p.y);
            const Worker* wk = reg.try_get<Worker>(e);
            const Convoy* cv = reg.try_get<Convoy>(e);
            const Haul* hl = reg.try_get<Haul>(e);
            w.put<uint8_t>(uint8_t((wk ? 1 : 0) | (cv ? 2 : 0) | (hl ? 4 : 0)));
            if (wk) { w.put<uint32_t>(wk->home).put<uint8_t>(wk->phase); for (float c : wk->cargo) w.put<float>(c); }
            if (cv) { w.put<uint8_t>(cv->from).put<uint8_t>(cv->to); for (float c : cv->cargo) w.put<float>(c); }
            if (hl) { w.put<uint32_t>(hl->storage).put<uint8_t>(hl->phase); for (float c : hl->cargo) w.put<float>(c); }
        }
    });
    worldSave(w);                                                       // [world] appended section: cleared tiles
    shipsSave(w);   // append-only ship passenger section
    saveBase(w);
    std::string tmp = savePath + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { std::printf("Could not write %s\n", tmp.c_str()); return; }
        f.write(reinterpret_cast<const char*>(w.b.data()), std::streamsize(w.b.size()));
    }
    std::error_code ec;
    std::filesystem::rename(tmp, savePath, ec);
    if (ec) std::printf("Could not replace %s: %s\n", savePath.c_str(), ec.message().c_str());
}

bool Server::load() {
    std::ifstream f(savePath, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Reader r{data.data(), data.data() + data.size()};
    if (r.get<uint32_t>() != SAVE_MAGIC) { std::puts("Savegame has an unknown format; starting fresh."); return false; }
    seed = r.get<uint32_t>();
    rng.seed(seed ^ uint32_t(nowSec()));
    nextUid = r.get<uint32_t>();
    clock = r.get<double>();
    uint32_t np = r.get<uint32_t>();
    for (uint32_t n = 0; n < np && r.ok; n++) {
        int id = r.get<uint8_t>();
        if (id < 1 || id > MAX_PLAYERS) return false;
        Profile& p = pl[id];
        p.used = true;
        p.uuid = r.str();
        p.name = r.str();
        p.lastSeen = r.get<uint64_t>();
        for (float& v : p.res) v = r.get<float>();
        for (int a = 0; a <= MAX_PLAYERS; a++) { uint8_t b = r.get<uint8_t>(); p.allied[a] = b & 1; p.pending[a] = b & 2; }
        p.cam.x = r.get<float>();
        p.cam.y = r.get<float>();
        p.steamId = r.get<uint64_t>();
        p.clan = r.get<uint8_t>();
        for (float& rp : p.rep) rp = r.get<float>();
        for (int k = K_Pawn; k <= K_Monk; k++) p.ulevel[k] = r.get<uint8_t>();
        uint32_t ne = r.get<uint32_t>();
        for (uint32_t i = 0; i < ne && r.ok; i++) {
            int64_t k = r.get<int64_t>();
            Bits b{};
            if (auto d = r.bytes(32)) std::memcpy(b.data(), d, 32);
            p.expl[k] = b;
        }
    }
    for (int c = 1; c <= MAX_CLANS; c++) {
        clans[c] = Clan{};
        if (r.get<uint8_t>()) { clans[c].used = true; clans[c].name = r.str(); clans[c].leader = r.get<uint8_t>(); }
    }
    for (float& d : facDonated) d = r.get<float>();
    uint32_t nr = r.get<uint32_t>();
    for (uint32_t i = 0; i < nr && r.ok; i++) regionsDone.insert(r.get<int64_t>());
    uint32_t nt = r.get<uint32_t>();
    for (uint32_t i = 0; i < nt && r.ok; i++) {
        Town t{};
        t.id = r.get<uint16_t>(); t.faction = r.get<uint8_t>(); t.cx = r.get<int32_t>(); t.cy = r.get<int32_t>(); t.th = r.get<uint32_t>();
        t.alive = r.get<uint8_t>() != 0; t.respawnAt = r.get<double>(); t.nextSpawn = 0; t.lastAlert = -100;
        towns.push_back(t);
    }
    uint32_t ns = r.get<uint32_t>();
    for (uint32_t i = 0; i < ns && r.ok; i++) spawned.insert(r.get<int64_t>());
    uint32_t nent = r.get<uint32_t>();
    for (uint32_t n = 0; n < nent && r.ok; n++) {
        uint32_t uid = r.get<uint32_t>();
        Kind k = Kind(r.get<uint8_t>());
        uint8_t owner = r.get<uint8_t>(), level = r.get<uint8_t>();
        float hp = r.get<float>(), mhp = r.get<float>();
        uint16_t town = r.get<uint16_t>();
        uint8_t isB = r.get<uint8_t>();
        if (!r.ok || k >= K_Count) return false;
        auto e = makeBase(k, owner, uid);
        reg.get<Info>(e).level = level;
        reg.get<Health>(e) = Health{hp, mhp, 0};
        if (town) reg.emplace<TownTag>(e, TownTag{town});
        if (isB) {
            Building b{};
            b.tx = r.get<int32_t>(); b.ty = r.get<int32_t>(); b.progress = r.get<float>();
            for (float& s : b.stock) s = r.get<float>();
            b.respawn = r.get<float>();
            b.haulPct = r.get<uint8_t>();
            reg.emplace<Building>(e, b);
            for (int y = b.ty; y < b.ty + KINFO[k].fh; y++)
                for (int x = b.tx; x < b.tx + KINFO[k].fw; x++) occ[tkey(x, y)] = uid;
        } else {
            Pos p{r.get<float>(), r.get<float>()};
            reg.emplace<Pos>(e, p);
            reg.emplace<Unit>(e);
            if (k == K_Sheep) reg.emplace<Wander>(e, Wander{1.f});
            uint8_t fl = r.get<uint8_t>();
            if (town) {
                if (Town* tw = townById(town)) { reg.emplace<Home>(e, Home{float(tw->cx) + .5f, float(tw->cy) + 3.f, 0}); reg.emplace_or_replace<Wander>(e, Wander{1.f}); }
            }
            if (fl & 1) {
                Worker w;
                w.home = r.get<uint32_t>();
                w.phase = r.get<uint8_t>();
                for (float& c : w.cargo) c = r.get<float>();
                if (w.phase == 1) w.phase = 2;
                reg.emplace<Worker>(e, w);
            }
            if (fl & 2) {
                Convoy cv{};
                cv.from = r.get<uint8_t>(); cv.to = r.get<uint8_t>();
                for (float& c : cv.cargo) c = r.get<float>();
                reg.emplace<Convoy>(e, cv);
            }
            if (fl & 4) {
                Haul hl;
                hl.storage = r.get<uint32_t>(); hl.phase = r.get<uint8_t>();
                for (float& c : hl.cargo) c = r.get<float>();
                reg.emplace<Haul>(e, hl);
            }
        }
    }
    worldLoad(r);                                                       // [world] optional appended section
    if (!r.ok) { std::puts("Savegame is truncated; starting fresh."); return false; }
    shipsLoad(r);
    if (r.ok && r.p < r.e) loadBase(r);                                  // optional base-mechanics section (appended)
    if (!r.ok) { std::puts("Savegame is truncated; starting fresh."); return false; }
    shipsLoad(r);
    return true;
}

void Server::run() {
    using clk = std::chrono::steady_clock;
    auto next = clk::now();
    const auto step = std::chrono::milliseconds(1000 / TICK_HZ);
    while (!g_quit) {
        NetEvent ev;
        while (net.poll(ev)) {
            switch (ev.type) {
            case NetEvent::Connect: break;
            case NetEvent::Receive: onPacket(ev.conn, ev.data); break;
            case NetEvent::Disconnect: onDisconnect(ev.conn); break;
            }
        }
        if (clock - lastPublish > 5) {                          // keep the Steam server-browser entry fresh
            int online = 0;
            for (int i = 1; i <= MAX_PLAYERS; i++) if (pl[i].online) online++;
            net.publish(online, MAX_PLAYERS, "Base Wars");
            lastPublish = clock;
        }
        update(1.f / TICK_HZ);
        net.flush();
        next += step;
        auto now = clk::now();
        if (next < now - std::chrono::seconds(1)) next = now;
        std::this_thread::sleep_until(next);
    }
    std::puts("Shutting down, saving world...");
}

}  // namespace

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : DEFAULT_PORT;
    uint32_t seed = argc > 2 ? uint32_t(std::strtoul(argv[2], nullptr, 10)) : std::random_device{}();
    std::string save = argc > 3 ? argv[3] : "savegame.bin";
    std::string serverName = argc > 4 ? argv[4] : "Base Wars Server";
    if (enet_initialize() != 0) { std::puts("enet_initialize failed"); return 1; }
    std::signal(SIGINT, [](int) { g_quit = 1; });
    std::signal(SIGTERM, [](int) { g_quit = 1; });
    {
        Server s(uint16_t(port), seed, save, serverName);
        s.run();
    }
    enet_deinitialize();
}
