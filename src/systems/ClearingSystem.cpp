/**
 * @file ClearingSystem.cpp
 * @brief Clearing of surface features by pawns, buildings queued behind clearing, resource drops.
 */
#include "systems/ClearingSystem.hpp"

#include "common/Progression.hpp"
#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <spdlog/spdlog.h>
#include <string>

namespace
{
struct WorldAdapter
{
    // Adapter for the shared goldNear()
    World &s;
    int biome(int x, int y) { return s.biome(x, y); }
    bool cleared(int x, int y) { return s.cleared.count(tkey(x, y)) > 0; }
    uint32_t seedValue() { return s.seed; }
};
constexpr float WORK_REACH = 1.25f;       // a pawn works on a feature from this close (tiles from its centre)
constexpr double NO_PAWN_TIMEOUT = 180.0; // a job without any pawn for this long is cancelled and refunded
constexpr float AUTO_PICK_RANGE = 90.f;   // nearest free pawn search radius
} // namespace

std::vector<IV> ClearingSystem::blockedTiles(Kind k, int tx, int ty)
{
    std::vector<IV> v;
    for (int y = ty; y < ty + KINFO[k].fh; y++)
        for (int x = tx; x < tx + KINFO[k].fw; x++)
            if (!world.isCleared(x, y) && featureBlocks(world.featOf(x, y)))
                v.push_back({x, y});
    return v;
}

bool ClearingSystem::worldBuildOk(int id, Kind k, int tx, int ty)
{
    auto v = blockedTiles(k, tx, ty);
    if (v.empty())
        return true;
    int trees = 0, rocks = 0, gold = 0;
    for (auto t : v)
    {
        uint8_t f = world.featOf(t.x, t.y);
        (f == F_Rock ? rocks : (f == F_Gold ? gold : trees))++;
    }
    std::string msg = "Cannot build here: clear";
    if (trees)
        msg += " " + std::to_string(trees) + " tree" + (trees > 1 ? "s" : "");
    if (rocks)
        msg += (trees ? "," : "") + std::string(" ") + std::to_string(rocks) + " rock" + (rocks > 1 ? "s" : "");
    if (gold)
        msg += ((trees || rocks) ? "," : "") + std::string(" ") + std::to_string(gold) + " gold vein" +
               (gold > 1 ? "s" : "");
    world.say(id, msg + " first (right-click them to send a pawn).");
    return false;
}

void ClearingSystem::queueBuild(int id, Kind k, int tx, int ty, const std::vector<IV> &blocked)
{
    PendingBuild pb{id, k, tx, ty, {}, blocked};
    float total[R_Count];
    buildCost(k, pb.cost);
    for (int r = 0; r < R_Count; r++)
        total[r] = pb.cost[r];
    std::vector<IV> fresh;
    for (auto t : blocked)
        if (!clearJobs.count(tkey(t.x, t.y)))
        {
            fresh.push_back(t);
            total[R_Wood] += float(clearCostWood(world.featOf(t.x, t.y)));
        }
    if (!world.afford(id, total))
    {
        world.say(id, "Not enough resources (the building plus " +
                          std::to_string(int(total[R_Wood] - pb.cost[R_Wood])) + " wood for clearing).");
        return;
    }
    world.spend(id, total);
    for (auto t : fresh)
    {
        uint8_t f = world.featOf(t.x, t.y);
        ClearJob j{id, f, 0.f, clearSeconds(f)};
        j.since = world.clock;
        j.paid = float(clearCostWood(f));
        clearJobs[tkey(t.x, t.y)] = j;
    }
    pending[nextPending++] = pb;
    world.say(id, std::string("Pawns clear ") + std::to_string(blocked.size()) + " obstacle" +
                      (blocked.size() > 1 ? "s" : "") + " first; the " + KINFO[k].name +
                      " is placed when the site is clear.");
}

bool ClearingSystem::inClearReach(int id, int x, int y)
{
    // Clearing is allowed anywhere the player could place a building
    bool ok = false;
    reg.view<Info, Building, Owner>().each([&](const Info &i, const Building &b, const Owner &o)
    {
        if (ok || !hasTag(i.kind, KT_Anchor) || !world.allied(id, o.id) || b.progress < 1.f)
            return;
        Pos c = footCenter(i.kind, b.tx, b.ty);
        float reach = i.kind == K_TownHall ? 60.f : 34.f;
        if (o.id != id)
            reach = std::max(reach,
                             float(CLAN_TOWN_RADIUS) +
                                 4.f); // a homeless clan member may build (and so clear) anywhere around the clan halls
        if (std::max(std::abs(c.x - float(x)), std::abs(c.y - float(y))) <= reach)
            ok = true;
    });
    return ok;
}

// C_Clear: u8 n, n x (i32 x, i32 y) [, u8 m, m x u32 pawn]  - the optional tail names the pawns to send
bool ClearingSystem::readClear(int id, Reader &r, std::vector<IV> &ts, std::vector<uint32_t> &want)
{
    int n = r.get<uint8_t>();
    if (!r.ok || n < 1 || n > MAX_CLEAR_TILES)
        return false;

    for (int i = 0; i < n; i++)
    {
        int x = r.get<int32_t>(), y = r.get<int32_t>();
        if (!r.ok)
            return false;
        ts.push_back({x, y});
    }
    if (r.p < r.e)
    {
        int m = r.get<uint8_t>();
        for (int i = 0; i < m && r.ok; i++)
        {
            uint32_t uid = r.get<uint32_t>();
            auto e = world.find(uid);
            if (r.ok && e != entt::null && freePawn(e, id, true))
                want.push_back(uid);
        }
        if (!r.ok)
            return false;
    }
    return true;
}

// Picks the tiles that may be cleared now and sums their wood cost; `far` flags tiles outside the clear reach.
void ClearingSystem::planClear(int id, const std::vector<IV> &ts, float *cost, std::vector<IV> &ok, bool &far)
{
    int mine = 0;
    for (auto &j : clearJobs)
        if (j.second.owner == id)
            mine++;

    for (auto t : ts)
    {
        int64_t key = tkey(t.x, t.y);
        if (clearJobs.count(key) || world.isCleared(t.x, t.y) || !world.explored(id, t.x, t.y) || world.occ.count(key))
            continue;
        uint8_t f = world.featOf(t.x, t.y);
        if (f == F_None)
            continue;
        if (!inClearReach(id, t.x, t.y))
        {
            far = true;
            continue;
        }
        if (mine + int(ok.size()) >= 60)
            break;
        cost[R_Wood] += float(clearCostWood(f));
        ok.push_back(t);
    }
}

// Explicitly picked pawns leave their current clear job now.
void ClearingSystem::releasePicked(const std::vector<uint32_t> &want)
{
    for (auto uid : want)
    {
        auto e = world.find(uid);
        if (e == entt::null || !reg.all_of<ClearTask>(e))
            continue;
        int64_t old = reg.get<ClearTask>(e).tile;
        if (auto it = clearJobs.find(old); it != clearJobs.end() && it->second.pawn == uid)
        {
            it->second.pawn = 0;
            it->second.state = 0;
            it->second.since = world.clock;
        }
        releasePawn(e, false);
    }
}

void ClearingSystem::onClear(int id, Reader &r)
{
    std::vector<IV> ts;
    std::vector<uint32_t> want;
    if (!readClear(id, r, ts, want))
        return;

    float cost[R_Count]{};
    std::vector<IV> ok;
    bool far = false;
    planClear(id, ts, cost, ok, far);
    if (ok.empty())
    {
        world.say(id, far ? "That is too far from your Town Hall or Outposts to clear." : "Nothing to clear there.");
        return;
    }
    if (!world.afford(id, cost))
    {
        world.say(id, "Not enough wood to clear that (" + std::to_string(int(cost[R_Wood])) + " needed).");
        return;
    }
    world.spend(id, cost);

    for (auto t : ok)
    {
        uint8_t f = world.featOf(t.x, t.y);
        ClearJob j{id, f, 0.f, clearSeconds(f)};
        j.want = want;
        j.since = world.clock;
        j.paid = float(clearCostWood(f));
        clearJobs[tkey(t.x, t.y)] = j;
    }
    releasePicked(want);

    const char *what = ok.size() == 1 ? featName(world.featOf(ok[0].x, ok[0].y)) : "obstacles";
    world.say(id, std::string("Clearing ") + (ok.size() == 1 ? "the " : std::to_string(ok.size()) + " ") + what +
                      " (-" + std::to_string(int(cost[R_Wood])) + " wood)" +
                      (want.empty() ? ": the nearest free pawn goes there." : "."));
}

bool ClearingSystem::freePawn(entt::entity e, int owner, bool explicitPick)
{
    if (!reg.valid(e) || !reg.all_of<Unit, Owner, Info>(e) || !hasTag(reg.get<Info>(e).kind, KT_Worker) ||
        reg.get<Owner>(e).id != owner)
        return false;
    if (reg.any_of<Convoy, Haul, Raider, Aboard, Boarding, Extinguish, BuildTask>(e) ||
        world.dying.count(reg.get<Uid>(e).id))
        return false;
    if (explicitPick)
        return true;
    if (reg.all_of<ClearTask>(e))
        return false;
    if (auto it = manualUntil.find(reg.get<Uid>(e).id); it != manualUntil.end() && it->second > world.clock)
        return false;
    if (const Worker *w = reg.try_get<Worker>(e))
        return w->phase == 0 || w->phase == 3; // never pull a worker away while it carries goods
    return true;
}

entt::entity ClearingSystem::pickPawn(const ClearJob &j, Pos at)
{
    entt::entity best = entt::null;
    float bd = 1e9f;
    for (auto uid : j.want)
    {
        // The player's own pick first
        auto e = world.find(uid);
        if (e == entt::null || !freePawn(e, j.owner, true) || reg.all_of<ClearTask>(e))
            continue;
        Pos q = reg.get<Pos>(e);
        float d = std::hypot(q.x - at.x, q.y - at.y);
        if (d < bd)
        {
            bd = d;
            best = e;
        }
    }
    if (best != entt::null || !j.want.empty())
        return best; // picked pawns are busy: wait for them
    reg.view<Info, Owner, Unit, Pos>().each(
        [&](entt::entity e, const Info &i, const Owner &o, const Unit &, const Pos &q)
    {
        if (!hasTag(i.kind, KT_Worker) || o.id != j.owner || !freePawn(e, j.owner, false))
            return;
        float d =
            std::hypot(q.x - at.x, q.y - at.y) + (reg.all_of<Worker>(e) ? 12.f : 0.f); // idle pawns before workers
        if (d < bd && d <= AUTO_PICK_RANGE)
        {
            bd = d;
            best = e;
        }
    });
    return best;
}

void ClearingSystem::assign(int64_t key, ClearJob &j, entt::entity e)
{
    auto &u = reg.get<Unit>(e);
    ClearTask ct;
    ct.tile = key;
    if (const ClearTask *old = reg.try_get<ClearTask>(e))
    {
        // Chaining: keep the work from before the first job
        ct.hadWorker = old->hadWorker;
        ct.worker = old->worker;
        ct.hadDest = old->hadDest;
        ct.dest = old->dest;
    }
    else if (const Worker *w = reg.try_get<Worker>(e))
    {
        ct.hadWorker = true;
        ct.worker = *w;
        reg.remove<Worker>(e);
    }
    else if (!u.path.empty())
    {
        ct.hadDest = true;
        ct.dest = u.path.back();
    }
    reg.remove<Attack>(e);
    reg.remove<AMove>(e);
    u.path.clear();
    u.i = 0;
    reg.emplace_or_replace<ClearTask>(e, ct);
    j.pawn = reg.get<Uid>(e).id;
    j.state = 1;
    j.since = world.clock;
}

void ClearingSystem::releasePawn(entt::entity e, bool manual)
{
    const ClearTask *ct = reg.try_get<ClearTask>(e);
    if (!ct)
        return;
    ClearTask c = *ct;
    reg.remove<ClearTask>(e);
    auto &u = reg.get<Unit>(e);
    u.path.clear();
    u.i = 0;
    uint32_t uid = reg.get<Uid>(e).id;
    if (auto it = clearJobs.find(c.tile); it != clearJobs.end() && it->second.pawn == uid)
    {
        it->second.pawn = 0;
        it->second.state = 0;
        it->second.since = world.clock;
    }
    if (manual)
        manualUntil[uid] = world.clock + 30.0;
    if (c.hadWorker && world.find(c.worker.home) != entt::null)
    {
        // Back to the building it works for
        Worker w = c.worker;
        if (w.phase == 1)
            w.phase = 2;
        reg.emplace_or_replace<Worker>(e, w);
    }
    else if (c.hadDest && !manual)
        world.order(e, c.dest); // resume the walk it was on
}

// per unit and tick: walk to the tile, then work on it (only then does the job timer run)
void ClearingSystem::pawnTick(entt::entity e, Unit &u, ClearTask &ct, Pos &p, float dt)
{
    uint32_t uid = reg.get<Uid>(e).id;
    auto it = clearJobs.find(ct.tile);
    if (it == clearJobs.end() || it->second.pawn != uid)
    {
        releasePawn(e, false);
        return;
    }
    ClearJob &j = it->second;
    Pos c{float(keyCx(ct.tile)) + .5f, float(keyCy(ct.tile)) + .5f};
    float d = std::hypot(c.x - p.x, c.y - p.y);
    if (d <= WORK_REACH)
    {
        u.path.clear();
        u.i = 0;
        u.state = US_Work;
        u.aim = uint8_t(int((std::atan2(c.y - p.y, c.x - p.x) + 3.14159f) / 6.28319f * 255.f));
        j.state = 2;
        j.t += dt;
        return;
    }
    j.state = 1;
    ct.repath -= dt;
    if (!u.path.empty() || ct.repath > 0)
        return;
    ct.repath = 1.5f;
    if (d < ct.bestDist - .5f)
    {
        ct.bestDist = d;
        ct.stuck = 0;
    }
    else if (++ct.stuck >= 6)
    {
        // Cannot get there: let another pawn try
        j.fails++;
        if (!j.want.empty() && j.fails >= 2)
            j.want.clear();
        releasePawn(e, false);
        return;
    }
    if (!world.order(e, c, 0, 12000))
        ct.stuck += 2;
}

void ClearingSystem::cancelJob(int64_t key, const std::string &why)
{
    auto it = clearJobs.find(key);
    if (it == clearJobs.end())
        return;
    ClearJob j = it->second;
    clearJobs.erase(it);
    if (auto e = world.find(j.pawn); e != entt::null)
        releasePawn(e, false);
    if (isPlayer(j.owner) && world.pl[j.owner].used)
        world.pl[j.owner].res[R_Wood] += j.paid;
    world.say(j.owner, "Clearing cancelled: " + why + " (" + std::to_string(int(j.paid)) + " wood refunded).");
}

void ClearingSystem::finishJob(int64_t key)
{
    ClearJob j = clearJobs[key];
    clearJobs.erase(key);
    int x = keyCx(key), y = keyCy(key);
    world.markCleared(x, y);
    Pos p{float(x) + .5f, float(y) + .6f};
    creditDrop(j.owner, p, dropRes(j.feat), dropAmount(j.feat));
    world.fx(E_Dust, p.x, p.y);
    auto e = world.find(j.pawn);
    if (e == entt::null)
        return;
    int64_t next = 0; // chain to the nearest waiting job of the same owner
    float bd = 12.f;
    for (auto &[k2, j2] : clearJobs)
    {
        if (j2.owner != j.owner || j2.pawn ||
            (!j2.want.empty() && std::find(j2.want.begin(), j2.want.end(), j.pawn) == j2.want.end()))
            continue;
        float d = std::hypot(float(keyCx(k2) - x), float(keyCy(k2) - y));
        if (d < bd)
        {
            bd = d;
            next = k2;
        }
    }
    if (bd < 12.f)
        assign(next, clearJobs[next], e);
    else
        releasePawn(e, false);
}

void ClearingSystem::pendingTick()
{
    std::vector<uint32_t> done;
    for (auto &[pid, pb] : pending)
    {
        bool waiting = false, lost = false;
        for (auto t : pb.tiles)
        {
            if (world.isCleared(t.x, t.y))
                continue;
            if (clearJobs.count(tkey(t.x, t.y)))
                waiting = true;
            else
                lost = true;
        }
        if (waiting && !lost)
            continue;
        done.push_back(pid);
        PlaceWorld w{world, pb.owner};
        int err = lost ? P_Count : checkPlace(w, pb.k, pb.tx, pb.ty);
        if (!err && blockedTiles(pb.k, pb.tx, pb.ty).empty() && world.pl[pb.owner].used)
        {
            auto e = world.makeBuilding(pb.k, uint8_t(pb.owner), pb.tx, pb.ty, false);
            Pos c = world.posOf(e);
            world.fx(E_Dust, c.x, c.y);
            world.say(pb.owner,
                      std::string("The site is clear: construction of your ") + KINFO[pb.k].name + " begins.");
        }
        else
        {
            if (world.pl[pb.owner].used)
                for (int r = 0; r < R_Count; r++)
                    world.pl[pb.owner].res[r] += pb.cost[r];
            world.say(pb.owner, std::string("Your ") + KINFO[pb.k].name + " could not be placed after clearing" +
                                    (err && err < P_Count ? std::string(" (") + PLACE_TEXT[err] + ")" : std::string()) +
                                    ". Cost refunded.");
        }
    }
    for (auto pid : done)
        pending.erase(pid);
}

// Drops pawn links whose pawn no longer works the tile; marks owners that still have a pawn at work.
void ClearingSystem::dropLostPawns(bool *busy)
{
    for (auto &[key, j] : clearJobs)
    {
        if (j.pawn)
        {
            auto e = world.find(j.pawn);
            const ClearTask *ct = e == entt::null ? nullptr : reg.try_get<ClearTask>(e);
            if (!ct || ct->tile != key)
            {
                j.pawn = 0;
                j.state = 0;
                j.since = world.clock;
            }
        }
        if (j.pawn)
            busy[uint8_t(j.owner)] = true;
    }
}

void ClearingSystem::sortJobs(const bool *busy, std::vector<int64_t> &done, std::vector<int64_t> &stale)
{
    for (auto &[key, j] : clearJobs)
    {
        if (!j.pawn && (busy[uint8_t(j.owner)] || std::any_of(j.want.begin(), j.want.end(),
                                                              [&](uint32_t w) { return world.find(w) != entt::null; })))
            j.since = world.clock;
        if (j.pawn && (j.state == 2 || j.total <= 0.f) && j.t >= j.total)
            done.push_back(key);
        else if (!j.pawn && world.clock - j.since > NO_PAWN_TIMEOUT)
            stale.push_back(key);
    }
}

void ClearingSystem::assignPawns()
{
    for (auto &[key, j] : clearJobs)
    {
        if (j.pawn)
            continue;
        auto e = pickPawn(j, tileCenter({keyCx(key), keyCy(key)}));
        if (e != entt::null)
            assign(key, j, e);
    }
    std::erase_if(manualUntil, [&](auto &kv) { return kv.second < world.clock; });
}

void ClearingSystem::clearTick(float)
{
    // Job timers advance in pawnTick, only while a pawn works.
    std::vector<int64_t> done, stale;
    bool busy[256]{}; // owners with a pawn at work: their queued jobs are not stale
    dropLostPawns(busy);
    sortJobs(busy, done, stale);
    for (auto key : done)
        finishJob(key);
    for (auto key : stale)
        cancelJob(key, "no free pawn reached the spot");

    if (world.tick % 10 == 0)
        assignPawns(); // twice a second
    if (world.tick % 20 == 5)
        pendingTick();

    if (!world.clearedNew.empty())
    {
        for (int id = 1; id <= MAX_PLAYERS; id++)
            if (world.pl[id].online)
                sendClearedList(id, world.clearedNew, false);
        world.clearedNew.clear();
    }
    if (world.tick % 4 == 0 && (!clearJobs.empty() || !pending.empty() || jobsWereSent))
        sendJobs();
}

// S_ClearJobs: u16 n, n x (i32 x, i32 y, u8 pct, u8 state) + u16 m, m x (u8 kind, i32 tx, i32 ty) own buildings waiting
// for clearing
void ClearingSystem::sendJobs()
{
    jobsWereSent = !clearJobs.empty() || !pending.empty();
    Writer w;
    w.put<uint8_t>(S_ClearJobs).put<uint16_t>(uint16_t(std::min<size_t>(clearJobs.size(), 400)));
    size_t n = 0;
    for (auto &[key, j] : clearJobs)
    {
        if (n++ >= 400)
            break;
        float pct = j.total > 0 ? j.t / j.total : 1.f;
        w.put<int32_t>(keyCx(key))
            .put<int32_t>(keyCy(key))
            .put<uint8_t>(uint8_t(std::clamp(pct, 0.f, 1.f) * 100.f))
            .put<uint8_t>(j.state);
    }
    for (int id = 1; id <= MAX_PLAYERS; id++)
    {
        if (!world.pl[id].online)
            continue;
        Writer pw = w;
        uint16_t m = 0;
        for (auto &[pid, pb] : pending)
            if (pb.owner == id && m < 60)
                m++;
        pw.put<uint16_t>(m);
        uint16_t k = 0;
        for (auto &[pid, pb] : pending)
            if (pb.owner == id && k < m)
            {
                pw.put<uint8_t>(pb.k).put<int32_t>(pb.tx).put<int32_t>(pb.ty);
                k++;
            }
        world.net.send(world.pl[id].conn, pw);
    }
}

void ClearingSystem::sendDrop(Pos p, int res, int amt, int credited)
{
    // Visible bouncing "+N" drop for everyone who can see the spot
    Writer w;
    w.put<uint8_t>(S_Drop)
        .put<float>(p.x)
        .put<float>(p.y)
        .put<uint8_t>(uint8_t(res))
        .put<uint16_t>(uint16_t(std::clamp(amt, 0, 65000)));
    for (int id = 1; id <= MAX_PLAYERS; id++)
    {
        if (!world.pl[id].online)
            continue;
        if (id != credited)
        {
            // Vision sources are rebuilt at most once per tick per player
            if (dropSrcTick[id] != world.tick)
            {
                dropSrc[id] = world.sourcesFor(id);
                dropSrcTick[id] = world.tick;
            }
            if (!seen(dropSrc[id], p.x, p.y, 2.f))
                continue;
        }
        world.net.send(world.pl[id].conn, w);
    }
}

void ClearingSystem::creditDrop(int player, Pos p, int res, int amt)
{
    world.creditRes(player, res, float(amt)); // within the resource's storage share
    sendDrop(p, res, amt, player);
}

void ClearingSystem::sendClearedList(int id, const std::vector<int64_t> &tiles, bool full)
{
    size_t i = 0;
    do
    {
        // Batches keep packets small
        size_t n = std::min<size_t>(3000, tiles.size() - i);
        Writer w;
        w.put<uint8_t>(S_Cleared)
            .put<uint8_t>(full ? (i == 0 ? 0 : 2) : 1)
            .put<uint32_t>(uint32_t(n)); // 0 = replace, 2 = append, 1 = delta
        for (size_t k = 0; k < n; k++)
            w.put<int32_t>(keyCx(tiles[i + k])).put<int32_t>(keyCy(tiles[i + k]));
        world.net.send(world.pl[id].conn, w);
        i += n;
    } while (i < tiles.size());
}

void ClearingSystem::sendClearedFull(int id)
{
    // Hook for onHello
    std::vector<int64_t> v(world.cleared.begin(), world.cleared.end());
    sendClearedList(id, v, true);
}

int ClearingSystem::pendingOf(int id, Kind k) const
{
    int n = 0;
    for (auto &[pid, pb] : pending)
        if (pb.owner == id && pb.k == k)
            n++;
    return n;
}

// gold deposits boost mines
float ClearingSystem::mineMul(entt::entity e, Kind k, Pos c)
{
    // Output multiplier from nearby gold deposits (cached per clear-version)
    if (!hasTag(k, KT_GoldBoosted))
        return 1.f;
    uint32_t uid = reg.get<Uid>(e).id;
    auto it = mineCache.find(uid);
    if (it != mineCache.end() && it->second.first == world.clearVer)
        return it->second.second;
    WorldAdapter wa{world};
    float m = 1.f + goldBonusOf(goldNear(wa, c));
    mineCache[uid] = {world.clearVer, m};
    static const bool logIt = std::getenv("BW_WORLDLOG") != nullptr; // test hook
    if (logIt)
        spdlog::info("[world] mine {} at {:.1f},{:.1f}: {} gold deposits -> output x{:.2f}", uid, c.x, c.y,
                     goldNear(wa, c), m);
    return m;
}
