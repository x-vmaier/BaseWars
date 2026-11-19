#include "systems/ConstructionSystem.hpp"

#include "common/Progression.hpp"
#include "systems/Systems.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr float SITE_REACH = 1.1f;                                   // a pawn works on a building from this close to its footprint
const char* jobName(uint8_t k) { return k == SJ_Upgrade ? "upgrade" : (k == SJ_Repair ? "repair" : "construction"); }
}  // namespace

bool ConstructionSystem::needsPawn(entt::entity e) const {
    const auto* b = reg.try_get<Building>(e);
    if (!b || b->progress >= 1.f || cfg().instantBuild || !isPlayer(reg.get<Owner>(e).id)) return false;
    return !(reg.get<Info>(e).kind == K_TownHall && !reg.all_of<RelocNew>(e));   // a newly founded hall raises itself
}

// ---------------------------------------------------------------- pawns
bool ConstructionSystem::freePawn(entt::entity e, int owner) const {
    if (!reg.valid(e) || !reg.all_of<Unit, Owner, Info>(e) || reg.get<Info>(e).kind != K_Pawn || reg.get<Owner>(e).id != owner) return false;
    if (reg.any_of<Convoy, Haul, Raider, Aboard, Boarding, Extinguish, ClearTask, BuildTask, Support>(e) || world.dying.count(reg.get<Uid>(e).id)) return false;
    if (auto it = manualUntil.find(reg.get<Uid>(e).id); it != manualUntil.end() && it->second > world.clock) return false;
    if (const Worker* w = reg.try_get<Worker>(e)) return w->phase == 0 || w->phase == 3;   // never pull a worker away while it carries goods
    return true;
}
entt::entity ConstructionSystem::pickPawn(int owner, Pos at) {
    entt::entity best = entt::null;
    float bd = 1e9f;
    reg.view<Info, Owner, Unit, Pos>().each([&](entt::entity e, const Info& i, const Owner& o, const Unit&, const Pos& q) {
        if (i.kind != K_Pawn || o.id != owner || !freePawn(e, owner)) return;
        float d = std::hypot(q.x - at.x, q.y - at.y) + (reg.all_of<Worker>(e) ? 12.f : 0.f);   // idle pawns before workers
        if (d < bd) { bd = d; best = e; }
    });
    return best;
}
void ConstructionSystem::assign(entt::entity site, SiteJob& j, entt::entity e) {
    auto& u = reg.get<Unit>(e);
    BuildTask bt{reg.get<Uid>(site).id};
    if (const Worker* w = reg.try_get<Worker>(e)) { bt.hadWorker = true; bt.worker = *w; reg.remove<Worker>(e); }
    else if (!u.path.empty()) { bt.hadDest = true; bt.dest = u.path.back(); }
    reg.remove<Attack>(e);
    reg.remove<AMove>(e);
    u.path.clear();
    u.i = 0;
    reg.emplace_or_replace<BuildTask>(e, bt);
    j.pawn = reg.get<Uid>(e).id;
    j.phase = SP_Walking;
    j.since = world.clock;
}
void ConstructionSystem::releasePawn(entt::entity e, bool manual) {
    const BuildTask* bt = reg.try_get<BuildTask>(e);
    if (!bt) return;
    BuildTask c = *bt;
    reg.remove<BuildTask>(e);
    auto& u = reg.get<Unit>(e);
    u.path.clear();
    u.i = 0;
    uint32_t uid = reg.get<Uid>(e).id;
    auto site = world.find(c.site);
    if (SiteJob* j = site == entt::null ? nullptr : reg.try_get<SiteJob>(site); j && j->pawn == uid) { j->pawn = 0; j->phase = SP_Waiting; j->since = world.clock; }
    if (manual) manualUntil[uid] = world.clock + 30.0;
    if (c.hadWorker && world.find(c.worker.home) != entt::null) {
        Worker w = c.worker;
        if (w.phase == 1) w.phase = 2;
        reg.emplace_or_replace<Worker>(e, w);
    } else if (c.hadDest && !manual) world.order(e, c.dest);
}
// per unit and tick: walk to the site, then work there (only then does the job timer run)
void ConstructionSystem::pawnTick(entt::entity e, Unit& u, BuildTask& bt, Pos& p, float dt) {
    uint32_t uid = reg.get<Uid>(e).id;
    auto site = world.find(bt.site);
    SiteJob* j = site == entt::null ? nullptr : reg.try_get<SiteJob>(site);
    if (!j || j->pawn != uid) { releasePawn(e, false); return; }
    Pos c = world.posOf(site);
    float d = world.rectDist(p, site);
    if (d <= SITE_REACH) {
        u.path.clear();
        u.i = 0;
        u.state = US_Build;
        u.aim = uint8_t(int((std::atan2(c.y - p.y, c.x - p.x) + 3.14159f) / 6.28319f * 255.f));
        j->phase = SP_Working;
        work(site, *j, dt);
        return;
    }
    j->phase = SP_Walking;
    bt.repath -= dt;
    if (!u.path.empty() || bt.repath > 0) return;
    bt.repath = 1.5f;
    if (d < bt.bestDist - .5f) { bt.bestDist = d; bt.stuck = 0; }
    else if (++bt.stuck >= 8) { j->fails++; releasePawn(e, false); manualUntil[uid] = world.clock + 20.0; return; }   // cannot get there: let another pawn try
    if (!world.order(e, c, bt.site, 16000)) bt.stuck += 2;
}

// ---------------------------------------------------------------- jobs
void ConstructionSystem::work(entt::entity site, SiteJob& j, float dt) {
    dt *= baseTestSpeed();
    if (j.kind == SJ_Build) {
        auto& b = reg.get<Building>(site);
        b.progress = std::min(1.f, b.progress + dt / s.build.buildTimeFor(site, reg.get<Info>(site).kind));
        j.t = b.progress * j.total;
        if (b.progress >= 1.f) { s.build.onCompleted(site); endJob(site); }
        return;
    }
    j.t += dt;
    if (j.kind == SJ_Repair) { auto& h = reg.get<Health>(site); h.hp = std::min(h.maxhp, h.hp + j.hpRate * dt); }
    if (j.t < j.total) return;
    int ow = reg.get<Owner>(site).id;
    if (j.kind == SJ_Upgrade) finishUpgrade(site);
    else { world.say(ow, std::string(KINFO[reg.get<Info>(site).kind].name) + " repaired."); Pos c = world.posOf(site); world.fx(E_Dust, c.x, c.y); }
    endJob(site);
}
void ConstructionSystem::finishUpgrade(entt::entity site) {
    auto& in = reg.get<Info>(site);
    auto& h = reg.get<Health>(site);
    float old = h.maxhp;
    in.level = uint8_t(std::min(MAX_LEVEL, in.level + 1));
    h.maxhp = KINFO[in.kind].hp * levelHp(in.level);
    h.hp = std::min(h.maxhp, h.hp + h.maxhp - old);
    Pos c = world.posOf(site);
    world.fx(E_Dust, c.x, c.y);
    world.say(reg.get<Owner>(site).id, std::string(KINFO[in.kind].name) + " upgraded to level " + std::to_string(in.level) + ".");
}
void ConstructionSystem::endJob(entt::entity site) {
    SiteJob j = reg.get<SiteJob>(site);
    reg.remove<SiteJob>(site);
    if (auto e = world.find(j.pawn); e != entt::null) releasePawn(e, false);
}

void ConstructionSystem::tick(float dt) {
    (void)dt;                                                         // job timers advance in pawnTick (only while a pawn works)
    std::vector<entt::entity> fresh;
    if (world.tick % 5 == 0) {
        reg.view<Building, Owner>().each([&](entt::entity e, const Building& b, const Owner&) { if (b.progress < 1.f && !reg.all_of<SiteJob>(e) && needsPawn(e)) fresh.push_back(e); });
        for (auto e : fresh) {
            SiteJob j;
            j.kind = SJ_Build;
            j.total = s.build.buildTimeFor(e, reg.get<Info>(e).kind);
            j.since = world.clock;
            reg.emplace<SiteJob>(e, j);
        }
    }
    if (world.tick % 10 == 0) {                                       // pawn validation and assignment, twice a second
        std::vector<entt::entity> sites;
        reg.view<SiteJob>().each([&](entt::entity e, SiteJob& j) {
            if (j.pawn) {
                auto pe = world.find(j.pawn);
                const BuildTask* bt = pe == entt::null ? nullptr : reg.try_get<BuildTask>(pe);
                if (!bt || bt->site != reg.get<Uid>(e).id) { j.pawn = 0; j.phase = SP_Waiting; j.since = world.clock; }
            }
            if (!j.pawn) sites.push_back(e);
        });
        for (auto e : sites) {
            auto pawn = pickPawn(reg.get<Owner>(e).id, world.posOf(e));
            if (pawn != entt::null) assign(e, reg.get<SiteJob>(e), pawn);
        }
        std::erase_if(manualUntil, [&](auto& kv) { return kv.second < world.clock; });
    }
    if (world.tick % 5 == 2)
        for (int id = 1; id <= MAX_PLAYERS; id++) if (world.pl[id].online) sendJobs(id);
}

// ---------------------------------------------------------------- commands
void ConstructionSystem::upgrade(int id, entt::entity be) {
    if (be == entt::null || !reg.all_of<Building>(be) || reg.get<Owner>(be).id != id || !world.complete(be)) return;
    auto& in = reg.get<Info>(be);
    std::string nm = KINFO[in.kind].name;
    if (reg.all_of<SiteJob>(be)) { world.say(id, "The " + nm + " is already being " + (reg.get<SiteJob>(be).kind == SJ_Repair ? "repaired." : "upgraded.")); return; }
    if (world.outOfTerritory(be) || reg.all_of<Moving>(be)) { world.say(id, "This building is not functional."); return; }
    if (world.crippled(be)) { world.say(id, "The " + nm + " is too badly damaged: repair it first."); return; }
    std::string why = upgradeBlock(in.kind, in.level, world.thLevel(id));
    if (!why.empty()) { world.say(id, "Cannot upgrade the " + nm + ": " + why + "."); return; }
    float cost[R_Count];
    upgradeCost(in.kind, in.level, cost);
    if (!world.afford(id, cost)) { world.say(id, "Not enough resources (food included) to upgrade the " + nm + "."); return; }
    world.spend(id, cost);
    if (cfg().instantUpgrade) { finishUpgrade(be); return; }
    SiteJob j;
    j.kind = SJ_Upgrade;
    j.total = upgradeSeconds(in.kind, in.level);
    j.since = world.clock;
    std::copy(cost, cost + R_Count, j.paid);
    reg.emplace<SiteJob>(be, j);
    world.say(id, "Upgrade of the " + nm + " to level " + std::to_string(in.level + 1) + " ordered (" + std::to_string(int(j.total)) + " s of work). A pawn is on the way; production and training pause meanwhile.");
}
void ConstructionSystem::repair(int id, entt::entity be) {
    auto& in = reg.get<Info>(be);
    auto& h = reg.get<Health>(be);
    std::string nm = KINFO[in.kind].name;
    if (reg.all_of<SiteJob>(be)) { world.say(id, "The " + nm + " already has work in progress."); return; }
    if (reg.all_of<Moving>(be)) { world.say(id, "Buildings being relocated cannot be repaired."); return; }
    float missing = 1.f - h.hp / std::max(1.f, h.maxhp);
    if (missing < .005f) { world.say(id, "The " + nm + " is not damaged."); return; }
    float cost[R_Count];
    repairCost(in.kind, in.level, missing, cost);
    if (!world.afford(id, cost)) { world.say(id, "Not enough resources (food included) to repair the " + nm + "."); return; }
    world.spend(id, cost);
    if (cfg().instantUpgrade) { h.hp = h.maxhp; world.say(id, nm + " repaired."); return; }
    SiteJob j;
    j.kind = SJ_Repair;
    j.total = repairSeconds(in.kind, missing);
    j.hpRate = (h.maxhp - h.hp) / j.total;
    j.since = world.clock;
    std::copy(cost, cost + R_Count, j.paid);
    reg.emplace<SiteJob>(be, j);
    world.say(id, "Repair of the " + nm + " ordered (" + std::to_string(int(j.total)) + " s of work). A pawn is on the way.");
}
void ConstructionSystem::cancel(int id, entt::entity be) {
    SiteJob* j = reg.try_get<SiteJob>(be);
    if (!j || j->kind == SJ_Build) { world.say(id, "Nothing to cancel."); return; }
    float keep = std::clamp(1.f - j->t / std::max(.1f, j->total), 0.f, 1.f);    // refund the share of work not done yet
    for (int r = 0; r < R_Count; r++) world.pl[id].res[r] += std::floor(j->paid[r] * keep);
    std::string what = jobName(j->kind);
    endJob(be);
    world.say(id, "The " + what + " was cancelled; " + std::to_string(int(std::lround(keep * 100.f))) + "% of its cost was refunded.");
}
void ConstructionSystem::repairCmd(int id, Reader& r) {
    auto be = world.find(r.get<uint32_t>());
    int mode = r.get<uint8_t>();
    if (!r.ok || be == entt::null || !reg.all_of<Building>(be) || reg.get<Owner>(be).id != id || !world.complete(be)) return;
    if (mode == 1) cancel(id, be);
    else repair(id, be);
}

// S_Jobs: u16 n, n x (u32 building, u8 SiteKind, u8 SitePhase, u8 pct) for own and allied buildings
void ConstructionSystem::sendJobs(int id) {
    Writer w;
    w.put<uint8_t>(S_Jobs);
    size_t cp = w.b.size();
    w.put<uint16_t>(0);
    uint16_t n = 0;
    reg.view<SiteJob, Owner, Uid>().each([&](const SiteJob& j, const Owner& o, const Uid& uid) {
        if (!world.allied(id, o.id) || n >= 400) return;
        w.put<uint32_t>(uid.id).put<uint8_t>(j.kind).put<uint8_t>(j.phase).put<uint8_t>(uint8_t(std::clamp(j.t / std::max(.01f, j.total), 0.f, 1.f) * 100.f));
        n++;
    });
    if (!n && !jobsWereSent[id]) return;
    jobsWereSent[id] = n > 0;
    std::memcpy(&w.b[cp], &n, 2);
    world.net.send(world.pl[id].conn, w);
}
