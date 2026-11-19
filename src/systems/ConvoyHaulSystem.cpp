#include "systems/ConvoyHaulSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void ConvoyHaulSystem::refundConvoy(entt::entity e, const Convoy& cv, const std::string& why) {
    for (int r = 0; r < R_Count; r++) if (isPlayer(cv.from)) world.pl[cv.from].res[r] += cv.cargo[r];
    world.say(cv.from, "Convoy turned back: " + why + ". Resources returned.");
    world.destroyNow(e);
}
void ConvoyHaulSystem::runConvoy(entt::entity e, Unit& u, Convoy& cv, float dt) {
    Pos p = reg.get<Pos>(e);
    entt::entity th = entt::null;
    float bd = 1e9f;
    reg.view<Info, Building, Owner>().each([&](entt::entity b, const Info& i, const Building& bb, const Owner& o) {
        if (i.kind != K_TownHall || o.id != cv.to || bb.progress < 1.f) return;
        Pos c = footCenter(i.kind, bb.tx, bb.ty);
        float d = std::hypot(c.x - p.x, c.y - p.y);
        if (d < bd) { bd = d; th = b; }
    });
    if (th == entt::null || !isPlayer(cv.to) || !world.pl[cv.to].used || !world.sameClan(cv.from, cv.to)) { refundConvoy(e, cv, "the recipient is no longer reachable"); return; }
    if (world.rectDist(p, th) <= 1.6f) {
        float tot = 0;
        for (int r = 0; r < R_Count; r++) { world.pl[cv.to].res[r] += cv.cargo[r]; tot += cv.cargo[r]; }
        world.say(cv.to, "A convoy from " + world.ownerName(cv.from) + " arrived with " + std::to_string(int(tot)) + " resources.");
        world.say(cv.from, "Your convoy reached " + world.ownerName(cv.to) + ".");
        world.destroyNow(e);
        return;
    }
    cv.repath -= dt;
    if (u.path.empty() && cv.repath <= 0) {
        cv.repath = 3.f;
        float d = world.rectDist(p, th);
        if (d < cv.bestDist - 2.f) { cv.bestDist = d; cv.stuck = 0; }
        else if (++cv.stuck >= 8) { refundConvoy(e, cv, "no land route"); return; }
        if (!world.order(e, world.posOf(th), reg.get<Uid>(th).id, 40000)) cv.stuck += 2;
    }
}
void ConvoyHaulSystem::updateSpotting() {                                             // convoys seen by a clan member are shown to the whole clan
    std::vector<entt::entity> cvs;
    reg.view<Convoy>().each([&](entt::entity e, const Convoy&) { cvs.push_back(e); });
    reg.view<Haul>().each([&](entt::entity e, const Haul&) { cvs.push_back(e); });
    for (auto& [c, m] : clanSpot) std::erase_if(m, [&](auto& kv) { return kv.second.until < world.clock; });
    if (cvs.empty()) return;
    std::vector<Source> srcs[MAX_PLAYERS + 1];
    for (int pid = 1; pid <= MAX_PLAYERS; pid++) if (world.pl[pid].used && world.pl[pid].clan) srcs[pid] = world.sourcesFor(pid);
    for (auto e : cvs) {
        int owner = reg.get<Owner>(e).id;
        Pos p = reg.get<Pos>(e);
        uint32_t uid = reg.get<Uid>(e).id;
        for (int pid = 1; pid <= MAX_PLAYERS; pid++) {
            if (!world.pl[pid].used || !world.pl[pid].clan || world.allied(pid, owner) || !seen(srcs[pid], p.x, p.y)) continue;
            auto& m = clanSpot[world.pl[pid].clan];
            bool fresh = m.find(uid) == m.end();
            m[uid] = {world.clock + 6.0, uint8_t(pid)};
            if (fresh) world.notifyClan(world.pl[pid].clan, "Enemy convoy of " + world.ownerName(owner) + " spotted by " + world.pl[pid].name + " at (" + std::to_string(int(p.x)) + ", " + std::to_string(int(p.y)) + ")!");
        }
    }
}

void ConvoyHaulSystem::spawnHauls() {
    std::unordered_set<uint32_t> busy;
    reg.view<Haul>().each([&](const Haul& h) { busy.insert(h.storage); });
    std::vector<entt::entity> blds;
    reg.view<Info, Building, Owner>().each([&](entt::entity e, const Info& i, const Building& b, const Owner& o) { if ((i.kind == K_Outpost || i.kind == K_Storage) && b.progress >= 1.f && isPlayer(o.id) && !world.inactive(e)) blds.push_back(e); });
    for (auto e : blds) {
        uint32_t uid = reg.get<Uid>(e).id;
        if (busy.count(uid)) continue;
        auto& b = reg.get<Building>(e);
        int ow = reg.get<Owner>(e).id;
        float stock = world.stockOf(e), cap = world.capOf(e);
        if (stock < 20.f || stock / cap * 100.f < float(b.haulPct)) continue;
        auto th = world.townHallOf(ow);
        Pos at;
        if (th == entt::null || !world.spawnSpot(th, false, at)) continue;
        auto u = world.makeUnit(K_Pawn, uint8_t(ow), at.x, at.y);
        Haul h;
        h.storage = uid;
        reg.emplace<Haul>(u, h);
        world.say(ow, std::string("A hauling party leaves the Town Hall for your ") + KINFO[reg.get<Info>(e).kind].name + ".");
    }
}
void ConvoyHaulSystem::runHaul(entt::entity e, Unit& u, Haul& h, float dt) {
    if (auto* bd = reg.try_get<Boarding>(e)) { s.naval.runBoarding(e, u, *bd, dt); return; }   // walking to a ferry
    Pos p = reg.get<Pos>(e);
    int ow = reg.get<Owner>(e).id;
    auto st = world.find(h.storage);
    auto th = world.townHallOf(ow);
    auto giveUp = [&]() {                                           // return whatever we carry to the storage house if we can
        if (st != entt::null) world.storeCargo(st, h.cargo);
        world.destroyNow(e);
    };
    if (th == entt::null) { giveUp(); return; }
    entt::entity target = h.phase == 0 ? st : th;
    if (target == entt::null) { world.destroyNow(e); return; }
    float d = world.rectDist(p, target);
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
            world.storeCargo(th, h.cargo);
            giveUp();
        }
        return;
    }
    h.repath -= dt;
    if (u.path.empty() && h.repath <= 0) {
        h.repath = 3.f;
        if (h.boatPhase != h.phase) {                               // long trip: would one of our ships be clearly faster?
            int ferry = s.ferry.tryFerryHaul(e, h, target);
            if (ferry == 2) { h.repath = .6f; return; }
            if (ferry == 1) return;
        }
        if (d < h.bestDist - 2.f) { h.bestDist = d; h.stuck = 0; }
        else if (++h.stuck >= 8) { giveUp(); return; }
        if (!world.order(e, world.posOf(target), reg.get<Uid>(target).id, 40000)) h.stuck += 2;
    }
}

bool ConvoyHaulSystem::spottedForClan(int id, uint32_t uid) const {
    if (!isPlayer(id) || !world.pl[id].clan) return false;
    auto it = clanSpot.find(world.pl[id].clan);
    if (it == clanSpot.end()) return false;
    auto f = it->second.find(uid);
    return f != it->second.end() && f->second.until >= world.clock;
}

void ConvoyHaulSystem::runWorker(entt::entity e, Unit& u, Worker& w, float dt) {
    Pos p = reg.get<Pos>(e);
    auto home = world.find(w.home);
    if (home == entt::null) { reg.remove<Worker>(e); return; }
    auto& hb = reg.get<Building>(home);
    int ow = reg.get<Owner>(e).id;
    auto toward = [&](entt::entity b) { return world.order(e, world.posOf(b), reg.get<Uid>(b).id); };
    auto findStorage = [&]() {                              // nearest own storage that still has room
        entt::entity best = entt::null;
        float bd = 1e9f;
        reg.view<Info, Building, Owner>().each([&](entt::entity st, const Info& i, const Building& sb, const Owner& so) {
            if (!isStorage(i.kind) || sb.progress < 1.f || so.id != ow || world.capOf(st) - world.stockOf(st) < 1.f) return;
            Pos c = footCenter(i.kind, sb.tx, sb.ty);
            float d = std::hypot(p.x - c.x, p.y - c.y);
            if (d < bd) { bd = d; best = st; }
        });
        return best;
    };
    switch (w.phase) {
    case 0: {
        if (hb.progress < 1.f) return;
        if (world.rectDist(p, home) > 1.6f) { if (u.path.empty()) toward(home); return; }
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
        auto st = world.find(w.dest);
        if (st == entt::null || !world.complete(st) || world.capOf(st) - world.stockOf(st) < 1.f) { st = findStorage(); w.dest = st == entt::null ? 0 : reg.get<Uid>(st).id; u.path.clear(); }
        if (st == entt::null) return;                       // every storage is full: wait with the cargo
        if (world.rectDist(p, st) <= 1.5f) {
            world.storeCargo(st, w.cargo);
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
        if (world.rectDist(p, home) <= 1.6f) { w.phase = 0; u.path.clear(); }
        else if (u.path.empty()) toward(home);
        break;
    }
}
