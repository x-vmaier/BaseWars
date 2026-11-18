// Authoritative server: map gen (noise + WFC), fog, economy, army, diplomacy.
#include "shared.hpp"

#include <entt/entt.hpp>

#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <queue>
#include <random>
#include <thread>

namespace {

struct Owner { uint8_t id; };                                   // 0 = neutral
struct Health { Kind kind; float hp, maxhp; uint8_t lastHit; };
struct Mover { std::vector<Pos> path; size_t i = 0; };          // units only
struct Attack { entt::entity target; };
struct THDestroyed { entt::entity e; uint8_t killer; };

struct Player {
    bool active = false;
    ENetPeer* peer = nullptr;
    float gold = 300, wood = 200;
    std::vector<uint8_t> expl;                                  // permanently explored tiles
    bool allied[MAX_PLAYERS + 1]{};
    bool pending[MAX_PLAYERS + 1]{};                            // pending[x] = I proposed to x
};

float hf(int x, int y, uint32_t s) { return float(hash2(x, y, s) & 0xFFFF) / 65535.f; }
float vnoise(float x, float y, uint32_t s) {
    int xi = int(std::floor(x)), yi = int(std::floor(y));
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    return std::lerp(std::lerp(hf(xi, yi, s), hf(xi + 1, yi, s), fx), std::lerp(hf(xi, yi + 1, s), hf(xi + 1, yi + 1, s), fx), fy);
}
float fbm(float x, float y, uint32_t s) {
    float v = 0, a = 0.5f, t = 0;
    for (int o = 0; o < 4; o++) { v += a * vnoise(x, y, s + o * 131); t += a; x *= 2; y *= 2; a *= 0.5f; }
    return v / t;
}

struct Server {
    static constexpr int N = MAP_W * MAP_H, GC = 8, GW = (MAP_W + GC - 1) / GC, GH = (MAP_H + GC - 1) / GC;
    ENetHost* host = nullptr;
    entt::registry reg;
    entt::dispatcher disp;
    std::vector<uint8_t> terrain, occ;
    Player pl[MAX_PLAYERS + 1];
    entt::entity th[MAX_PLAYERS + 1];
    std::vector<IV> slots, camps;
    std::vector<std::vector<entt::entity>> grid;                // spatial hash of units
    uint32_t seed, tick = 0;
    std::mt19937 rng;

    Server(uint16_t port, uint32_t sd) : seed(sd), rng(sd) {
        for (auto& t : th) t = entt::null;
        occ.assign(N, 0);
        grid.resize(GW * GH);
        for (auto& p : pl) p.expl.assign(N, 0);
        buildTerrain();
        for (auto c : camps) {                                  // neutral camps
            auto e = make(K_TownHall, 0, c.x + .5f, c.y + .5f);
            auto& h = reg.get<Health>(e);
            h.hp = h.maxhp = 400;
            reveal(0, c.x, c.y, 3);
        }
        disp.sink<THDestroyed>().connect<&Server::onTH>(*this);
        ENetAddress a{};
        a.host = ENET_HOST_ANY;
        a.port = port;
        host = enet_host_create(&a, MAX_PLAYERS + 4, 2, 0, 0);
        if (!host) { std::puts("Failed to bind port"); std::exit(1); }
        std::printf("Server listening on UDP port %d (seed %u)\n", port, seed);
    }

    // ---------- map generation: noise-biased Wave Function Collapse ----------
    bool walk(int x, int y) const { return inb(x, y) && walkableT(terrain[ix(x, y)]); }

    void buildTerrain() {
        // bits: 0 clearing, 1 forest, 2 mountain, 3 sea. Sea may only touch sea/clearing (beaches).
        static constexpr uint8_t ALLOW[4] = {0b1111, 0b0111, 0b0111, 0b1001};
        std::vector<uint8_t> dom(N, 0b1111);
        std::vector<float> jit(N);
        for (int i = 0; i < N; i++) jit[i] = hf(i % MAP_W, i / MAP_W, seed + 3);
        auto propagate = [&](int s) {
            std::vector<int> st{s};
            const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            while (!st.empty()) {
                int i = st.back();
                st.pop_back();
                uint8_t al = 0;
                for (int t = 0; t < 4; t++) if (dom[i] >> t & 1) al |= ALLOW[t];
                for (int d = 0; d < 4; d++) {
                    int nx = i % MAP_W + dx[d], ny = i / MAP_W + dy[d];
                    if (!inb(nx, ny)) continue;
                    int n = ix(nx, ny);
                    uint8_t nm = dom[n] & al;
                    if (nm && nm != dom[n]) { dom[n] = nm; st.push_back(n); }
                }
            }
        };
        for (;;) {
            int best = -1, bp = 9;
            float bj = 2;
            for (int i = 0; i < N; i++) {                       // lowest-entropy cell, jitter tie-break
                int p = std::popcount(dom[i]);
                if (p < 2) continue;
                if (p < bp || (p == bp && jit[i] < bj)) { best = i; bp = p; bj = jit[i]; }
            }
            if (best < 0) break;
            int x = best % MAP_W, y = best / MAP_W;
            float e = fbm(x * .055f, y * .055f, seed), m = fbm(x * .08f, y * .08f, seed + 77);
            float wt[4] = {.05f, (m - .52f) * 3, (e - .62f) * 4, (.36f - e) * 4};
            int bt = 0;
            float bw = -1e9f;
            for (int t = 0; t < 4; t++) {
                if (!(dom[best] >> t & 1)) continue;
                float v = wt[t] + (hf(x, y, seed + 9 + t) - .5f) * .05f;
                if (v > bw) { bw = v; bt = t; }
            }
            dom[best] = uint8_t(1 << bt);
            propagate(best);
        }
        terrain.resize(N);
        for (int i = 0; i < N; i++) terrain[i] = uint8_t(std::countr_zero(dom[i]));

        // spawn plots (3x3 grid of regions minus the centre) and neutral camps
        for (int gy = 0; gy < 3; gy++)
            for (int gx = 0; gx < 3; gx++) {
                if (gx == 1 && gy == 1) continue;
                slots.push_back({16 + 32 * gx + int(hf(gx, gy, seed + 5) * 10) - 5, 16 + 32 * gy + int(hf(gx, gy, seed + 6) * 10) - 5});
            }
        camps = {{32, 32}, {64, 32}, {32, 64}, {64, 64}, {48, 48}};
        auto carve = [&](IV c, int r) {
            for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++)
                    if (inb(c.x + dx, c.y + dy)) terrain[ix(c.x + dx, c.y + dy)] = T_Clearing;
        };
        for (auto s : slots) carve(s, 3);
        for (auto c : camps) carve(c, 1);

        // guarantee ground connectivity between every plot/camp by carving land bridges
        std::vector<IV> pts = slots;
        pts.insert(pts.end(), camps.begin(), camps.end());
        for (int iter = 0; iter < int(pts.size()) + 4; iter++) {
            std::vector<uint8_t> seen(N, 0);
            std::vector<int> q{ix(pts[0].x, pts[0].y)};
            seen[q[0]] = 1;
            for (size_t h = 0; h < q.size(); h++) {
                int x = q[h] % MAP_W, y = q[h] / MAP_W;
                const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                for (int d = 0; d < 4; d++)
                    if (walk(x + dx[d], y + dy[d]) && !seen[ix(x + dx[d], y + dy[d])]) { seen[ix(x + dx[d], y + dy[d])] = 1; q.push_back(ix(x + dx[d], y + dy[d])); }
            }
            bool all = true;
            for (size_t k = 1; k < pts.size() && all; k++) {
                if (seen[ix(pts[k].x, pts[k].y)]) continue;
                all = false;
                int x = pts[k].x, y = pts[k].y;
                auto fix = [&](int cx, int cy) {
                    uint8_t& t = terrain[ix(cx, cy)];
                    if (t == T_Sea) t = T_Clearing;
                    else if (t == T_Mountain) t = T_Forest;
                };
                while (x != pts[0].x) { x += x < pts[0].x ? 1 : -1; fix(x, y); }
                while (y != pts[0].y) { y += y < pts[0].y ? 1 : -1; fix(x, y); }
            }
            if (all) break;
        }
    }

    // ---------- helpers ----------
    bool allied(int a, int b) const { return a == b || pl[a].allied[b]; }

    entt::entity make(Kind k, uint8_t owner, float x, float y) {
        auto e = reg.create();
        reg.emplace<Pos>(e, Pos{x, y});
        reg.emplace<Owner>(e, Owner{owner});
        reg.emplace<Health>(e, Health{k, KINFO[k].hp, KINFO[k].hp, 0});
        if (KINFO[k].building) occ[ix(int(x), int(y))] = 1;
        else reg.emplace<Mover>(e);
        return e;
    }

    void reveal(int owner, int cx, int cy, int r) {
        for (int y = cy - r; y <= cy + r; y++)
            for (int x = cx - r; x <= cx + r; x++)
                if (inb(x, y)) pl[owner].expl[ix(x, y)] = 1;
    }

    std::vector<uint8_t> vision(int id) const {
        auto v = pl[id].expl;
        for (int a = 0; a <= MAX_PLAYERS; a++)
            if (a != id && pl[id].allied[a])
                for (int i = 0; i < N; i++) v[i] |= pl[a].expl[i];
        return v;
    }

    void say(int id, const std::string& s) {
        if (id < 1 || id > MAX_PLAYERS || !pl[id].active) return;
        Writer w;
        w.put<uint8_t>(S_Text);
        w.str(s);
        sendPacket(pl[id].peer, w);
    }

    void breakAlliance(int a, int b) {
        pl[a].allied[b] = pl[b].allied[a] = false;
        pl[a].pending[b] = pl[b].pending[a] = false;
        say(a, "Alliance broken.");
        say(b, "Player " + std::to_string(a) + " broke the alliance!");
    }

    void removeOwner(int id) {
        std::vector<entt::entity> v;
        reg.view<Owner>().each([&](entt::entity e, const Owner& o) { if (o.id == id) v.push_back(e); });
        for (auto e : v) {
            if (KINFO[reg.get<Health>(e).kind].building) { auto& p = reg.get<Pos>(e); occ[ix(int(p.x), int(p.y))] = 0; }
            reg.destroy(e);
        }
    }

    void resetPlayer(int id) {
        Player& p = pl[id];
        p.gold = 300; p.wood = 200;
        std::fill(p.expl.begin(), p.expl.end(), 0);
        for (int a = 0; a <= MAX_PLAYERS; a++) { p.allied[a] = p.pending[a] = false; pl[a].allied[id] = pl[a].pending[id] = false; }
        IV s = slots[id - 1];
        th[id] = make(K_TownHall, uint8_t(id), s.x + .5f, s.y + .5f);
        reveal(id, s.x, s.y, KINFO[K_TownHall].vision);
    }

    // ---------- pathfinding (A*, 8-dir, no corner cutting) ----------
    bool findPath(IV s, IV g, std::vector<Pos>& out) {
        out.clear();
        if (s.x == g.x && s.y == g.y) return true;
        if (!walk(g.x, g.y) || !walk(s.x, s.y)) return false;
        std::vector<float> gs(N, 1e9f);
        std::vector<int> par(N, -1);
        std::vector<uint8_t> closed(N, 0);
        using QE = std::pair<float, int>;
        std::priority_queue<QE, std::vector<QE>, std::greater<>> q;
        auto h = [&](int x, int y) {
            float dx = float(std::abs(x - g.x)), dy = float(std::abs(y - g.y));
            return dx + dy - 0.586f * std::min(dx, dy);
        };
        gs[ix(s.x, s.y)] = 0;
        q.push({h(s.x, s.y), ix(s.x, s.y)});
        while (!q.empty()) {
            int i = q.top().second;
            q.pop();
            if (closed[i]) continue;
            closed[i] = 1;
            int x = i % MAP_W, y = i / MAP_W;
            if (x == g.x && y == g.y) break;
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++) {
                    if (!dx && !dy) continue;
                    int nx = x + dx, ny = y + dy;
                    if (!walk(nx, ny)) continue;
                    if (dx && dy && (!walk(x + dx, y) || !walk(x, y + dy))) continue;
                    int n = ix(nx, ny);
                    float ng = gs[i] + (dx && dy ? 1.414f : 1.f) + (terrain[n] == T_Forest ? .5f : 0.f);
                    if (ng < gs[n]) { gs[n] = ng; par[n] = i; q.push({ng + h(nx, ny), n}); }
                }
        }
        int gi = ix(g.x, g.y), si = ix(s.x, s.y);
        if (par[gi] < 0) return false;
        for (int i = gi; i != si; i = par[i]) out.push_back({i % MAP_W + .5f, i / MAP_W + .5f});
        std::reverse(out.begin(), out.end());
        return true;
    }

    bool nearestWalkable(IV t, IV& out) {
        if (walk(t.x, t.y)) { out = t; return true; }
        for (int r = 1; r <= 6; r++)
            for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++)
                    if (std::max(std::abs(dx), std::abs(dy)) == r && walk(t.x + dx, t.y + dy)) { out = {t.x + dx, t.y + dy}; return true; }
        return false;
    }

    // ---------- spatial queries ----------
    void rebuildGrid() {
        for (auto& c : grid) c.clear();
        reg.view<Pos, Mover>().each([&](entt::entity e, const Pos& p, const Mover&) {
            int gx = std::clamp(int(p.x) / GC, 0, GW - 1), gy = std::clamp(int(p.y) / GC, 0, GH - 1);
            grid[gy * GW + gx].push_back(e);
        });
    }
    template <class F> void queryUnits(float cx, float cy, float r, F fn) {
        int x0 = std::clamp(int(cx - r) / GC, 0, GW - 1), x1 = std::clamp(int(cx + r) / GC, 0, GW - 1);
        int y0 = std::clamp(int(cy - r) / GC, 0, GH - 1), y1 = std::clamp(int(cy + r) / GC, 0, GH - 1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                for (auto e : grid[y * GW + x]) fn(e);
    }

    // ---------- events ----------
    void onTH(const THDestroyed& ev) {
        if (!reg.valid(ev.e)) return;
        int victim = reg.get<Owner>(ev.e).id, killer = ev.killer;
        if (victim == 0) {
            Pos p = reg.get<Pos>(ev.e);
            occ[ix(int(p.x), int(p.y))] = 0;
            reg.destroy(ev.e);
            if (killer > 0 && pl[killer].active) {
                pl[killer].gold += 150; pl[killer].wood += 100;
                say(killer, "Neutral camp destroyed! +150 gold, +100 wood.");
            }
            return;
        }
        if (killer > 0 && killer != victim && pl[killer].active) {
            int g = int(pl[victim].gold * .5f), w = int(pl[victim].wood * .5f);
            pl[killer].gold += float(g); pl[killer].wood += float(w);
            say(killer, "You destroyed Player " + std::to_string(victim) + "'s Town Hall! Loot: " + std::to_string(g) + " gold, " + std::to_string(w) + " wood.");
        }
        removeOwner(victim);
        resetPlayer(victim);
        say(victim, "Your Town Hall was destroyed! You start over with a new base.");
        std::printf("Player %d was defeated by %d\n", victim, killer);
    }

    void onConnect(ENetPeer* peer) {
        int id = 0;
        for (int i = 1; i <= MAX_PLAYERS && !id; i++) if (!pl[i].active) id = i;
        if (!id) { enet_peer_disconnect_now(peer, 0); return; }
        peer->data = reinterpret_cast<void*>(uintptr_t(id));
        pl[id].active = true;
        pl[id].peer = peer;
        resetPlayer(id);
        IV s = slots[id - 1];
        Writer w;
        w.put<uint8_t>(S_Welcome).put<uint8_t>(uint8_t(id)).put<uint16_t>(MAP_W).put<uint16_t>(MAP_H).put<int16_t>(int16_t(s.x)).put<int16_t>(int16_t(s.y));
        w.bytes(terrain.data(), terrain.size());
        sendPacket(peer, w);
        sendFog(id);
        say(id, "Welcome! Train a Scout (3) and send it into the clouds. Right-click for commands.");
        std::printf("Player %d joined\n", id);
    }

    void onDisconnect(ENetPeer* peer) {
        int id = int(reinterpret_cast<uintptr_t>(peer->data));
        peer->data = nullptr;
        if (id < 1 || id > MAX_PLAYERS) return;
        removeOwner(id);
        pl[id] = Player{};
        pl[id].expl.assign(N, 0);
        for (int a = 0; a <= MAX_PLAYERS; a++) pl[a].allied[id] = pl[a].pending[id] = false;
        th[id] = entt::null;
        std::printf("Player %d left\n", id);
    }

    int unitCount(int id) {
        int n = 0;
        reg.view<Owner, Mover>().each([&](const Owner& o, const Mover&) { if (o.id == id) n++; });
        return n;
    }

    // ---------- commands ----------
    void onPacket(ENetPeer* peer, const ENetPacket* pk) {
        int id = int(reinterpret_cast<uintptr_t>(peer->data));
        if (id < 1) return;
        Reader r{pk->data, pk->data + pk->dataLength};
        Player& p = pl[id];
        auto readUnits = [&](bool noScouts) {
            std::vector<entt::entity> us;
            int n = r.get<uint8_t>();
            for (int i = 0; i < n; i++) {
                auto e = static_cast<entt::entity>(r.get<uint32_t>());
                if (reg.valid(e) && reg.all_of<Owner, Mover, Health>(e) && reg.get<Owner>(e).id == id && !(noScouts && reg.get<Health>(e).kind == K_Scout)) us.push_back(e);
            }
            return us;
        };
        switch (r.get<uint8_t>()) {
        case C_Build: {
            auto k = Kind(r.get<uint8_t>());
            int tx = r.get<int16_t>(), ty = r.get<int16_t>();
            if (!r.ok || (k != K_GoldMine && k != K_Lumber) || !inb(tx, ty)) return;
            const KindInfo& ki = KINFO[k];
            if (p.gold < float(ki.gold) || p.wood < float(ki.wood)) { say(id, "Not enough resources."); return; }
            if (!vision(id)[ix(tx, ty)]) { say(id, "You cannot build in the clouds."); return; }
            uint8_t t = terrain[ix(tx, ty)];
            bool okT = k == K_GoldMine ? (t == T_Clearing || t == T_Mountain) : (t == T_Clearing || t == T_Forest);
            if (!okT) { say(id, k == K_GoldMine ? "Gold Mines need clearing or mountain (x2 yield)." : "Lumber Camps need clearing or forest (x2 yield)."); return; }
            if (occ[ix(tx, ty)]) { say(id, "That tile is occupied."); return; }
            if (!reg.valid(th[id])) return;
            Pos tp = reg.get<Pos>(th[id]);
            if (std::max(std::abs(tx - int(tp.x)), std::abs(ty - int(tp.y))) > BUILD_RADIUS) { say(id, "Too far from your Town Hall."); return; }
            p.gold -= float(ki.gold); p.wood -= float(ki.wood);
            make(k, uint8_t(id), tx + .5f, ty + .5f);
            break;
        }
        case C_Train: {
            auto k = Kind(r.get<uint8_t>());
            if (!r.ok || k < K_Scout || k >= K_Count || !reg.valid(th[id])) return;
            const KindInfo& ki = KINFO[k];
            if (p.gold < float(ki.gold) || p.wood < float(ki.wood)) { say(id, "Not enough resources."); return; }
            if (unitCount(id) >= UNIT_CAP) { say(id, "Army cap reached."); return; }
            p.gold -= float(ki.gold); p.wood -= float(ki.wood);
            Pos tp = reg.get<Pos>(th[id]);
            float a = float(rng() % 628) / 100.f;
            make(k, uint8_t(id), tp.x + std::cos(a) * 1.6f, tp.y + std::sin(a) * 1.6f);
            break;
        }
        case C_Move: {
            auto us = readUnits(false);
            int tx = r.get<int16_t>(), ty = r.get<int16_t>();
            if (!r.ok || !inb(tx, ty)) return;
            IV g{tx, ty};
            bool ground = false;
            for (auto e : us) ground |= reg.get<Health>(e).kind != K_Scout;
            if (ground && !nearestWalkable({tx, ty}, g)) { say(id, "Ground units cannot go there."); return; }
            int fail = 0;
            for (auto e : us) {
                reg.remove<Attack>(e);
                auto& m = reg.get<Mover>(e);
                Pos pos = reg.get<Pos>(e);
                float jx = (float(hash2(int(entt::to_integral(e)), 7, 1) & 255) / 255.f - .5f) * .6f, jy = (float(hash2(int(entt::to_integral(e)), 8, 1) & 255) / 255.f - .5f) * .6f;
                if (reg.get<Health>(e).kind == K_Scout) { m.path = {{tx + .5f + jx, ty + .5f + jy}}; m.i = 0; continue; }
                std::vector<Pos> path;
                if (!findPath({int(pos.x), int(pos.y)}, g, path)) { fail++; continue; }
                if (!path.empty()) { path.back().x += jx; path.back().y += jy; }
                m.path = std::move(path);
                m.i = 0;
            }
            if (fail) say(id, "Some units found no path.");
            break;
        }
        case C_Attack: {
            auto us = readUnits(true);
            auto tg = static_cast<entt::entity>(r.get<uint32_t>());
            if (!r.ok || us.empty() || !reg.valid(tg) || !reg.all_of<Owner, Health, Pos>(tg)) return;
            int to = reg.get<Owner>(tg).id;
            if (to == id || reg.get<Health>(tg).kind != K_TownHall) return;
            if (allied(id, to)) breakAlliance(id, to);
            Pos tp = reg.get<Pos>(tg);
            int fail = 0;
            for (auto e : us) {
                Pos pos = reg.get<Pos>(e);
                std::vector<Pos> path;
                if (!findPath({int(pos.x), int(pos.y)}, {int(tp.x), int(tp.y)}, path)) { fail++; continue; }
                auto& m = reg.get<Mover>(e);
                m.path = std::move(path);
                m.i = 0;
                reg.emplace_or_replace<Attack>(e, Attack{tg});
            }
            if (fail) say(id, "Some units cannot reach the target.");
            if (to > 0) say(to, "Player " + std::to_string(id) + " is attacking your Town Hall!");
            break;
        }
        case C_Ally: {
            int to = r.get<uint8_t>();
            if (!r.ok || to == id || to > MAX_PLAYERS || (to > 0 && !pl[to].active)) return;
            if (p.allied[to]) { say(id, "Already allied."); return; }
            if (to == 0 || pl[to].pending[id]) {
                p.allied[to] = pl[to].allied[id] = true;
                p.pending[to] = pl[to].pending[id] = false;
                if (to == 0) say(id, "Neutral camps are now your allies: shared vision, they will not be attacked.");
                else { say(id, "You are now allied with Player " + std::to_string(to) + " (shared vision)."); say(to, "Player " + std::to_string(id) + " accepted the alliance."); }
            } else {
                p.pending[to] = true;
                say(id, "Alliance proposed to Player " + std::to_string(to) + ".");
                say(to, "Player " + std::to_string(id) + " proposes an alliance. Right-click their Town Hall and choose Ally.");
            }
            break;
        }
        default: break;
        }
    }

    // ---------- simulation ----------
    void update(float dt) {
        tick++;
        rebuildGrid();
        std::vector<entt::entity> drop, dead;

        // unit attacks on Town Halls
        reg.view<Pos, Mover, Health, Owner, Attack>().each([&](entt::entity e, const Pos& p, Mover& m, const Health& h, const Owner& o, const Attack& a) {
            if (!reg.valid(a.target) || !reg.all_of<Pos, Health, Owner>(a.target)) { drop.push_back(e); return; }
            const Pos& tp = reg.get<Pos>(a.target);
            Health& tgt = reg.get<Health>(a.target);
            int to = reg.get<Owner>(a.target).id;
            if (allied(o.id, to)) { drop.push_back(e); return; }
            if (std::hypot(tp.x - p.x, tp.y - p.y) <= 1.4f) {
                m.path.clear(); m.i = 0;
                tgt.hp -= KINFO[h.kind].dps * dt;
                tgt.lastHit = o.id;
                if (tgt.hp <= 0) disp.enqueue(THDestroyed{a.target, o.id});
            } else if (m.path.empty()) {
                std::vector<Pos> path;
                if (findPath({int(p.x), int(p.y)}, {int(tp.x), int(tp.y)}, path)) { m.path = std::move(path); m.i = 0; }
                else drop.push_back(e);
            }
        });
        for (auto e : drop) reg.remove<Attack>(e);

        // Town Hall defences (spatial query for the closest hostile unit)
        reg.view<Pos, Health, Owner>().each([&](const Pos& p, const Health& h, const Owner& o) {
            if (h.kind != K_TownHall) return;
            entt::entity best = entt::null;
            float bd = 4.5f;
            queryUnits(p.x, p.y, 4.5f, [&](entt::entity u) {
                if (allied(o.id, reg.get<Owner>(u).id)) return;
                const Pos& up = reg.get<Pos>(u);
                float d = std::hypot(up.x - p.x, up.y - p.y);
                if (d < bd) { bd = d; best = u; }
            });
            if (best == entt::null) return;
            Health& uh = reg.get<Health>(best);
            uh.hp -= KINFO[K_TownHall].dps * dt;
            if (uh.hp <= 0) dead.push_back(best);
        });
        std::sort(dead.begin(), dead.end());
        dead.erase(std::unique(dead.begin(), dead.end()), dead.end());
        for (auto e : dead) if (reg.valid(e)) reg.destroy(e);
        disp.update();                                          // resolves destroyed Town Halls

        // movement
        reg.view<Pos, Mover, Health>().each([&](Pos& p, Mover& m, const Health& h) {
            if (m.i >= m.path.size()) return;
            float step = KINFO[h.kind].speed * dt;
            Pos t = m.path[m.i];
            float dx = t.x - p.x, dy = t.y - p.y, d = std::hypot(dx, dy);
            if (d <= step) { p = t; if (++m.i >= m.path.size()) { m.path.clear(); m.i = 0; } }
            else { p.x += dx / d * step; p.y += dy / d * step; }
        });

        // economy
        reg.view<Pos, Health, Owner>().each([&](const Pos& p, const Health& h, const Owner& o) {
            if (o.id == 0 || !pl[o.id].active) return;
            uint8_t t = terrain[ix(int(p.x), int(p.y))];
            Player& pp = pl[o.id];
            if (h.kind == K_TownHall) { pp.gold += 1.f * dt; pp.wood += .5f * dt; }
            else if (h.kind == K_GoldMine) pp.gold += 2.f * (t == T_Mountain ? 2.f : 1.f) * dt;
            else if (h.kind == K_Lumber) pp.wood += 2.f * (t == T_Forest ? 2.f : 1.f) * dt;
        });

        // fog of war: everything owned reveals around itself
        if (tick % 4 == 0)
            reg.view<Pos, Health, Owner>().each([&](const Pos& p, const Health& h, const Owner& o) {
                if (o.id) reveal(o.id, int(p.x), int(p.y), KINFO[h.kind].vision);
            });

        for (int id = 1; id <= MAX_PLAYERS; id++) {
            if (!pl[id].active) continue;
            if (tick % 2 == 0) sendState(id);
            if (tick % 10 == 0) sendFog(id);
        }
    }

    void sendState(int id) {
        Player& p = pl[id];
        auto vis = vision(id);
        Writer w;
        w.put<uint8_t>(S_State).put<float>(p.gold).put<float>(p.wood);
        uint16_t am = 0, pm = 0;
        for (int i = 0; i <= MAX_PLAYERS; i++) { if (p.allied[i]) am |= uint16_t(1 << i); if (pl[i].pending[id]) pm |= uint16_t(1 << i); }
        w.put<uint16_t>(am).put<uint16_t>(pm);
        size_t cp = w.b.size();
        w.put<uint16_t>(0);
        uint16_t n = 0;
        reg.view<Pos, Health, Owner>().each([&](entt::entity e, const Pos& ps, const Health& h, const Owner& o) {
            int tx = std::clamp(int(ps.x), 0, MAP_W - 1), ty = std::clamp(int(ps.y), 0, MAP_H - 1);
            if (!(allied(id, o.id) || vis[ix(tx, ty)])) return;
            w.put<uint32_t>(entt::to_integral(e)).put<uint8_t>(h.kind).put<uint8_t>(o.id).put<float>(ps.x).put<float>(ps.y);
            w.put<uint16_t>(uint16_t(std::max(0.f, h.hp))).put<uint16_t>(uint16_t(h.maxhp));
            n++;
        });
        std::memcpy(&w.b[cp], &n, 2);
        sendPacket(p.peer, w);
    }

    void sendFog(int id) {
        auto vis = vision(id);
        Writer w;
        w.put<uint8_t>(S_Fog);
        std::vector<uint8_t> bits((N + 7) / 8, 0);
        for (int i = 0; i < N; i++) if (vis[i]) bits[i >> 3] |= uint8_t(1 << (i & 7));
        w.bytes(bits.data(), bits.size());
        sendPacket(pl[id].peer, w);
    }

    void run() {
        using clk = std::chrono::steady_clock;
        auto next = clk::now();
        const auto step = std::chrono::milliseconds(1000 / TICK_HZ);
        for (;;) {
            ENetEvent ev;
            while (enet_host_service(host, &ev, 0) > 0) {
                switch (ev.type) {
                case ENET_EVENT_TYPE_CONNECT: onConnect(ev.peer); break;
                case ENET_EVENT_TYPE_RECEIVE: onPacket(ev.peer, ev.packet); enet_packet_destroy(ev.packet); break;
                case ENET_EVENT_TYPE_DISCONNECT: onDisconnect(ev.peer); break;
                default: break;
                }
            }
            update(1.f / TICK_HZ);
            enet_host_flush(host);
            next += step;
            auto now = clk::now();
            if (next < now - std::chrono::seconds(1)) next = now;
            std::this_thread::sleep_until(next);
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : DEFAULT_PORT;
    uint32_t seed = argc > 2 ? uint32_t(std::strtoul(argv[2], nullptr, 10)) : std::random_device{}();
    if (enet_initialize() != 0) { std::puts("enet_initialize failed"); return 1; }
    {
        Server s(uint16_t(port), seed);
        s.run();
    }
    enet_deinitialize();
}
