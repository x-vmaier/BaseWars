/**
 * @file Replication.cpp
 * @brief Outgoing world state: S_State, S_Fog, S_Roster, S_Map, S_Queue, S_BaseInfo, S_ShipInfo, S_Paths.
 */
#include "systems/Replication.hpp"

#include "common/Snapshot.hpp"
#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// One snapshot = several independent unreliable chunks (each below SNAPSHOT_MTU); a lost chunk only delays its
// entities until the next snapshot, which is complete again (full state, no deltas).
void Replication::sendState(int id)
{
    Profile &p = world.pl[id];
    auto src = world.sourcesFor(id);
    SrcGrid grid(src);

    Writer globals;
    for (int r = 0; r < R_Count; r++)
        globals.put<float>(p.res[r]);
    for (int f = 0; f < FACTION_COUNT; f++)
        globals.put<int8_t>(int8_t(std::lround(p.rep[f])));
    for (int k = K_Pawn; k <= K_Monk; k++)
        globals.put<uint8_t>(uint8_t(std::max<int>(1, p.ulevel[k])));

    struct Chunk
    {
        Writer ents, fx;
        uint16_t nEnt = 0, nFx = 0;
    };
    std::vector<Chunk> chunks(1);
    // Returns the chunk with room for `add` more bytes (a new one if needed), or null at the chunk cap.
    auto roomFor = [&](size_t add) -> Chunk *
    {
        auto used = [&](const Chunk &c)
        { return SNAP_HEADER_BYTES + (&c == &chunks[0] ? globals.b.size() : 0) + 4 + c.ents.b.size() + c.fx.b.size(); };
        if (used(chunks.back()) + add > SNAPSHOT_MTU)
        {
            if (int(chunks.size()) >= SNAPSHOT_MAX_CHUNKS)
                return nullptr;
            chunks.emplace_back();
        }
        return &chunks.back();
    };

    Writer rec;
    reg.view<Uid, Owner, Info, Health>().each(
        [&](entt::entity e, const Uid &uid, const Owner &o, const Info &in, const Health &h)
    {
        rec.b.clear();
        if (!writeStateEntity(rec, id, grid, e, uid, o, in, h))
            return;
        if (Chunk *c = roomFor(rec.b.size()))
        {
            c->ents.bytes(rec.b.data(), rec.b.size());
            c->nEnt++;
        }
    });
    for (auto &f : world.effects)
        if (grid.seen(f.x, f.y, 3.f) || grid.seen(f.x2, f.y2, 3.f))
            if (Chunk *c = roomFor(17))
            {
                c->fx.put<uint8_t>(f.type).put<float>(f.x).put<float>(f.y).put<float>(f.x2).put<float>(f.y2);
                c->nFx++;
            }

    for (size_t i = 0; i < chunks.size(); i++)
    {
        Writer w;
        putSnapHeader(w, {uint32_t(world.tick), p.lastInputSeq, uint8_t(i), uint8_t(chunks.size())});
        if (i == 0)
            w.bytes(globals.b.data(), globals.b.size());
        w.put<uint16_t>(chunks[i].nEnt);
        w.bytes(chunks[i].ents.b.data(), chunks[i].ents.b.size());
        w.put<uint16_t>(chunks[i].nFx);
        w.bytes(chunks[i].fx.b.data(), chunks[i].fx.b.size());
        world.net.send(p.conn, w, false);
    }
}

bool Replication::writeStateEntity(Writer &w, int id, const SrcGrid &grid, entt::entity e, const Uid &uid,
                                   const Owner &o, const Info &in, const Health &h)
{
    Pos ps = world.posOf(e);
    // Test hook: show every entity.
    static const bool seeAll = std::getenv("BW_SEEALL") != nullptr;
    const Ruin *ruin = reg.try_get<Ruin>(e);
    if (!seeAll && !(ruin && ruin->victim == id) && !world.allied(id, o.id) &&
        !grid.seen(ps.x, ps.y, isBuilding(in.kind) ? 2.f : 0.f) &&
        !((reg.all_of<Convoy>(e) || reg.all_of<Haul>(e)) && s.convoy.spottedForClan(id, uid.id)))
        return false;

    uint8_t state = 0, aux = 0, aim = 0, prog = 100;
    uint16_t stock = 0;
    if (const Unit *u = reg.try_get<Unit>(e))
        unitFields(e, *u, state, aux, aim);
    else
        buildingFields(e, in, state, aux, aim, prog, stock);
    if (!s.naval.shipStateHook(id, e, o.id, in, aux, prog, stock))
        return false;

    w.put<uint32_t>(uid.id).put<uint8_t>(in.kind).put<uint8_t>(o.id).put<float>(ps.x).put<float>(ps.y);
    w.put<uint16_t>(uint16_t(std::max(0.f, h.hp))).put<uint16_t>(uint16_t(h.maxhp));
    w.put<uint8_t>(state).put<uint8_t>(aux).put<uint8_t>(aim).put<uint8_t>(prog).put<uint16_t>(stock);
    return true;
}

void Replication::unitFields(entt::entity e, const Unit &u, uint8_t &state, uint8_t &aux, uint8_t &aim)
{
    state = u.state;
    if (reg.all_of<Convoy>(e) || reg.all_of<Haul>(e))
        state |= UNIT_CONVOY_FLAG;
    if (reg.all_of<Support>(e))
        state |= UNIT_SUPPORT_FLAG;
    aim = u.aim;

    if (const Worker *wk = reg.try_get<Worker>(e))
    {
        // aux names the resource the worker carries most of.
        float best = 0;
        for (int r = 0; r < R_Count; r++)
            if (wk->cargo[r] > best)
            {
                best = wk->cargo[r];
                aux = uint8_t(r + 1);
            }
    }
    else
        aux = s.hunt.worldAux(e);
}

void Replication::buildingFields(entt::entity e, const Info &in, uint8_t &state, uint8_t &aux, uint8_t &aim,
                                 uint8_t &prog, uint16_t &stock)
{
    const Building &b = reg.get<Building>(e);
    state = world.bflagsOf(e);
    aux = in.level;
    aim = b.haulPct;
    prog = uint8_t(std::min(1.f, b.progress) * 100.f);

    float tot = 0;
    for (float amount : b.stock)
        tot += amount;
    stock = uint16_t(std::min(tot, 65000.f));
}

void Replication::sendFog(int id)
{
    Profile &p = world.pl[id];
    std::unordered_map<int64_t, Bits> uni = sharedExplored(id);

    std::vector<std::pair<int64_t, Bits>> fogOut;
    std::vector<int64_t> terrOut;
    for (auto &[k, b] : uni)
    {
        auto it = p.sentFog.find(k);
        if (it != p.sentFog.end() && it->second == b)
            continue;
        if (fogOut.size() >= 150)
            break;
        fogOut.push_back({k, b});
        p.sentFog[k] = b;

        // Terrain of the chunk and its neighbours is sent once alongside the fog.
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
            {
                int64_t nk = chunkKey(keyCx(k) + dx, keyCy(k) + dy);
                if (p.sentTerr.insert(nk).second)
                    terrOut.push_back(nk);
            }
    }
    if (fogOut.empty())
        return;

    Writer w;
    w.put<uint8_t>(S_Fog).put<uint16_t>(uint16_t(terrOut.size()));
    for (auto k : terrOut)
    {
        int cx = keyCx(k), cy = keyCy(k);
        world.biome(cx * CH, cy * CH);
        w.put<int32_t>(cx).put<int32_t>(cy);
        w.bytes(world.terr[k].data(), CH * CH);
    }
    w.put<uint16_t>(uint16_t(fogOut.size()));
    for (auto &[k, b] : fogOut)
    {
        w.put<int32_t>(keyCx(k)).put<int32_t>(keyCy(k));
        w.bytes(b.data(), 32);
    }
    world.net.send(p.conn, w);
}

std::unordered_map<int64_t, Bits> Replication::sharedExplored(int id)
{
    std::unordered_map<int64_t, Bits> uni = world.pl[id].expl;
    for (int a = 1; a <= MAX_PLAYERS; a++)
        if (a != id && world.pl[a].used && world.shares(a, id))
            for (auto &[k, b] : world.pl[a].expl)
            {
                Bits &u = uni.try_emplace(k, Bits{}).first->second;
                for (size_t i = 0; i < 32; i++)
                    u[i] |= b[i];
            }
    return uni;
}

void Replication::sendRoster()
{
    Pos thPos[MAX_PLAYERS + 1];
    bool hasTh[MAX_PLAYERS + 1]{};
    reg.view<Info, Building, Owner>().each([&](const Info &i, const Building &b, const Owner &o)
    {
        if (i.kind == K_TownHall && isPlayer(o.id) && !hasTh[o.id])
        {
            hasTh[o.id] = true;
            thPos[o.id] = footCenter(i.kind, b.tx, b.ty);
        }
    });

    for (int id = 1; id <= MAX_PLAYERS; id++)
    {
        if (!world.pl[id].online)
            continue;
        Writer w;
        w.put<uint8_t>(S_Roster);
        writeRosterPlayers(w, id, thPos, hasTh);
        writeRosterClans(w);
        world.net.send(world.pl[id].conn, w);
    }
}

void Replication::writeRosterPlayers(Writer &w, int id, const Pos *thPos, const bool *hasTh)
{
    uint8_t n = 0;
    size_t cp = w.b.size();
    w.put<uint8_t>(0);
    for (int o = 1; o <= MAX_PLAYERS; o++)
    {
        if (!world.pl[o].used)
            continue;
        uint8_t flags = uint8_t((world.pl[o].online ? 1 : 0) | (world.pl[id].allied[o] ? 2 : 0) |
                                (world.pl[o].pending[id] ? 4 : 0) | (world.pl[id].pending[o] ? 8 : 0));
        w.put<uint8_t>(uint8_t(o)).put<uint8_t>(flags).put<uint8_t>(world.pl[o].clan);
        w.str(world.pl[o].name);

        // A Town Hall position is only revealed to allies and to players who explored it.
        bool known = hasTh[o] && (o == id || world.shares(id, o) ||
                                  world.explored(id, int(std::floor(thPos[o].x)), int(std::floor(thPos[o].y))));
        w.put<uint8_t>(known ? 1 : 0).put<float>(known ? thPos[o].x : 0.f).put<float>(known ? thPos[o].y : 0.f);
        n++;
    }
    w.b[cp] = n;
}

void Replication::writeRosterClans(Writer &w)
{
    uint8_t nc = 0;
    size_t cc = w.b.size();
    w.put<uint8_t>(0);
    for (int c = 1; c <= MAX_CLANS; c++)
    {
        if (!world.clans[c].used)
            continue;
        int members = 0;
        for (int o = 1; o <= MAX_PLAYERS; o++)
            if (world.pl[o].used && world.pl[o].clan == c)
                members++;
        w.put<uint8_t>(uint8_t(c));
        w.str(world.clans[c].name);
        w.put<uint8_t>(uint8_t(members)).put<uint8_t>(world.clans[c].leader);
        nc++;
    }
    w.b[cc] = nc;
}

void Replication::sendMap(int id)
{
    Writer w;
    w.put<uint8_t>(S_Map);
    uint8_t n = 0;
    size_t cp = w.b.size();
    w.put<uint8_t>(0);
    for (auto &t : world.towns)
    {
        if (!t.alive || !world.explored(id, t.cx, t.cy))
            continue;
        w.put<uint8_t>(t.faction).put<float>(float(t.cx) + .5f).put<float>(float(t.cy) + .5f);
        if (++n == 255)
            break;
    }
    w.b[cp] = n;
    world.net.send(world.pl[id].conn, w);
}

void Replication::sendQueues(int id)
{
    Writer w;
    w.put<uint8_t>(S_Queue);
    size_t cp = w.b.size();
    w.put<uint8_t>(0);
    uint8_t n = 0;
    reg.view<TrainQ, Owner, Uid>().each([&](const TrainQ &q, const Owner &o, const Uid &uid)
    {
        if (o.id != id || q.q.empty() || n == 255)
            return;
        w.put<uint32_t>(uid.id)
            .put<uint8_t>(uint8_t(q.q.size()))
            .put<uint8_t>(uint8_t(std::clamp(q.q[0].t / std::max(.1f, q.q[0].total), 0.f, 1.f) * 100.f));
        for (auto &it : q.q)
            w.put<uint8_t>(it.k);
        n++;
    });
    w.b[cp] = n;
    world.net.send(world.pl[id].conn, w);
}

void Replication::sendBaseInfo(int id)
{
    uint8_t moving = 0, pct = 0;
    reg.view<RelocNew, Building, Owner>().each([&](const RelocNew &rn, const Building &b, const Owner &o)
    {
        if (rn.town && o.id == id)
        {
            moving = 1;
            pct = uint8_t(std::min(1.f, b.progress) * 100.f);
        }
    });
    Writer w;
    const BaseProf &b = world.bp[id];
    w.put<uint8_t>(S_BaseInfo)
        .put<uint8_t>(uint8_t(std::min(255, b.quotaTotal())))
        .put<uint8_t>(b.homeless ? 1 : 0)
        .put<uint8_t>(moving)
        .put<uint8_t>(pct);
    for (uint8_t q : b.quota)
        w.put<uint8_t>(q);
    for (uint8_t a : b.alloc)
        w.put<uint8_t>(a);
    world.net.send(world.pl[id].conn, w);
}

void Replication::sendShipInfo(int id)
{
    Writer w;
    w.put<uint8_t>(S_ShipInfo);
    size_t cp = w.b.size();
    w.put<uint8_t>(0);
    uint8_t n = 0;
    reg.view<Ship, Owner, Uid, Info>().each(
        [&](entt::entity e, const Ship &, const Owner &o, const Uid &uid, const Info &in)
    {
        if (o.id != id || n >= 60)
            return;
        int t, kinds[5];
        float rs;
        s.naval.paxStats(e, t, rs, kinds);
        float res[R_Count]{};
        for (auto q : s.naval.passengers(e))
            if (auto *h = reg.try_get<Haul>(q))
                for (int r = 0; r < R_Count; r++)
                    res[r] += h->cargo[r];
        w.put<uint32_t>(uid.id).put<uint8_t>(uint8_t(shipTier(in.level)));
        for (int k : kinds)
            w.put<uint8_t>(uint8_t(k));
        for (float v : res)
            w.put<uint16_t>(uint16_t(std::min(v, 65000.f)));
        w.put<uint8_t>(reg.all_of<Voyage>(e) ? 1 : 0);
        n++;
    });
    w.b[cp] = n;
    world.net.send(world.pl[id].conn, w);
}
namespace
{
constexpr size_t MAX_PATH_UNITS = 60;
constexpr uint16_t MAX_PATH_ENTRIES = 48;

// Thinned waypoints (at most 14) plus an FNV hash used to skip unchanged paths.
PathCand makePathCand(const Unit &u, const Pos &p, const Pos &cam, uint32_t uid)
{
    PathCand c{uid, std::hypot(p.x - cam.x, p.y - cam.y), {}, 0};
    size_t n = u.path.size(), stride = std::max<size_t>(3, (n + 11) / 12);
    for (size_t j = u.i; j < n; j++)
        if (j == n - 1 || (j + 1) % stride == 0)
            c.pts.push_back({int(std::floor(u.path[j].x)), int(std::floor(u.path[j].y))});
    if (c.pts.size() > 14)
        c.pts.resize(14);

    uint64_t h = 1469598103934665603ull ^ uid;
    for (auto &q : c.pts)
    {
        h = (h ^ uint64_t(uint32_t(q.x))) * 1099511628211ull;
        h = (h ^ uint64_t(uint32_t(q.y))) * 1099511628211ull;
    }
    c.h = h;
    return c;
}
} // namespace

// Path lines: waypoints of every unit / ship of the viewer and of players sharing vision with the viewer (never anybody
// else).
void Replication::sendPaths(int id)
{
    auto &sent = pathSent[id];
    if (!world.pl[id].online)
    {
        sent.clear();
        return;
    }

    std::vector<PathCand> cs = collectPaths(id);

    Writer w;
    w.put<uint8_t>(S_Paths);
    size_t cp = w.b.size();
    w.put<uint16_t>(0);
    uint16_t n = writePathDeltas(w, sent, cs);
    if (!n)
        return;

    std::memcpy(&w.b[cp], &n, 2);
    world.net.send(world.pl[id].conn, w);
}

std::vector<PathCand> Replication::collectPaths(int id)
{
    std::vector<PathCand> cs;
    Pos cam = world.pl[id].cam;
    reg.view<Unit, Owner, Pos, Uid>().each(
        [&](entt::entity e, const Unit &u, const Owner &o, const Pos &p, const Uid &uid)
    {
        if (!isPlayer(o.id) || (o.id != id && !world.shares(id, o.id)) || u.i >= u.path.size() || reg.all_of<Aboard>(e))
            return;
        cs.push_back(makePathCand(u, p, cam, uid.id));
    });

    std::sort(cs.begin(), cs.end(), [](const PathCand &a, const PathCand &b) { return a.d < b.d; });
    if (cs.size() > MAX_PATH_UNITS)
        cs.resize(MAX_PATH_UNITS);
    return cs;
}

uint16_t Replication::writePathDeltas(Writer &w, std::unordered_map<uint32_t, uint64_t> &sent,
                                      const std::vector<PathCand> &cs)
{
    std::unordered_set<uint32_t> active;
    for (auto &c : cs)
        active.insert(c.uid);

    uint16_t n = 0;

    // Paths that ended are cleared on the client.
    for (auto it = sent.begin(); it != sent.end() && n < MAX_PATH_ENTRIES;)
    {
        if (active.count(it->first))
        {
            ++it;
            continue;
        }
        w.put<uint32_t>(it->first).put<uint8_t>(0);
        n++;
        it = sent.erase(it);
    }

    for (auto &c : cs)
    {
        if (n >= MAX_PATH_ENTRIES)
            break;
        auto f = sent.find(c.uid);
        if (f != sent.end() && f->second == c.h)
            continue;
        sent[c.uid] = c.h;
        w.put<uint32_t>(c.uid).put<uint8_t>(uint8_t(c.pts.size()));
        for (auto &q : c.pts)
            w.put<int32_t>(q.x).put<int32_t>(q.y);
        n++;
    }
    return n;
}
