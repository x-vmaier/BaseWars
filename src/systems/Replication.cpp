#include "systems/Replication.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void Replication::sendState(int id) {
    Profile& p = world.pl[id];
    auto src = world.sourcesFor(id);
    SrcGrid grid(src);
    Writer w;
    w.put<uint8_t>(S_State);
    for (int r = 0; r < R_Count; r++) w.put<float>(p.res[r]);
    for (int f = 0; f < FACTION_COUNT; f++) w.put<int8_t>(int8_t(std::lround(p.rep[f])));
    for (int k = K_Pawn; k <= K_Monk; k++) w.put<uint8_t>(uint8_t(std::max<int>(1, p.ulevel[k])));
    size_t cp = w.b.size();
    w.put<uint16_t>(0);
    uint16_t n = 0;
    reg.view<Uid, Owner, Info, Health>().each([&](entt::entity e, const Uid& uid, const Owner& o, const Info& in, const Health& h) {
        Pos ps = world.posOf(e);
        static const bool seeAll = std::getenv("BW_SEEALL") != nullptr;      // test hook: show every entity
        if (!seeAll && !world.allied(id, o.id) && !grid.seen(ps.x, ps.y, isBuilding(in.kind) ? 2.f : 0.f) && !((reg.all_of<Convoy>(e) || reg.all_of<Haul>(e)) && s.convoy.spottedForClan(id, uid.id))) return;
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
            } else aux = s.hunt.worldAux(e);                               // [world] hunter pawns carrying meat
        } else {
            const Building& b = reg.get<Building>(e);
            state = world.bflagsOf(e);                                   // BLD_BURNING / BLD_DISABLED / BLD_MOVING
            aux = in.level;
            aim = b.haulPct;
            prog = uint8_t(std::min(1.f, b.progress) * 100.f);
            float tot = 0;
            for (float s : b.stock) tot += s;
            stock = uint16_t(std::min(tot, 65000.f));
        }
        if (!s.naval.shipStateHook(id, e, o.id, in, aux, prog, stock)) return;
        w.put<uint32_t>(uid.id).put<uint8_t>(in.kind).put<uint8_t>(o.id).put<float>(ps.x).put<float>(ps.y);
        w.put<uint16_t>(uint16_t(std::max(0.f, h.hp))).put<uint16_t>(uint16_t(h.maxhp));
        w.put<uint8_t>(state).put<uint8_t>(aux).put<uint8_t>(aim).put<uint8_t>(prog).put<uint16_t>(stock);
        n++;
    });
    std::memcpy(&w.b[cp], &n, 2);
    size_t ep = w.b.size();
    w.put<uint16_t>(0);
    uint16_t ne = 0;
    for (auto& f : world.effects)
        if (grid.seen(f.x, f.y, 3.f) || grid.seen(f.x2, f.y2, 3.f)) { w.put<uint8_t>(f.type).put<float>(f.x).put<float>(f.y).put<float>(f.x2).put<float>(f.y2); ne++; }
    std::memcpy(&w.b[ep], &ne, 2);
    world.net.send(p.conn, w);
}

void Replication::sendFog(int id) {
    Profile& p = world.pl[id];
    std::unordered_map<int64_t, Bits> uni = p.expl;
    for (int a = 1; a <= MAX_PLAYERS; a++)
        if (a != id && world.pl[a].used && world.shares(a, id))
            for (auto& [k, b] : world.pl[a].expl) { Bits& u = uni.try_emplace(k, Bits{}).first->second; for (size_t i = 0; i < 32; i++) u[i] |= b[i]; }
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
        world.biome(cx * CH, cy * CH);
        w.put<int32_t>(cx).put<int32_t>(cy);
        w.bytes(world.terr[k].data(), CH * CH);
    }
    w.put<uint16_t>(uint16_t(fogOut.size()));
    for (auto& [k, b] : fogOut) { w.put<int32_t>(keyCx(k)).put<int32_t>(keyCy(k)); w.bytes(b.data(), 32); }
    world.net.send(p.conn, w);
}

void Replication::sendRoster() {
    Pos thPos[MAX_PLAYERS + 1];
    bool hasTh[MAX_PLAYERS + 1]{};
    reg.view<Info, Building, Owner>().each([&](const Info& i, const Building& b, const Owner& o) {
        if (i.kind == K_TownHall && isPlayer(o.id) && !hasTh[o.id]) { hasTh[o.id] = true; thPos[o.id] = footCenter(i.kind, b.tx, b.ty); }
    });
    for (int id = 1; id <= MAX_PLAYERS; id++) {
        if (!world.pl[id].online) continue;
        Writer w;
        w.put<uint8_t>(S_Roster);
        uint8_t n = 0;
        size_t cp = w.b.size();
        w.put<uint8_t>(0);
        for (int o = 1; o <= MAX_PLAYERS; o++) {
            if (!world.pl[o].used) continue;
            uint8_t flags = uint8_t((world.pl[o].online ? 1 : 0) | (world.pl[id].allied[o] ? 2 : 0) | (world.pl[o].pending[id] ? 4 : 0) | (world.pl[id].pending[o] ? 8 : 0));
            w.put<uint8_t>(uint8_t(o)).put<uint8_t>(flags).put<uint8_t>(world.pl[o].clan);
            w.str(world.pl[o].name);
            bool known = hasTh[o] && (o == id || world.shares(id, o) || world.explored(id, int(std::floor(thPos[o].x)), int(std::floor(thPos[o].y))));
            w.put<uint8_t>(known ? 1 : 0).put<float>(known ? thPos[o].x : 0.f).put<float>(known ? thPos[o].y : 0.f);
            n++;
        }
        w.b[cp] = n;
        uint8_t nc = 0;
        size_t cc = w.b.size();
        w.put<uint8_t>(0);
        for (int c = 1; c <= MAX_CLANS; c++) {
            if (!world.clans[c].used) continue;
            int members = 0;
            for (int o = 1; o <= MAX_PLAYERS; o++) if (world.pl[o].used && world.pl[o].clan == c) members++;
            w.put<uint8_t>(uint8_t(c));
            w.str(world.clans[c].name);
            w.put<uint8_t>(uint8_t(members)).put<uint8_t>(world.clans[c].leader);
            nc++;
        }
        w.b[cc] = nc;
        world.net.send(world.pl[id].conn, w);
    }
}

void Replication::sendMap(int id) {                                          // faction towns the player has discovered
    Writer w;
    w.put<uint8_t>(S_Map);
    uint8_t n = 0;
    size_t cp = w.b.size();
    w.put<uint8_t>(0);
    for (auto& t : world.towns) {
        if (!t.alive || !world.explored(id, t.cx, t.cy)) continue;
        w.put<uint8_t>(t.faction).put<float>(float(t.cx) + .5f).put<float>(float(t.cy) + .5f);
        if (++n == 255) break;
    }
    w.b[cp] = n;
    world.net.send(world.pl[id].conn, w);
}

void Replication::sendQueues(int id) {
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
    world.net.send(world.pl[id].conn, w);
}
void Replication::sendBaseInfo(int id) {
    uint8_t moving = 0, pct = 0;
    reg.view<RelocNew, Building, Owner>().each([&](const RelocNew& rn, const Building& b, const Owner& o) {
        if (rn.town && o.id == id) { moving = 1; pct = uint8_t(std::min(1.f, b.progress) * 100.f); }
    });
    Writer w;
    const BaseProf& b = world.bp[id];
    w.put<uint8_t>(S_BaseInfo).put<uint8_t>(uint8_t(std::min(255, b.quotaTotal()))).put<uint8_t>(b.homeless ? 1 : 0).put<uint8_t>(moving).put<uint8_t>(pct);
    for (uint8_t q : b.quota) w.put<uint8_t>(q);
    for (uint8_t a : b.alloc) w.put<uint8_t>(a);
    world.net.send(world.pl[id].conn, w);
}

void Replication::sendShipInfo(int id) {
    Writer w;
    w.put<uint8_t>(S_ShipInfo);
    size_t cp = w.b.size();
    w.put<uint8_t>(0);
    uint8_t n = 0;
    reg.view<Ship, Owner, Uid, Info>().each([&](entt::entity e, const Ship&, const Owner& o, const Uid& uid, const Info& in) {
        if (o.id != id || n >= 60) return;
        int t, kinds[5];
        float rs;
        s.naval.paxStats(e, t, rs, kinds);
        float res[R_Count]{};
        for (auto q : s.naval.passengers(e)) if (auto* h = reg.try_get<Haul>(q)) for (int r = 0; r < R_Count; r++) res[r] += h->cargo[r];
        w.put<uint32_t>(uid.id).put<uint8_t>(uint8_t(shipTier(in.level)));
        for (int k : kinds) w.put<uint8_t>(uint8_t(k));
        for (float v : res) w.put<uint16_t>(uint16_t(std::min(v, 65000.f)));
        w.put<uint8_t>(reg.all_of<Voyage>(e) ? 1 : 0);
        n++;
    });
    w.b[cp] = n;
    world.net.send(world.pl[id].conn, w);
}
// Path lines: waypoints of every unit / ship of the viewer and of players sharing vision with the viewer (never anybody else).
void Replication::sendPaths(int id) {
    auto& sent = pathSent[id];
    if (!world.pl[id].online) { sent.clear(); return; }
    struct C { uint32_t uid; float d; std::vector<IV> pts; uint64_t h; };
    std::vector<C> cs;
    Pos cam = world.pl[id].cam;
    reg.view<Unit, Owner, Pos, Uid>().each([&](entt::entity e, const Unit& u, const Owner& o, const Pos& p, const Uid& uid) {
        if (!isPlayer(o.id) || (o.id != id && !world.shares(id, o.id)) || u.i >= u.path.size() || reg.all_of<Aboard>(e)) return;
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
    world.net.send(world.pl[id].conn, w);
}
