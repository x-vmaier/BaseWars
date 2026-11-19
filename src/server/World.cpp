#include "server/World.hpp"

#include "common/Progression.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <queue>

float baseTestSpeed()
{
    static const float v =
        std::getenv("BW_BASESPEED") ? std::max(1.f, float(std::atof(std::getenv("BW_BASESPEED")))) : 1.f;
    return v;
}

SrcGrid::SrcGrid(const std::vector<Source> &s)
{
    for (auto &v : s)
    {
        int x0 = floorDiv(int(std::floor(v.x - v.r - 3.f)), 16), x1 = floorDiv(int(std::floor(v.x + v.r + 3.f)), 16);
        int y0 = floorDiv(int(std::floor(v.y - v.r - 3.f)), 16), y1 = floorDiv(int(std::floor(v.y + v.r + 3.f)), 16);
        for (int cy = y0; cy <= y1; cy++)
            for (int cx = x0; cx <= x1; cx++)
                cells[chunkKey(cx, cy)].push_back(v);
    }
}
bool SrcGrid::seen(float x, float y, float pad) const
{
    auto it = cells.find(chunkKey(floorDiv(int(std::floor(x)), 16), floorDiv(int(std::floor(y)), 16)));
    if (it == cells.end())
        return false;
    for (auto &v : it->second)
        if ((x - v.x) * (x - v.x) + (y - v.y) * (y - v.y) <= (v.r + pad) * (v.r + pad))
            return true;
    return false;
}
bool seen(const std::vector<Source> &s, float x, float y, float pad)
{
    for (auto &v : s)
        if ((x - v.x) * (x - v.x) + (y - v.y) * (y - v.y) <= (v.r + pad) * (v.r + pad))
            return true;
    return false;
}

World::World(uint32_t sd, std::string path) : seed(sd), rng(sd), savePath(std::move(path)) {}

int World::biome(int x, int y)
{
    int64_t k = chunkKey(chunkOf(x), chunkOf(y));
    auto it = terr.find(k);
    if (it == terr.end())
    {
        std::array<uint8_t, CH * CH> c;
        for (int j = 0; j < CH; j++)
            for (int i = 0; i < CH; i++)
                c[size_t(j * CH + i)] = genBiome(chunkOf(x) * CH + i, chunkOf(y) * CH + j, seed);
        it = terr.emplace(k, c).first;
    }
    return it->second[size_t(localIdx(x, y))];
}
bool World::cliff(int x, int y)
{
    return isCliffTile(biome(x, y), biome(x, y - 1));
}
bool World::walkable(int x, int y, bool water, uint32_t ignore)
{
    int b = biome(x, y);
    if (water)
        return b == B_Ocean;
    if (!isLand(b) || isCliffTile(b, biome(x, y - 1)))
        return false;
    auto it = occ.find(tkey(x, y));
    return it == occ.end() || it->second == ignore;
}

bool World::sameClan(int a, int b) const
{
    return isPlayer(a) && isPlayer(b) && pl[a].clan && pl[a].clan == pl[b].clan;
}
bool World::shares(int a, int b) const
{
    return a != b && isPlayer(a) && isPlayer(b) && (pl[a].allied[b] || sameClan(a, b));
} // shared vision
bool World::allied(int a, int b) const
{
    return a == b || shares(a, b);
}
float World::repOf(int player, int owner) const
{
    return isPlayer(player) && isFactionOwner(owner) ? pl[player].rep[owner - FACTION_BASE] : 0.f;
}
bool World::hostile(int a, int b) const
{
    if (a < 1 || b < 1 || a == b)
        return false;
    if (isFactionOwner(a) && isFactionOwner(b))
        return factionsAtWar(a - FACTION_BASE, b - FACTION_BASE);
    if (isFactionOwner(a))
        return repOf(b, a) <= float(HOSTILE_REP);
    if (isFactionOwner(b))
        return repOf(a, b) <= float(HOSTILE_REP);
    return !allied(a, b);
}
bool World::factionsAtWar(int fa, int fb) const
{
    if (fa == fb || fa < 0 || fb < 0 || fa >= FACTION_COUNT || fb >= FACTION_COUNT)
        return false;
    return hash2(std::min(fa, fb), std::max(fa, fb), seed + 515) % 100 < uint32_t(cfg().factionWarPercent);
}
int World::ulv(int owner, Kind k) const
{
    return isPlayer(owner) ? std::max<int>(1, pl[owner].ulevel[k]) : 1;
}
float World::totalRes(int id) const
{
    float t = 0;
    for (float v : pl[id].res)
        t += v;
    return t;
}
float World::stockOf(entt::entity e) const
{ // the Town Hall stores the player's resources
    const auto &i = reg.get<Info>(e);
    int o = reg.get<Owner>(e).id;
    if (i.kind == K_TownHall && isPlayer(o))
        return totalRes(o);
    float t = 0;
    for (float v : reg.get<Building>(e).stock)
        t += v;
    return t;
}
float World::capOf(entt::entity e) const
{ // non-functional storage holds nothing; damaged storage holds less
    const auto &i = reg.get<Info>(e);
    return inactive(e) ? 0.f : storageCap(i.kind, i.level) * efficiency(e);
}
// puts as much of `cargo` as fits into storage building `st`; returns the amount stored. At a player's Town Hall each
// resource is also limited to its storage share (BaseProf::alloc): the part beyond it is discarded, not carried back.
float World::storeCargo(entt::entity st, float *cargo)
{
    int o = reg.get<Owner>(st).id;
    bool th = reg.get<Info>(st).kind == K_TownHall;
    if (th && isPlayer(o))
        for (int r = 0; r < R_Count; r++)
        {
            float over = cargo[r] - std::max(0.f, allocRoom(o, r));
            if (over > .01f)
            {
                cargo[r] -= over;
                noteDiscard(o, r, over);
            }
        }
    float tot = 0;
    for (int r = 0; r < R_Count; r++)
        tot += cargo[r];
    float room = capOf(st) - stockOf(st);
    if (tot <= 0 || room <= 0)
        return 0;
    float f = std::min(1.f, room / tot), stored = 0;
    for (int r = 0; r < R_Count; r++)
    {
        float add = cargo[r] * f;
        if (th)
            pl[o].res[r] += add;
        else
            reg.get<Building>(st).stock[r] += add;
        cargo[r] -= add;
        stored += add;
    }
    return stored;
}
float World::allocRoom(int id, int r)
{
    if (!isPlayer(id))
        return 1e9f;
    auto th = townHallOf(id);
    float cap = th == entt::null ? 0.f : capOf(th);
    if (cap <= 0.f)
        return 1e9f; // no working hall: shares are not enforced
    return allocCap(cap, bp[id].alloc[r]) - pl[id].res[r];
}
float World::creditRes(int id, int r, float amt)
{
    if (!isPlayer(id) || !pl[id].used || amt <= 0)
        return 0;
    float keep = std::clamp(allocRoom(id, r), 0.f, amt);
    pl[id].res[r] += keep;
    if (amt - keep > .01f)
        noteDiscard(id, r, amt - keep);
    return keep;
}
void World::noteDiscard(int id, int r, float lost)
{
    if (!isPlayer(id) || clock < bp[id].allocNote)
        return;
    bp[id].allocNote = clock + 60.0;
    say(id, std::string(RES_NAME[r]) + " storage share is full: " + std::to_string(int(std::ceil(lost))) + " " +
                RES_NAME[r] + " discarded (Town Hall > Storage).");
}
std::string World::ownerName(int o) const
{
    return isFactionOwner(o) ? FACTION_NAME[o - FACTION_BASE] : (isPlayer(o) ? pl[o].name : std::string("Wildlife"));
}

void World::say(int id, const std::string &s)
{
    if (id < 1 || id > MAX_PLAYERS || !pl[id].online)
        return;
    Writer w;
    w.put<uint8_t>(S_Text);
    w.str(s);
    net.send(pl[id].conn, w);
}
void World::fx(EffectType t, float x, float y, float x2, float y2)
{
    effects.push_back({t, x, y, x2, y2});
}

entt::entity World::find(uint32_t uid) const
{
    auto it = byUid.find(uid);
    return it == byUid.end() || !reg.valid(it->second) ? entt::entity(entt::null) : it->second;
}
Pos World::posOf(entt::entity e) const
{
    const auto &i = reg.get<Info>(e);
    if (isBuilding(i.kind))
    {
        const auto &b = reg.get<Building>(e);
        return footCenter(i.kind, b.tx, b.ty);
    }
    return reg.get<Pos>(e);
}
float World::rectDist(Pos p, entt::entity e) const
{
    const auto &i = reg.get<Info>(e);
    if (!isBuilding(i.kind))
    {
        Pos q = reg.get<Pos>(e);
        return std::hypot(p.x - q.x, p.y - q.y);
    }
    const auto &b = reg.get<Building>(e);
    float dx = std::max({float(b.tx) - p.x, 0.f, p.x - float(b.tx + KINFO[i.kind].fw)});
    float dy = std::max({float(b.ty) - p.y, 0.f, p.y - float(b.ty + KINFO[i.kind].fh)});
    return std::hypot(dx, dy);
}
bool World::complete(entt::entity e) const
{
    return !reg.all_of<Building>(e) || reg.get<Building>(e).progress >= 1.f;
}

// pooled resources: own first, then allies ("shared economy")
bool World::afford(int id, const float *cost) const
{
    for (int r = 0; r < R_Count; r++)
    {
        float have = pl[id].res[r];
        for (int a = 1; a <= MAX_PLAYERS; a++)
            if (a != id && pl[a].used && pl[id].allied[a])
                have += pl[a].res[r];
        if (have + 1e-3f < cost[r])
            return false;
    }
    return true;
}
void World::spend(int id, const float *cost)
{
    for (int r = 0; r < R_Count; r++)
    {
        float need = cost[r], take = std::min(need, pl[id].res[r]);
        pl[id].res[r] -= take;
        need -= take;
        for (int a = 1; a <= MAX_PLAYERS && need > 0; a++)
            if (a != id && pl[a].used && pl[id].allied[a])
            {
                float t = std::min(need, pl[a].res[r]);
                pl[a].res[r] -= t;
                need -= t;
            }
    }
}

// fog of war
bool World::bitAt(const std::unordered_map<int64_t, Bits> &m, int x, int y) const
{
    auto it = m.find(chunkKey(chunkOf(x), chunkOf(y)));
    if (it == m.end())
        return false;
    int i = localIdx(x, y);
    return it->second[size_t(i >> 3)] >> (i & 7) & 1;
}
bool World::explored(int id, int x, int y) const
{
    if (bitAt(pl[id].expl, x, y))
        return true;
    for (int a = 1; a <= MAX_PLAYERS; a++)
        if (a != id && pl[a].used && shares(a, id) && bitAt(pl[a].expl, x, y))
            return true;
    return false;
}
void World::reveal(int id, int cx, int cy, int r)
{
    if (!isPlayer(id))
        return;
    auto &m = pl[id].expl;
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++)
        {
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r + r)
                continue;
            Bits &b = m.try_emplace(chunkKey(chunkOf(x), chunkOf(y)), Bits{}).first->second;
            int i = localIdx(x, y);
            b[size_t(i >> 3)] |= uint8_t(1 << (i & 7));
        }
}

entt::entity World::makeBase(Kind k, uint8_t owner, uint32_t uid)
{
    auto e = reg.create();
    if (!uid)
        uid = nextUid++;
    nextUid = std::max(nextUid, uid + 1);
    byUid[uid] = e;
    reg.emplace<Uid>(e, Uid{uid});
    reg.emplace<Owner>(e, Owner{owner});
    reg.emplace<Info>(e, Info{k, 1});
    reg.emplace<Health>(e, Health{KINFO[k].hp, KINFO[k].hp, 0});
    return e;
}
entt::entity World::makeUnit(Kind k, uint8_t owner, float x, float y)
{
    auto e = makeBase(k, owner);
    reg.emplace<Pos>(e, Pos{x, y});
    reg.emplace<Unit>(e);
    {
        auto &h = reg.get<Health>(e);
        h.hp = h.maxhp = KINFO[k].hp * unitHpMul(ulv(owner, k));
    }
    if (k == K_Sheep)
        reg.emplace<Wander>(e, Wander{float(rng() % 300) / 100.f});
    return e;
}
entt::entity World::makeBuilding(Kind k, uint8_t owner, int tx, int ty, bool done)
{
    auto e = makeBase(k, owner);
    reg.emplace<Building>(e, Building{tx, ty, done ? 1.f : 0.f, {0, 0, 0, 0}, 0.f, 0.f});
    for (int y = ty; y < ty + KINFO[k].fh; y++)
        for (int x = tx; x < tx + KINFO[k].fw; x++)
            occ[tkey(x, y)] = reg.get<Uid>(e).id;
    markFootprint(k, tx, ty); // [world] a footprint never needs clearing again
    if (done)
        reveal(owner, int(std::floor(footCenter(k, tx, ty).x)), int(std::floor(footCenter(k, tx, ty).y)),
               KINFO[k].vision);
    return e;
}
void World::destroyNow(entt::entity e)
{
    if (!reg.valid(e))
        return;
    if (reg.all_of<Ship>(e))
        shipDestroyed(e);
    uint32_t uid = reg.get<Uid>(e).id;
    if (reg.all_of<Building>(e))
    {
        const auto &b = reg.get<Building>(e);
        Kind k = reg.get<Info>(e).kind;
        markFootprint(k, b.tx, b.ty); // [world] rubble does not regrow trees
        for (int y = b.ty; y < b.ty + KINFO[k].fh; y++)
            for (int x = b.tx; x < b.tx + KINFO[k].fw; x++)
                occ.erase(tkey(x, y));
    }
    byUid.erase(uid);
    dying.erase(uid);
    reg.destroy(e);
}

void World::resetProfile(int id)
{
    for (int r = 0; r < R_Count; r++)
        pl[id].res[r] = START_RES[r] * (std::getenv("BW_RICH") ? 20.f : 1.f); // BW_RICH: test hook
}

bool World::findPath(IV s, IV g, bool water, uint32_t ignore, std::vector<Pos> &out, int maxNodes)
{
    out.clear();
    if (!walkable(g.x, g.y, water, ignore))
    {
        bool f = false;
        for (int r = 1; r <= 8 && !f; r++)
            for (int dy = -r; dy <= r && !f; dy++)
                for (int dx = -r; dx <= r && !f; dx++)
                    if (std::max(std::abs(dx), std::abs(dy)) == r && walkable(g.x + dx, g.y + dy, water, ignore))
                    {
                        g = {g.x + dx, g.y + dy};
                        f = true;
                    }
        if (!f)
            return false;
    }
    if (s.x == g.x && s.y == g.y)
        return true;
    struct PN
    {
        float g;
        int64_t par;
        bool closed;
    };
    std::unordered_map<int64_t, PN> nodes;
    using QE = std::pair<float, int64_t>;
    std::priority_queue<QE, std::vector<QE>, std::greater<>> q;
    auto h = [&](int x, int y)
    {
        float dx = float(std::abs(x - g.x)), dy = float(std::abs(y - g.y));
        return dx + dy - 0.586f * std::min(dx, dy);
    };
    int64_t sk = tkey(s.x, s.y), gk = tkey(g.x, g.y), best = sk;
    float bestH = h(s.x, s.y);
    nodes[sk] = {0, -1, false};
    q.push({bestH, sk});
    int expanded = 0;
    bool found = false;
    while (!q.empty() && expanded < maxNodes)
    {
        int64_t k = q.top().second;
        q.pop();
        PN &cur = nodes[k];
        if (cur.closed)
            continue;
        cur.closed = true;
        expanded++;
        if (k == gk)
        {
            found = true;
            break;
        }
        int x = keyCx(k), y = keyCy(k);
        float cg = cur.g;
        float ch = h(x, y);
        if (ch < bestH)
        {
            bestH = ch;
            best = k;
        }
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++)
            {
                if (!dx && !dy)
                    continue;
                int nx = x + dx, ny = y + dy;
                if (!walkable(nx, ny, water, ignore))
                    continue;
                if (dx && dy && (!walkable(x + dx, y, water, ignore) || !walkable(x, y + dy, water, ignore)))
                    continue;
                int64_t nk = tkey(nx, ny);
                float ng = cg + (dx && dy ? 1.414f : 1.f);
                auto it = nodes.find(nk);
                if (it == nodes.end() || ng < it->second.g)
                {
                    nodes[nk] = {ng, k, false};
                    q.push({ng + h(nx, ny), nk});
                }
            }
    }
    int64_t end = found ? gk : best;
    if (end == sk)
        return false;
    for (int64_t k = end; k != sk && k != -1; k = nodes[k].par)
        out.push_back({float(keyCx(k)) + .5f, float(keyCy(k)) + .5f});
    std::reverse(out.begin(), out.end());
    return !out.empty();
}
bool World::order(entt::entity e, Pos goal, uint32_t ignore, int maxNodes)
{
    auto &u = reg.get<Unit>(e);
    Pos p = reg.get<Pos>(e);
    bool water = reg.get<Info>(e).kind == K_Ship;
    std::vector<Pos> path;
    if (!findPath({int(std::floor(p.x)), int(std::floor(p.y))}, {int(std::floor(goal.x)), int(std::floor(goal.y))},
                  water, ignore, path, maxNodes))
        return false;
    u.path = std::move(path);
    u.i = 0;
    return true;
}

Town *World::townById(int id)
{
    for (auto &t : towns)
        if (t.id == id)
            return &t;
    return nullptr;
}

void World::removeOwner(int id)
{
    std::vector<entt::entity> v;
    reg.view<Owner>().each([&](entt::entity e, const Owner &o)
    {
        if (o.id == id)
            v.push_back(e);
    });
    for (auto e : v)
    {
        if (!reg.valid(e))
            continue; // passengers are destroyed together with their ship
        if (isBuilding(reg.get<Info>(e).kind))
        {
            Pos p = posOf(e);
            fx(E_Explosion, p.x, p.y);
        }
        destroyNow(e);
    }
}

entt::entity World::nearestHostile(Pos p, int owner, float range, bool buildings)
{
    entt::entity best = entt::null;
    float bd = range;
    reg.view<Owner, Info>().each([&](entt::entity e, const Owner &o, const Info &i)
    {
        if (!hostile(owner, o.id) || (isBuilding(i.kind) && !buildings) || dying.count(reg.get<Uid>(e).id) ||
            reg.all_of<Aboard>(e))
            return;
        float d = rectDist(p, e);
        if (d < bd)
        {
            bd = d;
            best = e;
        }
    });
    return best;
}

int World::unitCount(int id)
{
    int n = 0;
    reg.view<Owner, Unit>().each([&](const Owner &o, const Unit &)
    {
        if (o.id == id)
            n++;
    });
    return n;
}
int World::workersOf(uint32_t home)
{
    int n = 0;
    reg.view<Worker>().each([&](const Worker &w)
    {
        if (w.home == home)
            n++;
    });
    return n;
}

bool World::spawnSpot(entt::entity b, bool water, Pos &out)
{
    const auto &bi = reg.get<Building>(b);
    Kind k = reg.get<Info>(b).kind;
    int fw = KINFO[k].fw, fh = KINFO[k].fh;
    for (int r = 1; r <= 4; r++)
        for (int y = bi.ty + fh - 1 + r; y >= bi.ty - r; y--)
            for (int x = bi.tx - r; x <= bi.tx + fw - 1 + r; x++)
            {
                if (std::max({bi.tx - x, x - (bi.tx + fw - 1), bi.ty - y, y - (bi.ty + fh - 1)}) != r)
                    continue;
                if (walkable(x, y, water, 0))
                {
                    out = {float(x) + .5f, float(y) + .5f};
                    return true;
                }
            }
    return false;
}

void World::notifyClan(int clan, const std::string &msg)
{
    for (int i = 1; i <= MAX_PLAYERS; i++)
        if (pl[i].used && pl[i].clan == clan)
            say(i, msg);
}

entt::entity World::townHallOf(int id)
{
    entt::entity th = entt::null;
    reg.view<Info, Building, Owner>().each([&](entt::entity b, const Info &i, const Building &bb, const Owner &o)
    {
        if (th == entt::null && i.kind == K_TownHall && o.id == id && bb.progress >= 1.f)
            th = b;
    });
    return th;
}

std::vector<Source> World::sourcesFor(int id)
{
    std::vector<Source> s;
    reg.view<Owner, Info>().each([&](entt::entity e, const Owner &o, const Info &i)
    {
        if (o.id == 0 || !allied(id, o.id) || !complete(e))
            return;
        Pos p = posOf(e);
        s.push_back(
            {p.x, p.y, float(KINFO[i.kind].vision) + (isBuilding(i.kind) ? float(KINFO[i.kind].fw) * .5f : 0.f)});
    });
    return s;
}

uint8_t World::featOf(int x, int y)
{
    int b = biome(x, y);
    if (!isLand(b) || isCliffTile(b, biome(x, y - 1)))
        return F_None;
    return featureAt(x, y, b, seed);
}
bool World::isCleared(int x, int y) const
{
    return cleared.count(tkey(x, y)) > 0;
}
void World::markCleared(int x, int y)
{
    if (cleared.insert(tkey(x, y)).second)
    {
        clearedNew.push_back(tkey(x, y));
        clearVer++;
    }
}
void World::markFootprint(Kind k, int tx, int ty)
{ // a building's footprint never shows (or demands clearing of) features again
    for (int y = ty; y < ty + KINFO[k].fh; y++)
        for (int x = tx; x < tx + KINFO[k].fw; x++)
            markCleared(x, y);
}

void World::shipDestroyed(entt::entity ship)
{ // passengers go down with the ship
    auto *sh = reg.try_get<Ship>(ship);
    if (!sh)
        return;
    std::vector<uint32_t> pax = sh->pax;
    int ow = reg.get<Owner>(ship).id;
    reg.remove<Ship>(ship);
    int n = 0;
    for (uint32_t uid : pax)
    {
        auto e = find(uid);
        if (e == entt::null)
            continue;
        n++;
        destroyNow(e);
    }
    if (n)
        say(ow, "Your ship sank with " + std::to_string(n) + " unit(s) aboard!");
}

bool World::outOfTerritory(entt::entity e) const
{
    auto *b = reg.try_get<BFlags>(e);
    return b && b->dis;
}
bool World::inactive(entt::entity e) const
{
    return outOfTerritory(e) || crippled(e);
} // non-functional building
bool World::crippled(entt::entity e) const
{
    const auto *b = reg.try_get<Building>(e);
    if (!b || b->progress < 1.f || !isPlayer(reg.get<Owner>(e).id))
        return false;
    const auto &h = reg.get<Health>(e);
    return critical(h.hp / std::max(1.f, h.maxhp));
}
float World::efficiency(entt::entity e) const
{
    if (!reg.all_of<Building>(e) || !isPlayer(reg.get<Owner>(e).id))
        return 1.f;
    const auto &h = reg.get<Health>(e);
    return hpEfficiency(h.hp / std::max(1.f, h.maxhp));
}
bool World::siteBusy(entt::entity e) const
{
    const auto *b = reg.try_get<Building>(e);
    return (b && b->progress < 1.f) || reg.all_of<SiteJob>(e);
}
int World::thLevel(int id)
{
    auto th = townHallOf(id);
    return th == entt::null ? 0 : reg.get<Info>(th).level;
}
int World::countKind(int id, Kind k)
{
    int n = 0;
    reg.view<Info, Owner, Building>().each([&](entt::entity e, const Info &i, const Owner &o, const Building &)
    {
        if (o.id == id && i.kind == k && !reg.all_of<Moving>(e))
            n++;
    });
    return n;
}
uint8_t World::bflagsOf(entt::entity e) const
{
    uint8_t f = 0;
    if (auto *b = reg.try_get<BFlags>(e))
    {
        if (b->burn > 0)
            f |= BLD_BURNING;
        if (b->dis)
            f |= BLD_DISABLED;
    }
    if (reg.all_of<Moving>(e))
        f |= BLD_MOVING;
    if (const SiteJob *j = reg.try_get<SiteJob>(e); j && j->kind != SJ_Build)
        f |= BLD_WORK;
    if (crippled(e))
        f |= BLD_CRIPPLED;
    return f;
}

void World::releaseUnit(entt::entity e)
{ // a player command takes the unit over
    reg.remove<Support>(e);
    reg.remove<Extinguish>(e);
}
bool World::townMoveActive(int id)
{
    bool a = false;
    reg.view<RelocNew, Owner>().each([&](const RelocNew &rn, const Owner &o)
    {
        if (rn.town && o.id == id)
            a = true;
    });
    return a;
}

void World::sendAlert(int to, uint8_t type, Pos at, const std::string &text)
{
    if (!isPlayer(to) || !pl[to].online)
        return;
    Writer w;
    w.put<uint8_t>(S_Alert).put<uint8_t>(type).put<float>(at.x).put<float>(at.y);
    w.str(text);
    net.send(pl[to].conn, w);
}

std::vector<int> World::clanHalls(int victim)
{ // clan mates that still have a Town Hall
    std::vector<int> v;
    int c = pl[victim].clan;
    if (!c)
        return v;
    for (int i = 1; i <= MAX_PLAYERS; i++)
        if (i != victim && pl[i].used && pl[i].clan == c && townHallOf(i) != entt::null)
            v.push_back(i);
    return v;
}

bool World::inClanRadiusOf(int id, Pos c)
{
    for (int m : clanHalls(id))
    {
        Pos h = posOf(townHallOf(m));
        if (std::max(std::abs(h.x - c.x), std::abs(h.y - c.y)) <= CLAN_TOWN_RADIUS)
            return true;
    }
    return false;
}
