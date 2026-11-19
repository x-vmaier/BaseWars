#include "client/PacketHandler.hpp"

#include "client/App.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

void PacketHandler::handle(const uint8_t* d, size_t n) {
    Reader r{d, d + n};
    uint8_t ptype = r.get<uint8_t>();
    switch (ptype) {
    case S_Welcome: onWelcome(r); break;
    case S_State: onState(r); break;
    case S_Fog: onFog(r); break;
    case S_Text: app.msg(r.str()); break;
    case S_Roster: onRoster(r); break;
    case S_Chat: onChat(r); break;
    case S_Map: onMap(r); break;
    case S_Paths: if (app.session.welcomed) onShipsPaths(r); break;
    case S_ShipInfo: if (app.session.welcomed) onShipsInfo(r); break;
    default: worldPacket(d, n); basePacket(ptype, r); break;     // [world] / [base] packet sets (each ignores the other's types)
    }
}

void PacketHandler::addFx(uint8_t type, float x, float y, float x2, float y2) {
    ClientWorld& w = app.world;
    w.fxs.push_back({type, x, y, x2, y2, GetTime()});
    if (type == E_Splash) w.ripples.push_back({x * TILE, y * TILE, GetTime()});
    if (type == E_Explosion) {
        float d = std::hypot(x * TILE - app.cam.target.x, y * TILE - app.cam.target.y) / TILE;
        app.post.shake = std::max(app.post.shake, std::clamp(14.f - d * .6f, 0.f, 14.f));
    }
}

void PacketHandler::onWelcome(Reader& r) {
    ClientWorld& w = app.world;
    Camera2D& cam = app.cam;
    w.me = r.get<uint8_t>();
    w.seed = r.get<uint32_t>();
    float cx = r.get<float>(), cy = r.get<float>();
    std::string nm = r.str();
    if (!r.ok) return;
    getRules(r);                                                  // the server's progression / food / donation tunables
    w.myName = nm;
    cam.target = {cx * TILE, cy * TILE};
    cam.zoom = 1.f;
    if (const char* c = std::getenv("BW_CAM")) { float a = 0, b = 0; if (std::sscanf(c, "%f,%f", &a, &b) == 2) cam.target = {a * TILE, b * TILE}; }   // test hooks
    if (const char* z = std::getenv("BW_ZOOM")) cam.zoom = float(std::atof(z));
    app.session.welcomed = true;
    app.session.screen = Screen::Playing;
    SetWindowTitle("Base Wars - connected");
}

void PacketHandler::onState(Reader& r) {
    if (!app.session.welcomed) return;
    ClientWorld& w = app.world;
    auto& reg = w.reg;
    for (float& v : w.res) v = r.get<float>();
    for (int& f : w.facRep) f = r.get<int8_t>();
    for (int& u : w.unitLvl) u = r.get<uint8_t>();
    int cnt = r.get<uint16_t>();
    ++w.stamp;
    for (int i = 0; i < cnt; i++) {
        uint32_t id = r.get<uint32_t>();
        Kind k = Kind(r.get<uint8_t>());
        uint8_t ow = r.get<uint8_t>();
        float x = r.get<float>(), y = r.get<float>();
        float hp = r.get<uint16_t>(), mhp = r.get<uint16_t>();
        uint8_t st = r.get<uint8_t>(), aux = r.get<uint8_t>(), aim = r.get<uint8_t>(), prog = r.get<uint8_t>();
        uint16_t stock = r.get<uint16_t>();
        if (!r.ok || k >= K_Count) return;
        Info inf{k, ow, st, aux, aim, prog, hp, mhp, stock};
        if (aboardUnit(inf)) app.input.forget(id);                // embarked: hidden, cannot be selected
        auto it = w.ents.find(id);
        if (it == w.ents.end()) {
            auto e = reg.create();
            w.ents[id] = e;
            reg.emplace<NetId>(e, NetId{id});
            reg.emplace<RPos>(e, RPos{x, y});
            reg.emplace<Goal>(e, Goal{x, y});
            reg.emplace<Face>(e);
            reg.emplace<Info>(e, inf);
            reg.emplace<Seen>(e, Seen{w.stamp});
        } else {
            auto e = it->second;
            auto& old = reg.get<Info>(e);
            if (ow == w.me && hp < old.hp - .5f) { app.post.damage = std::min(.7f, app.post.damage + .12f); app.post.shake = std::max(app.post.shake, 3.f); }
            old = inf;
            reg.get<Goal>(e) = Goal{x, y};
            reg.get<Seen>(e).stamp = w.stamp;
            reg.remove<Remembered>(e);                            // seen again: live state replaces the memory
        }
    }
    std::vector<entt::entity> dead;
    reg.view<Seen, NetId>().each([&](entt::entity e, const Seen& s, const NetId&) {
        if (s.stamp != w.stamp) dead.push_back(e);
    });
    for (auto e : dead) {
        uint32_t id = reg.get<NetId>(e).id;
        const auto& in = reg.get<Info>(e);
        const auto& rp = reg.get<RPos>(e);
        bool remembered = reg.all_of<Remembered>(e);
        if (remembered && (w.stamp + id) % 5 != 0) continue;        // memories are re-checked against vision every 5th state
        bool vis = w.visibleSource(rp.x, rp.y);
        if (isBuilding(in.kind) && isFactionOwner(in.owner) && !vis) {     // NPC town building left our vision: keep its last known state
            if (!remembered) { reg.emplace<Remembered>(e); app.input.forget(id); }
            continue;
        }
        bool arrived = !isBuilding(in.kind) && (in.state & UNIT_CONVOY_FLAG) && w.nearStorage(rp.x, rp.y);   // convoys / haulers delivered: no ash ghost
        if (vis && !remembered && !arrived) w.ghosts.push_back({in.kind, in.owner, 1, rp.x, rp.y, reg.get<Face>(e).left, GetTime()});
        w.ents.erase(id);
        app.input.forget(id);
        if (app.context.target == id) app.context.close();
        reg.destroy(e);
    }
    int ne = r.get<uint16_t>();
    for (int i = 0; i < ne; i++) {
        uint8_t t = r.get<uint8_t>();
        float x = r.get<float>(), y = r.get<float>(), x2 = r.get<float>(), y2 = r.get<float>();
        if (!r.ok) break;
        addFx(t, x, y, x2, y2);
    }
    w.rebuildOcc();
}

void PacketHandler::onFog(Reader& r) {
    if (!app.session.welcomed) return;
    ClientWorld& w = app.world;
    int nt = r.get<uint16_t>();
    for (int i = 0; i < nt; i++) {
        int cx = r.get<int32_t>(), cy = r.get<int32_t>();
        auto b = r.bytes(CH * CH);
        if (!b) return;
        Chunk& c = w.chunks[chunkKey(cx, cy)];
        std::memcpy(c.biome, b, CH * CH);
        c.hasTerr = true;
    }
    int nf = r.get<uint16_t>();
    for (int i = 0; i < nf; i++) {
        int cx = r.get<int32_t>(), cy = r.get<int32_t>();
        auto b = r.bytes(32);
        if (!b) return;
        Chunk& c = w.chunks[chunkKey(cx, cy)];
        std::memcpy(c.fog, b, 32);
        c.hasFog = true;
    }
    app.minimap.invalidate();
}

void PacketHandler::onRoster(Reader& r) {
    int n = r.get<uint8_t>();
    std::vector<Peer> v;
    for (int i = 0; i < n; i++) {
        Peer p{};
        p.id = r.get<uint8_t>();
        p.flags = r.get<uint8_t>();
        p.clan = r.get<uint8_t>();
        p.name = r.str();
        p.hasLoc = r.get<uint8_t>() != 0;
        p.lx = r.get<float>();
        p.ly = r.get<float>();
        if (!r.ok) return;
        v.push_back(p);
    }
    int nc = r.get<uint8_t>();
    std::vector<ClanInfo> cl;
    for (int i = 0; i < nc; i++) {
        ClanInfo c{};
        c.id = r.get<uint8_t>();
        c.name = r.str();
        c.members = r.get<uint8_t>();
        c.leader = r.get<uint8_t>();
        if (!r.ok) return;
        cl.push_back(c);
    }
    app.world.roster = v;
    app.world.clansList = cl;
}

void PacketHandler::onChat(Reader& r) {
    ChatLine c{};
    c.ch = r.get<uint8_t>();
    r.get<uint8_t>();
    c.name = r.str();
    c.text = r.str();
    c.t = GetTime();
    if (!r.ok) return;
    app.chat.push(c);
}

void PacketHandler::onMap(Reader& r) {
    int n = r.get<uint8_t>();
    std::vector<FacTown> v;
    for (int i = 0; i < n; i++) {
        FacTown t{};
        t.faction = r.get<uint8_t>();
        t.x = r.get<float>();
        t.y = r.get<float>();
        if (!r.ok) return;
        v.push_back(t);
    }
    app.world.facTowns = v;
}

// Ships: aux = tier, prog = troops aboard, stock = resources aboard (S_State); S_ShipInfo: passenger and cargo
// breakdown of our own ships; S_Paths: path lines (owner + clan / allies only).
void PacketHandler::onShipsInfo(Reader& r) {
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
    app.world.shipDetails = std::move(m);
}
void PacketHandler::onShipsPaths(Reader& r) {
    int n = r.get<uint16_t>();
    for (int i = 0; i < n; i++) {
        uint32_t id = r.get<uint32_t>();
        int np = r.get<uint8_t>();
        std::vector<IV> v;
        for (int j = 0; j < np; j++) { int x = r.get<int32_t>(), y = r.get<int32_t>(); v.push_back({x, y}); }
        if (!r.ok) return;
        if (np) app.world.pathLines[id] = std::move(v); else app.world.pathLines.erase(id);
    }
}

void PacketHandler::worldPacket(const uint8_t* d, size_t n) {
    ClientWorld& w = app.world;
    Reader r{d, d + n};
    switch (r.get<uint8_t>()) {
    case S_Cleared: {
        int mode = r.get<uint8_t>();
        uint32_t cnt = r.get<uint32_t>();
        if (!r.ok) return;
        if (mode == 0) w.clearedT.clear();
        for (uint32_t i = 0; i < cnt; i++) {
            int x = r.get<int32_t>(), y = r.get<int32_t>();
            if (!r.ok) return;
            w.clearedT.insert(chunkKey(x, y));
            w.cjobs.erase(chunkKey(x, y));
        }
        break;
    }
    case S_ClearJobs: {
        int cnt = r.get<uint16_t>();
        std::unordered_map<int64_t, CJob> v;
        for (int i = 0; i < cnt; i++) {
            int x = r.get<int32_t>(), y = r.get<int32_t>();
            int pct = r.get<uint8_t>(), st = r.get<uint8_t>();
            if (!r.ok) return;
            v[chunkKey(x, y)] = CJob{float(pct) / 100.f, uint8_t(st)};
        }
        w.cjobs = std::move(v);
        int np = r.get<uint16_t>();
        std::vector<PendingSite> ps;
        for (int i = 0; i < np; i++) {
            int k = r.get<uint8_t>(), tx = r.get<int32_t>(), ty = r.get<int32_t>();
            if (!r.ok || k >= K_Count) break;
            ps.push_back({Kind(k), tx, ty});
        }
        w.pendingSites = std::move(ps);
        break;
    }
    case S_Drop: {
        DropFx f{};
        f.x = r.get<float>(); f.y = r.get<float>();
        f.res = r.get<uint8_t>(); f.amt = r.get<uint16_t>();
        f.t = GetTime();
        if (!r.ok || f.res >= R_Count) return;
        w.drops.push_back(f);
        if (w.drops.size() > 80) w.drops.erase(w.drops.begin());
        break;
    }
    default: break;
    }
}

bool PacketHandler::basePacket(uint8_t type, Reader& r) {
    ClientWorld& w = app.world;
    switch (type) {
    case S_Queue: {
        int n = r.get<uint8_t>();
        std::unordered_map<uint32_t, QEntry> q;
        for (int i = 0; i < n; i++) {
            uint32_t uid = r.get<uint32_t>();
            int cnt = r.get<uint8_t>();
            QEntry e;
            e.pct = r.get<uint8_t>();
            for (int j = 0; j < cnt; j++) e.kinds.push_back(r.get<uint8_t>());
            if (!r.ok) return true;
            q[uid] = e;
        }
        w.queues = q;
        return true;
    }
    case S_Alert: {
        Toast t{};
        t.type = r.get<uint8_t>();
        t.x = r.get<float>();
        t.y = r.get<float>();
        t.text = r.str();
        t.t = GetTime();
        if (!r.ok) return true;
        app.baseMenus.push(t);
        if (t.type == 2) app.cam.target = {t.x * TILE, t.y * TILE};          // lost our town: look at the clan
        return true;
    }
    case S_BaseInfo: {
        r.get<uint8_t>();                                                    // total quota (the per-type values follow)
        int hl = r.get<uint8_t>(), mv = r.get<uint8_t>(), pct = r.get<uint8_t>(), q[QUOTA_TYPES], al[R_Count];
        for (int& v : q) v = r.get<uint8_t>();
        for (int& v : al) v = r.get<uint8_t>();
        if (!r.ok) return true;
        if (GetTime() - w.baseEditAt > 1.5) {
            for (int t = 0; t < QUOTA_TYPES; t++) w.quota[t] = q[t];
            for (int k = 0; k < R_Count; k++) w.alloc[k] = al[k];
        }
        w.homeless = hl != 0;
        w.townMoving = mv != 0;
        w.movePct = pct;
        if (w.homeless && !w.wasHomeless) app.placement.begin(Placement::HomelessHall, K_TownHall, 0);
        if (!w.homeless && w.wasHomeless && app.placement.placeMode == Placement::HomelessHall) app.placement.buildKind = -1;
        w.wasHomeless = w.homeless;
        return true;
    }
    case S_Jobs: {
        int n = r.get<uint16_t>();
        std::unordered_map<uint32_t, BJob> v;
        for (int i = 0; i < n; i++) {
            uint32_t uid = r.get<uint32_t>();
            BJob j;
            j.kind = r.get<uint8_t>();
            j.phase = r.get<uint8_t>();
            j.pct = r.get<uint8_t>();
            if (!r.ok) return true;
            v[uid] = j;
        }
        w.jobs = std::move(v);
        return true;
    }
    case S_Econ: {
        float cd = r.get<float>();
        FacDemand d[FACTION_COUNT];
        for (auto& f : d) {
            f.res = r.get<uint8_t>();
            f.changeAt = GetTime() + double(r.get<uint16_t>());
            for (int& m : f.mul) m = r.get<uint8_t>();
        }
        if (!r.ok) return true;
        w.donateReadyAt = GetTime() + double(cd);
        for (int f = 0; f < FACTION_COUNT; f++) w.demand[f] = d[f];
        return true;
    }
    default: return false;
    }
}
