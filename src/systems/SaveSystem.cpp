#include "systems/SaveSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

void SaveSystem::saveGame() {
    Writer w;
    w.put<uint32_t>(SAVE_MAGIC).put<uint32_t>(world.seed).put<uint32_t>(world.nextUid).put<double>(world.clock);
    uint32_t np = 0;
    for (int i = 1; i <= MAX_PLAYERS; i++) if (world.pl[i].used) np++;
    w.put<uint32_t>(np);
    for (int i = 1; i <= MAX_PLAYERS; i++) {
        if (!world.pl[i].used) continue;
        const Profile& p = world.pl[i];
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
    for (int c = 1; c <= MAX_CLANS; c++) { w.put<uint8_t>(world.clans[c].used ? 1 : 0); if (world.clans[c].used) { w.str(world.clans[c].name); w.put<uint8_t>(world.clans[c].leader); } }
    for (float d : world.facDonated) w.put<float>(d);
    w.put<uint32_t>(uint32_t(world.regionsDone.size()));
    for (auto k : world.regionsDone) w.put<int64_t>(k);
    w.put<uint32_t>(uint32_t(world.towns.size()));
    for (auto& t : world.towns) w.put<uint16_t>(t.id).put<uint8_t>(t.faction).put<int32_t>(t.cx).put<int32_t>(t.cy).put<uint32_t>(t.th).put<uint8_t>(t.alive ? 1 : 0).put<double>(t.respawnAt);
    w.put<uint32_t>(uint32_t(world.spawned.size()));
    for (auto k : world.spawned) w.put<int64_t>(k);
    uint32_t saved = 0;
    reg.view<Uid>().each([&](entt::entity e, const Uid&) { if (!world.ephemeral(e)) saved++; });
    w.put<uint32_t>(saved);
    reg.view<Uid, Owner, Info, Health>().each([&](entt::entity e, const Uid& uid, const Owner& o, const Info& in, const Health& h) {
        if (world.ephemeral(e)) return;
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
            if (const ClearTask* ct = reg.try_get<ClearTask>(e); !wk && ct && ct->hadWorker) wk = &ct->worker;   // a clearing pawn keeps its job across saves
            if (const BuildTask* bt = reg.try_get<BuildTask>(e); !wk && bt && bt->hadWorker) wk = &bt->worker;   // ... and so does a pawn on a building site
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
    std::string tmp = world.savePath + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { std::printf("Could not write %s\n", tmp.c_str()); return; }
        f.write(reinterpret_cast<const char*>(w.b.data()), std::streamsize(w.b.size()));
    }
    std::error_code ec;
    std::filesystem::rename(tmp, world.savePath, ec);
    if (ec) std::printf("Could not replace %s: %s\n", world.savePath.c_str(), ec.message().c_str());
}

bool SaveSystem::loadGame() {
    std::ifstream f(world.savePath, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Reader r{data.data(), data.data() + data.size()};
    if (r.get<uint32_t>() != SAVE_MAGIC) { std::puts("Savegame has an older or unknown format."); return false; }
    world.loadStarted = true;                                          // from here on a failure leaves a half-loaded world behind
    world.seed = r.get<uint32_t>();
    world.rng.seed(world.seed ^ uint32_t(nowSec()));
    world.nextUid = r.get<uint32_t>();
    world.clock = r.get<double>();
    uint32_t np = r.get<uint32_t>();
    for (uint32_t n = 0; n < np && r.ok; n++) {
        int id = r.get<uint8_t>();
        if (id < 1 || id > MAX_PLAYERS) return false;
        Profile& p = world.pl[id];
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
        world.clans[c] = Clan{};
        if (r.get<uint8_t>()) { world.clans[c].used = true; world.clans[c].name = r.str(); world.clans[c].leader = r.get<uint8_t>(); }
    }
    for (float& d : world.facDonated) d = r.get<float>();
    uint32_t nr = r.get<uint32_t>();
    for (uint32_t i = 0; i < nr && r.ok; i++) world.regionsDone.insert(r.get<int64_t>());
    uint32_t nt = r.get<uint32_t>();
    for (uint32_t i = 0; i < nt && r.ok; i++) {
        Town t{};
        t.id = r.get<uint16_t>(); t.faction = r.get<uint8_t>(); t.cx = r.get<int32_t>(); t.cy = r.get<int32_t>(); t.th = r.get<uint32_t>();
        t.alive = r.get<uint8_t>() != 0; t.respawnAt = r.get<double>(); t.nextSpawn = 0; t.lastAlert = -100;
        world.towns.push_back(t);
    }
    uint32_t ns = r.get<uint32_t>();
    for (uint32_t i = 0; i < ns && r.ok; i++) world.spawned.insert(r.get<int64_t>());
    uint32_t nent = r.get<uint32_t>();
    for (uint32_t n = 0; n < nent && r.ok; n++) {
        uint32_t uid = r.get<uint32_t>();
        Kind k = Kind(r.get<uint8_t>());
        uint8_t owner = r.get<uint8_t>(), level = r.get<uint8_t>();
        float hp = r.get<float>(), mhp = r.get<float>();
        uint16_t town = r.get<uint16_t>();
        uint8_t isB = r.get<uint8_t>();
        if (!r.ok || k >= K_Count) return false;
        auto e = world.makeBase(k, owner, uid);
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
                for (int x = b.tx; x < b.tx + KINFO[k].fw; x++) world.occ[tkey(x, y)] = uid;
        } else {
            Pos p{r.get<float>(), r.get<float>()};
            reg.emplace<Pos>(e, p);
            reg.emplace<Unit>(e);
            if (k == K_Sheep) reg.emplace<Wander>(e, Wander{1.f});
            uint8_t fl = r.get<uint8_t>();
            if (town) {
                if (Town* tw = world.townById(town)) { reg.emplace<Home>(e, Home{float(tw->cx) + .5f, float(tw->cy) + 3.f, 0}); reg.emplace_or_replace<Wander>(e, Wander{1.f}); }
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
    if (!r.ok) { std::puts("Savegame is truncated."); return false; }
    // Optional appended sections (world, ships, base): a damaged or missing one is skipped instead of failing the whole load.
    auto optional = [&](const char* name, auto&& load) {
        load();
        if (!r.ok) { std::printf("Ignoring damaged %s section of the savegame.\n", name); r.ok = true; r.p = r.e; }
    };
    optional("world", [&] { worldLoad(r); });
    optional("ship", [&] { shipsLoad(r); });
    optional("base", [&] { if (r.p < r.e) loadBase(r); });
    return true;
}

void SaveSystem::worldSave(Writer& w) {
    w.put<uint32_t>(uint32_t(world.cleared.size()));
    for (auto k : world.cleared) w.put<int64_t>(k);
}
void SaveSystem::worldLoad(Reader& r) {                                       // append-only section after the entity list (absent in older saves)
    if (r.p >= r.e) return;
    uint32_t n = r.get<uint32_t>();
    for (uint32_t i = 0; i < n && r.ok; i++) world.cleared.insert(r.get<int64_t>());
    world.clearVer++;
}

void SaveSystem::shipsSave(Writer& w) {
    std::vector<entt::entity> ships;
    reg.view<Ship>().each([&](entt::entity e, const Ship& s) { if (!s.pax.empty()) ships.push_back(e); });
    w.put<uint32_t>(uint32_t(ships.size()));
    for (auto e : ships) {
        int t;
        float rs;
        s.naval.paxStats(e, t, rs);
        const auto& pax = reg.get<Ship>(e).pax;
        w.put<uint32_t>(reg.get<Uid>(e).id).put<uint16_t>(uint16_t(pax.size()));
        for (uint32_t u : pax) w.put<uint32_t>(u);
    }
}
void SaveSystem::shipsLoad(Reader& r) {
    reg.view<Info>().each([&](entt::entity e, const Info& in) { if (in.kind == K_Ship && !reg.all_of<Ship>(e)) reg.emplace<Ship>(e, Ship{{}, world.clock}); });
    if (r.p >= r.e) return;                                  // older savegame without a ship section
    uint32_t n = r.get<uint32_t>();
    for (uint32_t i = 0; i < n && r.ok; i++) {
        uint32_t suid = r.get<uint32_t>();
        int cnt = r.get<uint16_t>();
        auto ship = world.find(suid);
        for (int j = 0; j < cnt && r.ok; j++) {
            uint32_t uid = r.get<uint32_t>();
            auto e = world.find(uid);
            if (ship == entt::null || e == entt::null || !reg.all_of<Unit>(e) || !reg.all_of<Ship>(ship)) continue;
            reg.emplace_or_replace<Aboard>(e, Aboard{suid});
            reg.get<Ship>(ship).pax.push_back(uid);
            reg.get<Pos>(e) = reg.get<Pos>(ship);
        }
    }
}

void SaveSystem::saveBase(Writer& w) {
    w.put<uint32_t>(0x32455342u);                                                   // "BSE2": per-type quotas + storage shares
    uint8_t np = 0;
    for (int i = 1; i <= MAX_PLAYERS; i++) if (world.pl[i].used) np++;
    w.put<uint8_t>(np);
    for (int i = 1; i <= MAX_PLAYERS; i++) {
        if (!world.pl[i].used) continue;
        w.put<uint8_t>(uint8_t(i)).put<uint8_t>(world.bp[i].homeless ? 1 : 0);
        for (uint8_t q : world.bp[i].quota) w.put<uint8_t>(q);
        for (uint8_t a : world.bp[i].alloc) w.put<uint8_t>(a);
        w.put<uint16_t>(uint16_t(std::min<size_t>(world.bp[i].friends.size(), 200)));
        for (size_t f = 0; f < world.bp[i].friends.size() && f < 200; f++) w.put<uint64_t>(world.bp[i].friends[f]);
    }
    std::vector<entt::entity> es;
    reg.view<Uid>().each([&](entt::entity e, const Uid&) { if (reg.any_of<BFlags, Moving, RelocNew, TrainQ, Support, SiteJob>(e)) es.push_back(e); });
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
        const SiteJob* sj = reg.try_get<SiteJob>(e);                              // site jobs (the pawn is re-assigned after loading)
        w.put<uint8_t>(sj ? 1 : 0);
        if (sj) { w.put<uint8_t>(sj->kind).put<float>(sj->t).put<float>(sj->total).put<float>(sj->hpRate); for (float v : sj->paid) w.put<float>(v); }
    }
}
void SaveSystem::loadBase(Reader& r) {
    if (r.get<uint32_t>() != 0x32455342u) { r.ok = false; return; }
    int np = r.get<uint8_t>();
    for (int n = 0; n < np && r.ok; n++) {
        int id = r.get<uint8_t>();
        uint8_t hl = r.get<uint8_t>(), q[QUOTA_TYPES], al[R_Count];
        for (auto& v : q) v = r.get<uint8_t>();
        for (auto& v : al) v = r.get<uint8_t>();
        int nf = r.get<uint16_t>();
        std::vector<uint64_t> fr;
        for (int f = 0; f < nf && r.ok; f++) fr.push_back(r.get<uint64_t>());
        if (id < 1 || id > MAX_PLAYERS) continue;
        for (int t = 0; t < QUOTA_TYPES; t++) world.bp[id].quota[t] = std::min<uint8_t>(q[t], MAX_QUOTA);
        for (int k = 0; k < R_Count; k++) world.bp[id].alloc[k] = std::min<uint8_t>(al[k], 100);
        world.bp[id].homeless = hl != 0;
        world.bp[id].friends = fr;
    }
    uint32_t ne = r.get<uint32_t>();
    for (uint32_t n = 0; n < ne && r.ok; n++) {
        auto e = world.find(r.get<uint32_t>());
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
        bool hasSj = r.get<uint8_t>() != 0;
        SiteJob sj;
        if (hasSj) { sj.kind = r.get<uint8_t>(); sj.t = r.get<float>(); sj.total = r.get<float>(); sj.hpRate = r.get<float>(); for (float& v : sj.paid) v = r.get<float>(); sj.since = world.clock; }
        if (!r.ok || e == entt::null) continue;
        if (hasSj && sj.kind <= SJ_Repair && reg.all_of<Building>(e)) reg.emplace_or_replace<SiteJob>(e, sj);
        if (burn > 0) { auto& f = reg.get_or_emplace<BFlags>(e); f.burn = burn; f.burnBy = burnBy; }
        if (mv) reg.emplace_or_replace<Moving>(e, Moving{mv});
        if (hasRn) reg.emplace_or_replace<RelocNew>(e, rn);
        if (!tq.q.empty()) reg.emplace_or_replace<TrainQ>(e, tq);
        if (hasSp && reg.all_of<Unit>(e)) reg.emplace_or_replace<Support>(e, sp);
    }
}
