// Headless scenario bot for the world / environment / NPC-life features.
// Usage: world_bot <uuid> <name> <mode> [port]
//   mode 1 = clearing: build is rejected over features, C_Clear, jobs + drops + credit, build succeeds afterwards
//   mode 2 = hunting: free pawns hunt sheep and deliver meat (watch meat and carrying pawns)
//   mode 3 = patrols + loot + retaliation (run the server with BW_PATROLNOW=1 BW_SEEALL=1 BW_REVEAL=90 BW_ARMY=10)
//   mode 4 = rejoin: print the number of cleared tiles received on join
//   mode 5 = mine/gold bonus: place a mine at the best and at the worst spot (server prints with BW_WORLDLOG=1)
#include "shared.hpp"
#include <chrono>
#include <cstdio>
#include <map>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>

struct E { uint32_t id; Kind k; int o; float x, y, hp, mhp; int st, aux, aim, prog, stock; };
std::unordered_map<int64_t, std::array<uint8_t, 256>> terr;
std::unordered_map<int64_t, std::array<uint8_t, 32>> fog;
std::map<uint32_t, E> ents;
std::unordered_set<int64_t> clearedSet;
std::map<int64_t, int> jobs;
float res[4];
int me = 0, dropMsgs = 0, dropTotal[4]{}, deltaMsgs = 0, fullMsgs = 0, jobMsgs = 0, maxJobPct = 0;
std::string lastText;
uint32_t seedv = 0;

void pk(const uint8_t* d, size_t n) {
    Reader r{d, d + n};
    switch (r.get<uint8_t>()) {
    case S_Welcome: me = r.get<uint8_t>(); seedv = r.get<uint32_t>(); r.get<float>(); r.get<float>(); break;
    case S_State: {
        for (float& v : res) v = r.get<float>();
        for (int f = 0; f < 5; f++) r.get<int8_t>();
        for (int f = 0; f < 5; f++) r.get<uint8_t>();
        int c = r.get<uint16_t>();
        ents.clear();
        for (int i = 0; i < c; i++) {
            E e;
            e.id = r.get<uint32_t>(); e.k = Kind(r.get<uint8_t>()); e.o = r.get<uint8_t>(); e.x = r.get<float>(); e.y = r.get<float>();
            e.hp = r.get<uint16_t>(); e.mhp = r.get<uint16_t>(); e.st = r.get<uint8_t>(); e.aux = r.get<uint8_t>(); e.aim = r.get<uint8_t>(); e.prog = r.get<uint8_t>(); e.stock = r.get<uint16_t>();
            ents[e.id] = e;
        }
        break;
    }
    case S_Fog: {
        int nt = r.get<uint16_t>();
        for (int i = 0; i < nt; i++) { int cx = r.get<int32_t>(), cy = r.get<int32_t>(); auto b = r.bytes(256); std::array<uint8_t, 256> a; memcpy(a.data(), b, 256); terr[chunkKey(cx, cy)] = a; }
        int nf = r.get<uint16_t>();
        for (int i = 0; i < nf; i++) { int cx = r.get<int32_t>(), cy = r.get<int32_t>(); auto b = r.bytes(32); std::array<uint8_t, 32> a; memcpy(a.data(), b, 32); fog[chunkKey(cx, cy)] = a; }
        break;
    }
    case S_Text: lastText = r.str(); printf("  [text] %s\n", lastText.c_str()); break;
    case S_Cleared: {
        int mode = r.get<uint8_t>();
        uint32_t cnt = r.get<uint32_t>();
        if (mode == 0) { clearedSet.clear(); fullMsgs++; } else if (mode == 1) deltaMsgs++;
        for (uint32_t i = 0; i < cnt; i++) { int x = r.get<int32_t>(), y = r.get<int32_t>(); clearedSet.insert(chunkKey(x, y)); }
        break;
    }
    case S_ClearJobs: {
        int cnt = r.get<uint16_t>();
        jobs.clear();
        for (int i = 0; i < cnt; i++) { int x = r.get<int32_t>(), y = r.get<int32_t>(); int p = r.get<uint8_t>(); jobs[chunkKey(x, y)] = p; maxJobPct = std::max(maxJobPct, p); }
        jobMsgs++;
        break;
    }
    case S_Drop: {
        float x = r.get<float>(), y = r.get<float>();
        int rs = r.get<uint8_t>(), amt = r.get<uint16_t>();
        dropMsgs++;
        if (rs < 4) dropTotal[rs] += amt;
        printf("  [drop] %s +%d at %.1f,%.1f\n", RES_NAME[rs & 3], amt, x, y);
        break;
    }
    default: break;
    }
}
struct PW {
    int biome(int x, int y) { auto it = terr.find(chunkKey(chunkOf(x), chunkOf(y))); return it == terr.end() ? INVALID_BIOME : it->second[localIdx(x, y)]; }
    bool explored(int x, int y) { auto it = fog.find(chunkKey(chunkOf(x), chunkOf(y))); if (it == fog.end()) return false; int i = localIdx(x, y); return it->second[i >> 3] >> (i & 7) & 1; }
    bool occupied(int x, int y) {
        for (auto& [i, e] : ents) { if (!isBuilding(e.k)) continue; int tx = std::lround(e.x - KINFO[e.k].fw * .5f), ty = std::lround(e.y - KINFO[e.k].fh * .5f); if (x >= tx && x < tx + KINFO[e.k].fw && y >= ty && y < ty + KINFO[e.k].fh) return true; }
        return false;
    }
    template <class F> void anchors(F f) { for (auto& [i, e] : ents) if (e.k == K_TownHall || e.k == K_Outpost) f(Pos{e.x, e.y}, e.k, e.o == me, false); }
    bool cleared(int x, int y) { return clearedSet.count(chunkKey(x, y)) > 0; }
    uint32_t seedValue() { return seedv; }
};
ENetHost* host;
ENetPeer* peer;
void pump(int ms) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        ENetEvent ev;
        while (enet_host_service(host, &ev, 10) > 0) if (ev.type == ENET_EVENT_TYPE_RECEIVE) { pk(ev.packet->data, ev.packet->dataLength); enet_packet_destroy(ev.packet); }
    }
}
void snd(const Writer& w) { sendPacket(peer, w); enet_host_flush(host); }
void build(Kind k, int tx, int ty) { Writer b; b.put<uint8_t>(C_Build).put<uint8_t>(k).put<int32_t>(tx).put<int32_t>(ty); snd(b); }
void clearTiles(const std::vector<IV>& v) { Writer w; w.put<uint8_t>(C_Clear).put<uint8_t>(uint8_t(v.size())); for (auto t : v) w.put<int32_t>(t.x).put<int32_t>(t.y); snd(w); }
int count(Kind k, int owner) { int n = 0; for (auto& [i, e] : ents) if (e.k == k && e.o == owner) n++; return n; }
uint8_t featureOf(PW& w, int x, int y) {
    int b = w.biome(x, y);
    if (!isLand(b) || isCliffTile(b, w.biome(x, y - 1))) return F_None;
    return featureAt(x, y, b, seedv);
}
std::vector<IV> blocked(PW& w, Kind k, int tx, int ty) {
    std::vector<IV> v;
    for (int y = ty; y < ty + KINFO[k].fh; y++) for (int x = tx; x < tx + KINFO[k].fw; x++) if (!w.cleared(x, y) && featureBlocks(featureOf(w, x, y))) v.push_back({x, y});
    return v;
}
int main(int argc, char** argv) {
    std::string uuid = argc > 1 ? argv[1] : "world-bot-uuid-01", name = argc > 2 ? argv[2] : "WBot";
    int mode = argc > 3 ? atoi(argv[3]) : 1;
    setvbuf(stdout, nullptr, _IONBF, 0);
    enet_initialize();
    host = enet_host_create(nullptr, 1, 2, 0, 0);
    ENetAddress a;
    enet_address_set_host(&a, "127.0.0.1");
    a.port = argc > 4 ? uint16_t(atoi(argv[4])) : 7801;
    peer = enet_host_connect(host, &a, 2, 0);
    pump(500);
    Writer h;
    h.put<uint8_t>(C_Hello); h.str(uuid); h.str(name); h.put<uint64_t>(0); h.put<uint16_t>(0);
    snd(h);
    pump(2500);
    E te{};
    for (auto& [i, e] : ents) if (e.k == K_TownHall && e.o == me) te = e;
    printf("joined as %d, TH at %.1f,%.1f, res W%.0f G%.0f S%.0f M%.0f, cleared tiles known: %zu (full msgs %d)\n", me, te.x, te.y, res[0], res[1], res[2], res[3], clearedSet.size(), fullMsgs);
    PW w;
    if (mode == 4) { enet_peer_disconnect(peer, 0); pump(300); return 0; }

    if (mode == 1) {
        // find a 2x2 outpost-free spot (Storage House, 2x2) near the TH whose footprint contains blocking features
        int bx = 0, by = 0;
        size_t most = 0;
        for (int dy = -12; dy <= 12; dy++) for (int dx = -12; dx <= 12; dx++) {
            int tx = int(te.x) + dx, ty = int(te.y) + dy;
            if (checkPlace(w, K_Storage, tx, ty) != P_OK) continue;
            size_t n = blocked(w, K_Storage, tx, ty).size();
            if (n > most) { most = n; bx = tx; by = ty; }
        }
        auto v = blocked(w, K_Storage, bx, by);
        printf("test footprint %d,%d has %zu blocking features\n", bx, by, v.size());
        if (v.empty()) { puts("RESULT no features found near spawn (try another seed)"); return 0; }
        float before[4]; memcpy(before, res, 16);
        int stor0 = count(K_Storage, me);
        build(K_Storage, bx, by); pump(800);
        printf("build over features -> storages %d -> %d (must be unchanged)\n", stor0, count(K_Storage, me));
        clearTiles(v); pump(600);
        printf("after C_Clear: jobs running=%zu wood now %.0f (was %.0f)\n", jobs.size(), res[0], before[0]);
        build(K_Storage, bx, by); pump(500);
        printf("build while clearing -> storages=%d (must be unchanged)\n", count(K_Storage, me));
        pump(7000);
        printf("max job progress seen %d%%, job msgs %d, jobs left %zu, delta msgs %d, drop msgs %d\n", maxJobPct, jobMsgs, jobs.size(), deltaMsgs, dropMsgs);
        printf("drops: wood %d gold %d stone %d meat %d ; resources now W%.0f G%.0f S%.0f M%.0f\n", dropTotal[0], dropTotal[1], dropTotal[2], dropTotal[3], res[0], res[1], res[2], res[3]);
        size_t left = blocked(w, K_Storage, bx, by).size();
        printf("blocking features left: %zu\n", left);
        build(K_Storage, bx, by); pump(800);
        printf("build after clearing -> storages=%d (must be +1) %s\n", count(K_Storage, me), count(K_Storage, me) == stor0 + 1 ? "PASS" : "FAIL");
        printf("cleared tiles known to client: %zu\n", clearedSet.size());
        enet_peer_disconnect(peer, 0); pump(300);
        return 0;
    }
    if (mode == 2) {
        printf("free pawns: %d, warriors %d\n", count(K_Pawn, me), count(K_Warrior, me));
        int carryingMax = 0, last = int(res[3]);
        for (int t = 0; t < 150; t++) {
            pump(1000);
            int carrying = 0;
            for (auto& [i, e] : ents) if (e.o == me && e.k == K_Pawn && e.aux == R_Meat + 1) carrying++;
            carryingMax = std::max(carryingMax, carrying);
            int sheep = count(K_Sheep, 0);
            if (t % 10 == 0 || int(res[3]) != last) { printf("t=%3d meat %.0f carrying=%d sheep visible=%d\n", t, res[3], carrying, sheep); last = int(res[3]); }
        }
        printf("RESULT meat start->end, max carrying pawns %d\n", carryingMax);
        enet_peer_disconnect(peer, 0); pump(300);
        return 0;
    }
    if (mode == 3) {
        // wait for patrols to appear, track them, then send the army at one
        std::map<uint32_t, std::pair<float, float>> first;
        std::set<uint32_t> everSeen;
        float moved = 0;
        for (int t = 0; t < 40; t++) {
            pump(1000);
            for (auto& [i, e] : ents) if (e.o >= 100 && isArmy(e.k)) { everSeen.insert(i); if (!first.count(i)) first[i] = {e.x, e.y}; }
            if (t % 10 == 0) { int n = 0; for (auto& [i, e] : ents) if (e.o >= 100 && isArmy(e.k)) n++; printf("t=%d faction army units visible: %d\n", t, n); }
        }
        for (auto& [i, p0] : first) { auto it = ents.find(i); if (it != ents.end()) moved = std::max(moved, std::hypot(it->second.x - p0.first, it->second.y - p0.second)); }
        printf("faction army units seen: %zu, max displacement of any unit over time: %.1f tiles\n", everSeen.size(), moved);
        // pick the faction unit nearest to my TH and attack with everything
        uint32_t tgt = 0; float bd = 1e9f; int fowner = 0;
        for (auto& [i, e] : ents) if (e.o >= 100 && isArmy(e.k)) { float d = std::hypot(e.x - te.x, e.y - te.y); if (d < bd) { bd = d; tgt = i; fowner = e.o; } }
        if (!tgt) { puts("RESULT no faction army visible; try BW_REVEAL larger / other seed"); return 0; }
        printf("attacking faction unit %u (owner %d) at distance %.0f with %d warriors\n", tgt, fowner, bd, count(K_Warrior, me));
        float lootBefore[4]; memcpy(lootBefore, res, 16);
        Writer atk; std::vector<uint32_t> mine;
        for (auto& [i, e] : ents) if (e.o == me && isArmy(e.k)) mine.push_back(i);
        atk.put<uint8_t>(C_Attack).put<uint8_t>(uint8_t(mine.size())); for (auto i : mine) atk.put<uint32_t>(i); atk.put<uint32_t>(tgt);
        snd(atk);
        bool killed = false; int dropsBefore = dropMsgs;
        for (int t = 0; t < 120 && !killed; t++) { pump(1000); if (!ents.count(tgt)) killed = true; if (t % 10 == 0) { auto it = ents.find(mine.empty() ? 0 : mine[0]); printf("  t=%d my army alive: %d target alive: %d\n", t, count(K_Warrior, me), ents.count(tgt) ? 1 : 0); (void)it; } }
        printf("target killed: %s ; drop effects received: %d ; resources delta W%.0f G%.0f S%.0f M%.0f\n", killed ? "yes" : "no", dropMsgs - dropsBefore, res[0] - lootBefore[0], res[1] - lootBefore[1], res[2] - lootBefore[2], res[3] - lootBefore[3]);
        // now retreat everything to the TH and watch whether remaining faction units come for us
        Writer mv; std::vector<uint32_t> units;
        for (auto& [i, e] : ents) if (e.o == me && !isBuilding(e.k)) units.push_back(i);
        mv.put<uint8_t>(C_Move).put<uint8_t>(uint8_t(units.size())); for (auto i : units) mv.put<uint32_t>(i); mv.put<float>(te.x).put<float>(te.y + 3.f);
        snd(mv);
        for (int t = 0; t < 100; t++) {
            pump(1000);
            if (t % 10 == 0) {
                float dmin = 1e9f; int n = 0;
                for (auto& [i, e] : ents) if (e.o == fowner && isArmy(e.k)) { n++; dmin = std::min(dmin, std::hypot(e.x - te.x, e.y - te.y)); }
                printf("  t=%d faction %d army visible: %d, nearest to my TH: %.1f tiles; my units %d\n", t, fowner, n, dmin, int(units.size()));
            }
        }
        enet_peer_disconnect(peer, 0); pump(300);
        return 0;
    }
    if (mode == 5) {
        int best = -1, worst = 99, bx = 0, by = 0, wx = 0, wy = 0;
        for (int dy = -16; dy <= 16; dy++) for (int dx = -16; dx <= 16; dx++) {
            int tx = int(te.x) + dx, ty = int(te.y) + dy;
            if (checkPlace(w, K_Mine, tx, ty) != P_OK || !blocked(w, K_Mine, tx, ty).empty()) continue;
            int n = goldNear(w, footCenter(K_Mine, tx, ty));
            if (n > best) { best = n; bx = tx; by = ty; }
            if (n < worst) { worst = n; wx = tx; wy = ty; }
        }
        printf("best mine spot %d,%d: %d deposits (x%.2f); worst %d,%d: %d deposits\n", bx, by, best, 1.f + goldBonusOf(best), wx, wy, worst);
        if (best < 0) { puts("RESULT no placeable mine spot without features"); return 0; }
        build(K_Mine, wx, wy); pump(500);
        if (best != worst) { build(K_Mine, bx, by); pump(500); }
        pump(30000);
        for (auto& [i, e] : ents) if (e.o == me && e.k == K_Mine) printf("  mine uid %u at %.1f,%.1f prog %d stock %d\n", i, e.x, e.y, e.prog, e.stock);
        enet_peer_disconnect(peer, 0); pump(300);
    }
}
