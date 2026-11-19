#include "systems/ClearingSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
struct WorldAdapter {                                        // adapter for the shared goldNear()
    World& s;
    int biome(int x, int y) { return s.biome(x, y); }
    bool cleared(int x, int y) { return s.cleared.count(tkey(x, y)) > 0; }
    uint32_t seedValue() { return s.seed; }
};
}  // namespace

std::vector<IV> ClearingSystem::blockedTiles(Kind k, int tx, int ty) {
    std::vector<IV> v;
    for (int y = ty; y < ty + KINFO[k].fh; y++)
        for (int x = tx; x < tx + KINFO[k].fw; x++)
            if (!world.isCleared(x, y) && featureBlocks(world.featOf(x, y))) v.push_back({x, y});
    return v;
}
bool ClearingSystem::worldBuildOk(int id, Kind k, int tx, int ty) {           // hook for C_Build
    auto v = blockedTiles(k, tx, ty);
    if (v.empty()) return true;
    int trees = 0, rocks = 0, gold = 0;
    for (auto t : v) { uint8_t f = world.featOf(t.x, t.y); (f == F_Rock ? rocks : (f == F_Gold ? gold : trees))++; }
    std::string s = "Cannot build here: clear";
    if (trees) s += " " + std::to_string(trees) + " tree" + (trees > 1 ? "s" : "");
    if (rocks) s += (trees ? "," : "") + std::string(" ") + std::to_string(rocks) + " rock" + (rocks > 1 ? "s" : "");
    if (gold) s += ((trees || rocks) ? "," : "") + std::string(" ") + std::to_string(gold) + " gold vein" + (gold > 1 ? "s" : "");
    world.say(id, s + " first (click the blocked tiles to start clearing).");
    return false;
}
bool ClearingSystem::inClearReach(int id, int x, int y) {                     // clearing is allowed anywhere the player could place a building
    bool ok = false;
    reg.view<Info, Building, Owner>().each([&](const Info& i, const Building& b, const Owner& o) {
        if (ok || (i.kind != K_TownHall && i.kind != K_Outpost) || !world.allied(id, o.id) || b.progress < 1.f) return;
        Pos c = footCenter(i.kind, b.tx, b.ty);
        float reach = i.kind == K_TownHall ? 60.f : 34.f;
        if (o.id != id) reach = std::max(reach, float(CLAN_TOWN_RADIUS) + 4.f);        // a homeless clan member may build (and so clear) anywhere around the clan halls
        if (std::max(std::abs(c.x - float(x)), std::abs(c.y - float(y))) <= reach) ok = true;
    });
    return ok;
}
void ClearingSystem::onClear(int id, Reader& r) {
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
        if (clearJobs.count(key) || world.isCleared(t.x, t.y) || !world.explored(id, t.x, t.y) || world.occ.count(key)) continue;
        uint8_t f = world.featOf(t.x, t.y);
        if (!featureBlocks(f) || !inClearReach(id, t.x, t.y)) continue;
        if (mine + int(ok.size()) >= 60) break;
        cost[R_Wood] += float(clearCostWood(f));
        ok.push_back(t);
    }
    if (ok.empty()) { world.say(id, "Nothing to clear there."); return; }
    if (!world.afford(id, cost)) { world.say(id, "Not enough wood to clear that (" + std::to_string(int(cost[R_Wood])) + " needed)."); return; }
    world.spend(id, cost);
    for (auto t : ok) {
        uint8_t f = world.featOf(t.x, t.y);
        clearJobs[tkey(t.x, t.y)] = ClearJob{id, f, 0.f, clearSeconds(f)};
    }
    world.say(id, "Clearing " + std::to_string(ok.size()) + " tile" + (ok.size() > 1 ? "s" : "") + " (-" + std::to_string(int(cost[R_Wood])) + " wood).");
}
void ClearingSystem::sendDrop(Pos p, int res, int amt, int credited) {         // visible bouncing "+N" drop for everyone who can see the spot
    Writer w;
    w.put<uint8_t>(S_Drop).put<float>(p.x).put<float>(p.y).put<uint8_t>(uint8_t(res)).put<uint16_t>(uint16_t(std::clamp(amt, 0, 65000)));
    for (int id = 1; id <= MAX_PLAYERS; id++) {
        if (!world.pl[id].online) continue;
        if (id != credited) {                                                   // vision sources are rebuilt at most once per tick per player
            if (dropSrcTick[id] != world.tick) { dropSrc[id] = world.sourcesFor(id); dropSrcTick[id] = world.tick; }
            if (!seen(dropSrc[id], p.x, p.y, 2.f)) continue;
        }
        world.net.send(world.pl[id].conn, w);
    }
}
void ClearingSystem::creditDrop(int player, Pos p, int res, int amt) {
    if (isPlayer(player) && world.pl[player].used) world.pl[player].res[res] += float(amt);
    sendDrop(p, res, amt, player);
}
void ClearingSystem::sendClearedList(int id, const std::vector<int64_t>& tiles, bool full) {
    size_t i = 0;
    do {                                                                                                   // batches keep packets small
        size_t n = std::min<size_t>(3000, tiles.size() - i);
        Writer w;
        w.put<uint8_t>(S_Cleared).put<uint8_t>(full ? (i == 0 ? 0 : 2) : 1).put<uint32_t>(uint32_t(n));   // 0 = replace, 2 = append, 1 = delta
        for (size_t k = 0; k < n; k++) w.put<int32_t>(keyCx(tiles[i + k])).put<int32_t>(keyCy(tiles[i + k]));
        world.net.send(world.pl[id].conn, w);
        i += n;
    } while (i < tiles.size());
}
void ClearingSystem::sendClearedFull(int id) {                                 // hook for onHello
    std::vector<int64_t> v(world.cleared.begin(), world.cleared.end());
    sendClearedList(id, v, true);
}
void ClearingSystem::clearTick(float dt) {
    std::vector<int64_t> done;
    for (auto& [key, j] : clearJobs) { j.t += dt; if (j.t >= j.total) done.push_back(key); }
    for (auto key : done) {
        ClearJob j = clearJobs[key];
        clearJobs.erase(key);
        int x = keyCx(key), y = keyCy(key);
        world.markCleared(x, y);
        Pos p{float(x) + .5f, float(y) + .6f};
        creditDrop(j.owner, p, dropRes(j.feat), dropAmount(j.feat));
        world.fx(E_Dust, p.x, p.y);
    }
    if (!world.clearedNew.empty()) {
        for (int id = 1; id <= MAX_PLAYERS; id++) if (world.pl[id].online) sendClearedList(id, world.clearedNew, false);
        world.clearedNew.clear();
    }
    if (world.tick % 4 == 0 && (!clearJobs.empty() || jobsWereSent)) {
        jobsWereSent = !clearJobs.empty();
        Writer w;
        w.put<uint8_t>(S_ClearJobs).put<uint16_t>(uint16_t(std::min<size_t>(clearJobs.size(), 400)));
        size_t n = 0;
        for (auto& [key, j] : clearJobs) {
            if (n++ >= 400) break;
            w.put<int32_t>(keyCx(key)).put<int32_t>(keyCy(key)).put<uint8_t>(uint8_t(std::clamp(j.t / j.total, 0.f, 1.f) * 100.f));
        }
        for (int id = 1; id <= MAX_PLAYERS; id++) if (world.pl[id].online) world.net.send(world.pl[id].conn, w);
    }
}

// ---------------------------------------------------------------- gold deposits boost mines
float ClearingSystem::mineMul(entt::entity e, Kind k, Pos c) {                  // output multiplier from nearby gold deposits (cached per clear-version)
    if (k != K_Mine) return 1.f;
    uint32_t uid = reg.get<Uid>(e).id;
    auto it = mineCache.find(uid);
    if (it != mineCache.end() && it->second.first == world.clearVer) return it->second.second;
    WorldAdapter wa{world};
    float m = 1.f + goldBonusOf(goldNear(wa, c));
    mineCache[uid] = {world.clearVer, m};
    static const bool logIt = std::getenv("BW_WORLDLOG") != nullptr;           // test hook
    if (logIt) std::printf("[world] mine %u at %.1f,%.1f: %d gold deposits -> output x%.2f\n", uid, c.x, c.y, goldNear(wa, c), m);
    return m;
}
