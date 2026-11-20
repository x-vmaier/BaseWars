/**
 * @file ClientWorld.cpp
 * @brief Replicated world state on the client plus the read-only queries renderers and UI share.
 */
#include "client/ClientWorld.hpp"

#include <algorithm>
#include <cmath>

int ClientWorld::biomeAt(int x, int y) const
{
    auto it = chunks.find(chunkKey(chunkOf(x), chunkOf(y)));
    return it == chunks.end() || !it->second.hasTerr ? INVALID_BIOME : it->second.biome[localIdx(x, y)];
}

bool ClientWorld::exploredAt(int x, int y) const
{
    auto it = chunks.find(chunkKey(chunkOf(x), chunkOf(y)));
    if (it == chunks.end() || !it->second.hasFog)
        return false;
    int i = localIdx(x, y);
    return it->second.fog[i >> 3] >> (i & 7) & 1;
}

uint8_t ClientWorld::featHere(int x, int y) const
{
    int b = biomeAt(x, y);
    if (!isLand(b) || isCliffTile(b, biomeAt(x, y - 1)))
        return F_None;
    return featureAt(x, y, b, seed);
}

bool ClientWorld::featBlocking(int x, int y) const
{
    int64_t k = chunkKey(x, y);
    return !clearedT.count(k) && !occ.count(k) && featureBlocks(featHere(x, y));
}

uint8_t ClientWorld::featVisible(int x, int y) const
{
    int64_t k = chunkKey(x, y);
    if (!exploredAt(x, y) || clearedT.count(k) || occ.count(k))
        return F_None;
    return featHere(x, y);
}

std::vector<IV> ClientWorld::blockedUnder(Kind k, IV t) const
{
    std::vector<IV> v;
    for (int y = t.y; y < t.y + KINFO[k].fh; y++)
        for (int x = t.x; x < t.x + KINFO[k].fw; x++)
            if (featBlocking(x, y))
                v.push_back({x, y});
    return v;
}

int ClientWorld::clearWoodCost(const std::vector<IV> &v) const
{
    int c = 0;
    for (auto t : v)
        c += clearCostWood(featHere(t.x, t.y));
    return c;
}

namespace
{
struct GoldAdapter
{
    // adapter for the shared goldNear()
    const ClientWorld &w;
    int biome(int x, int y) { return w.biomeAt(x, y); }
    bool cleared(int x, int y) { return w.clearedT.count(chunkKey(x, y)) > 0; }
    uint32_t seedValue() { return w.seed; }
};
} // namespace

int ClientWorld::goldNear(Pos c) const
{
    GoldAdapter a{*this};
    return ::goldNear(a, c);
}

bool ClientWorld::coastNearShip(const RPos &p) const
{
    int cx = int(std::floor(p.x)), cy = int(std::floor(p.y));
    for (int y = cy - 2; y <= cy + 2; y++)
        for (int x = cx - 2; x <= cx + 2; x++)
        {
            int b = biomeAt(x, y);
            if (isLand(b) && !isCliffTile(b, biomeAt(x, y - 1)))
                return true;
        }
    return false;
}

const Peer *ClientWorld::peerOf(int id) const
{
    for (auto &p : roster)
        if (p.id == id)
            return &p;
    return nullptr;
}

int ClientWorld::myClan() const
{
    auto p = peerOf(me);
    return p ? p->clan : 0;
}

bool ClientWorld::alliedWith(int owner) const
{
    if (owner == me)
        return true;
    if (isFactionOwner(owner))
        return false;
    auto p = peerOf(owner);
    return p && ((p->flags & 2) || (p->clan && p->clan == myClan()));
}

bool ClientWorld::hostileTo(int owner) const
{
    if (isFactionOwner(owner))
        return facRep[owner - FACTION_BASE] <= HOSTILE_REP;
    return owner != me && !alliedWith(owner);
}

std::string ClientWorld::nameOf(int owner) const
{
    if (owner == me)
        return myName;
    if (isFactionOwner(owner))
        return FACTION_NAME[owner - FACTION_BASE];
    auto p = peerOf(owner);
    return p ? p->name : (owner ? "Player " + std::to_string(owner) : "Wildlife");
}

std::string ClientWorld::clanName(int c) const
{
    for (auto &k : clansList)
        if (k.id == c)
            return k.name;
    return "";
}

int ClientWorld::leaderClanOf(int owner) const
{
    for (auto &c : clansList)
        if (c.leader == owner)
            return c.id;
    return 0;
}

bool ClientWorld::hostileTargetId(uint32_t id) const
{
    auto it = ents.find(id);
    if (it == ents.end())
        return false;
    const Info &i = reg.get<Info>(it->second);
    return i.owner == 0 ? hasTag(i.kind, KT_Animal) : hostileTo(i.owner);
}

bool ClientWorld::attackable(uint32_t id) const
{
    auto it = ents.find(id);
    if (it == ents.end())
        return false;
    const Info &i = reg.get<Info>(it->second);
    return i.owner != me && !alliedWith(i.owner);
}

void ClientWorld::rebuildOcc()
{
    occ.clear();
    reg.view<Info, Goal>().each([&](const Info &i, const Goal &g)
    {
        if (!isBuilding(i.kind))
            return;
        int tx = int(std::lround(g.x - float(KINFO[i.kind].fw) * .5f)),
            ty = int(std::lround(g.y - float(KINFO[i.kind].fh) * .5f));
        for (int y = ty; y < ty + KINFO[i.kind].fh; y++)
            for (int x = tx; x < tx + KINFO[i.kind].fw; x++)
                occ.insert(chunkKey(x, y));
    });
}

bool ClientWorld::visibleSource(float x, float y) const
{
    bool v = false;
    reg.view<Info, Goal>().each([&](const Info &i, const Goal &g)
    {
        if (v || !alliedWith(i.owner) || (isBuilding(i.kind) && i.prog < 100))
            return;
        float r = float(KINFO[i.kind].vision) - 1.5f;
        if (r > 0 && std::hypot(g.x - x, g.y - y) < r)
            v = true;
    });
    return v;
}

bool ClientWorld::nearStorage(float x, float y) const
{
    bool v = false;
    reg.view<Info, Goal>().each([&](const Info &i, const Goal &g)
    {
        if (v || !isStorage(i.kind))
            return;
        float hw = float(KINFO[i.kind].fw) * .5f + 2.5f, hh = float(KINFO[i.kind].fh) * .5f + 2.5f;
        if (std::abs(g.x - x) <= hw && std::abs(g.y - y) <= hh)
            v = true;
    });
    return v;
}

Pos ClientWorld::myTH(Vector2 camTarget) const
{
    Pos out{camTarget.x / TILE, camTarget.y / TILE};
    reg.view<Goal, Info>().each([&](const Goal &g, const Info &i)
    {
        if (i.kind == K_TownHall && i.owner == me)
            out = {g.x, g.y};
    });
    return out;
}

bool ClientWorld::affordable(const float *cost) const
{
    for (int r = 0; r < R_Count; r++)
        if (res[r] + 1e-3f < cost[r])
            return false;
    return true;
}

int ClientWorld::thLevel() const
{
    int lv = 0;
    reg.view<Info>().each([&](const Info &i)
    {
        if (i.kind == K_TownHall && i.owner == me && i.prog >= 100 && !(i.state & BLD_MOVING))
            lv = std::max<int>(lv, i.aux);
    });
    return lv;
}

int ClientWorld::countKind(Kind k) const
{
    int n = 0;
    reg.view<Info>().each([&](const Info &i)
    {
        if (i.kind == k && i.owner == me && !(i.state & BLD_MOVING))
            n++;
    });
    for (auto &ps : pendingSites)
        if (ps.kind == k)
            n++;
    return n;
}

int ClientWorld::buildGateErr(Kind k) const
{
    return buildGate(k, thLevel(), countKind(k));
}

float ClientWorld::donateWait() const
{
    return float(std::max(0.0, donateReadyAt - GetTime()));
}

const BJob *ClientWorld::jobOf(uint32_t id) const
{
    auto it = jobs.find(id);
    return it == jobs.end() ? nullptr : &it->second;
}

void ClientWorld::clear()
{
    reg.clear();
    ents.clear();
    chunks.clear();
    occ.clear();
    roster.clear();
    myRaw.clear();
    clansList.clear();
    facTowns.clear();
    fxs.clear();
    ghosts.clear();
    pendingSites.clear();
    jobs.clear();
    markers.clear();
    ripples.clear();
    me = 0;
    stamp = 0;
}

void ClientWorld::interpolate(float dt)
{
    reg.view<RPos, Goal, Face, Info>().each([&](RPos &p, const Goal &g, Face &f, const Info &in)
    {
        float k = std::min(1.f, 12.f * dt);
        float dx = g.x - p.x;
        p.x += dx * k;
        p.y += (g.y - p.y) * k;
        if ((in.state & 7) == US_Attack || (in.state & 7) == US_Heal)
            f.left = std::cos(float(in.aim) / 255.f * 6.28319f - 3.14159f) < 0;
        else if (std::abs(dx) > .004f)
            f.left = dx < 0;
    });
}
