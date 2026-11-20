/**
 * @file HuntSystem.cpp
 * @brief Wildlife: sheep spawning / repopulation and free pawns hunting them.
 */
#include "systems/HuntSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void HuntSystem::spawnChunkNpcs(int cx, int cy)
{
    if (!world.spawned.insert(chunkKey(cx, cy)).second)
        return;

    int b = world.biome(cx * CH + 8, cy * CH + 8);
    if (b != B_Plains && b != B_Flowers)
        return;

    float sd = b == B_Plains ? cfg().sheepDensityPlains : cfg().sheepDensityFlowers;
    int n = int(float(hash2(cx, cy, world.seed + 901) % 4) * sd + .5f);
    for (int i = 0; i < n; i++)
    {
        int x = cx * CH + int(hash2(cx, cy * 7 + i, world.seed + 5) % CH),
            y = cy * CH + int(hash2(cx * 5 + i, cy, world.seed + 6) % CH);
        if (world.walkable(x, y, false, 0) && (b == world.biome(x, y)))
            world.makeUnit(K_Sheep, 0, float(x) + .5f, float(y) + .5f);
    }
}

entt::entity HuntSystem::nearestOwnStorage(int ow, Pos p, bool needRoom)
{
    entt::entity best = entt::null;
    float bd = 1e9f;

    reg.view<Info, Building, Owner>().each([&](entt::entity st, const Info &i, const Building &sb, const Owner &so)
    {
        if (!isStorage(i.kind) || sb.progress < 1.f || so.id != ow ||
            (needRoom && world.capOf(st) - world.stockOf(st) < 1.f))
            return;

        Pos c = footCenter(i.kind, sb.tx, sb.ty);
        float d = std::hypot(p.x - c.x, p.y - c.y);
        if (d < bd)
        {
            bd = d;
            best = st;
        }
    });

    return best;
}

void HuntSystem::huntPause(entt::entity e)
{
    reg.get_or_emplace<Hunt>(e).pause = world.clock + 25;
}

bool HuntSystem::isFreePawn(entt::entity e) const
{
    return !reg.all_of<Worker>(e) && !reg.all_of<ClearTask>(e) && !reg.all_of<Convoy>(e) && !reg.all_of<Haul>(e) &&
           !reg.all_of<Raider>(e);
}

uint8_t HuntSystem::worldAux(entt::entity e) const
{
    const Hunt *h = reg.try_get<Hunt>(e);
    return h && h->meat > 0 ? uint8_t(R_Meat + 1) : uint8_t(0);
}

bool HuntSystem::huntKill(entt::entity sheep, int killer)
{
    const Hit *hit = reg.try_get<Hit>(sheep);
    if (!hit)
        return false;

    auto he = world.find(hit->by);
    if (he == entt::null || !reg.all_of<Hunt>(he) || reg.get<Owner>(he).id != killer)
        return false;

    reg.get<Hunt>(he).meat += 12.f;
    return true;
}

bool HuntSystem::huntTick(entt::entity e, Unit &u, int ow, Pos &p, float dt)
{
    if (!isFreePawn(e))
        return false;

    auto &h = reg.get_or_emplace<Hunt>(e);
    if (world.clock < h.pause || !world.pl[ow].online)
    {
        s.combat.fight(e, u, K_Pawn, ow, p, dt);
        return true;
    }

    if (h.meat > 0.f)
    {
        carryMeat(e, u, h, ow, p);
        return true;
    }
    if (reg.all_of<Attack>(e))
    {
        s.combat.fight(e, u, K_Pawn, ow, p, dt);
        return true;
    }

    if (world.clock < h.scan)
        return true;
    h.scan = world.clock + 1.5 + double(world.rng() % 100) / 100.0;

    huntSheep(e, ow, p);
    return true;
}

// Carry the meat to the nearest storage with room.
void HuntSystem::carryMeat(entt::entity e, Unit &u, Hunt &h, int ow, Pos p)
{
    reg.remove<Attack>(e);

    auto st = world.find(h.storage);
    if (st == entt::null || !world.complete(st) || world.capOf(st) - world.stockOf(st) < 1.f)
    {
        st = nearestOwnStorage(ow, p, true);
        h.storage = st == entt::null ? 0 : reg.get<Uid>(st).id;
        u.path.clear();
    }
    if (st == entt::null)
        return; // every storage is full: wait

    if (world.rectDist(p, st) <= 1.6f)
    {
        float cargo[R_Count] = {0, 0, 0, h.meat};
        world.storeCargo(st, cargo);
        h.meat = cargo[R_Meat] < .5f ? 0.f : cargo[R_Meat];
        h.storage = 0;
        u.path.clear();
    }
    else if (u.path.empty() && world.clock >= h.scan)
    {
        h.scan = world.clock + 2.0;
        if (!world.order(e, world.posOf(st), reg.get<Uid>(st).id, 12000))
            h.storage = 0;
    }
}

// Pick the nearest unclaimed sheep close to home and send the pawn after it.
void HuntSystem::huntSheep(entt::entity e, int ow, Pos p)
{
    auto home = nearestOwnStorage(ow, p, false);
    if (home == entt::null)
        return;

    Pos hp = world.posOf(home);
    uint32_t myUid = reg.get<Uid>(e).id, bestUid = 0;
    float bd = 25.f;
    Pos bp{0, 0};

    for (auto &sh : sheepList)
    {
        float d = std::hypot(sh.p.x - p.x, sh.p.y - p.y);
        if (d >= bd || std::hypot(sh.p.x - hp.x, sh.p.y - hp.y) > 45.f)
            continue;

        auto cl = sheepClaim.find(sh.uid);
        if (cl != sheepClaim.end() && cl->second != myUid)
        {
            auto he = world.find(cl->second);
            if (he != entt::null && reg.all_of<Attack>(he) && reg.get<Attack>(he).target == sh.uid)
                continue;
        }
        if (world.find(sh.uid) == entt::null || world.dying.count(sh.uid))
            continue;

        bd = d;
        bestUid = sh.uid;
        bp = sh.p;
    }
    if (!bestUid)
        return;

    if (world.order(e, bp))
    {
        reg.emplace_or_replace<Attack>(e, Attack{bestUid});
        sheepClaim[bestUid] = myUid;
    }
}

void HuntSystem::sheepSecond()
{
    sheepList.clear();
    std::unordered_map<int64_t, int> perChunk;
    reg.view<Info, Pos, Uid>().each([&](const Info &i, const Pos &p, const Uid &u)
    {
        if (!hasTag(i.kind, KT_Animal))
            return;
        sheepList.push_back({u.id, p});
        perChunk[chunkKey(chunkOf(int(std::floor(p.x))), chunkOf(int(std::floor(p.y))))]++;
    });
    std::erase_if(sheepClaim,
                  [&](auto &kv) { return world.find(kv.first) == entt::null || world.find(kv.second) == entt::null; });

    if (world.clock < nextRepop)
        return;
    nextRepop = world.clock + 8.0;
    if (int(sheepList.size()) >= cfg().sheepMax)
        return;

    for (auto k : awakeChunks())
        repopulateChunk(k, perChunk);
}

// Chunks around online players' things.
std::unordered_set<int64_t> HuntSystem::awakeChunks()
{
    std::unordered_set<int64_t> awake;
    reg.view<Owner, Info>().each([&](entt::entity e, const Owner &o, const Info &)
    {
        if (!isPlayer(o.id) || !world.pl[o.id].online)
            return;
        Pos q = world.posOf(e);
        int cx = chunkOf(int(std::floor(q.x))), cy = chunkOf(int(std::floor(q.y)));
        for (int y = cy - 2; y <= cy + 2; y++)
            for (int x = cx - 2; x <= cx + 2; x++)
                awake.insert(chunkKey(x, y));
    });
    return awake;
}

void HuntSystem::repopulateChunk(int64_t k, const std::unordered_map<int64_t, int> &perChunk)
{
    if (!world.spawned.count(k))
        return;

    int cx = keyCx(k), cy = keyCy(k);
    int b = world.biome(cx * CH + 8, cy * CH + 8);
    if (b != B_Plains && b != B_Flowers)
        return;

    auto it = perChunk.find(k);
    if (it != perChunk.end() && it->second >= 4)
        return;
    if (world.rng() % 100 >= uint32_t(45.f * (b == B_Plains ? cfg().sheepDensityPlains : cfg().sheepDensityFlowers)))
        return;

    for (int tries = 0; tries < 6; tries++)
    {
        int x = cx * CH + int(world.rng() % CH), y = cy * CH + int(world.rng() % CH);
        if (world.walkable(x, y, false, 0) && world.biome(x, y) == b)
        {
            world.makeUnit(K_Sheep, 0, float(x) + .5f, float(y) + .5f);

            static const bool logIt = std::getenv("BW_WORLDLOG") != nullptr;
            if (logIt)
                std::printf("[world] sheep repopulated in chunk %d,%d (total %zu)\n", cx, cy, sheepList.size() + 1);
            break;
        }
    }
}
