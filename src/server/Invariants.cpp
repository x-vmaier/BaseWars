/**
 * @file Invariants.cpp
 * @brief World::checkInvariants(): cross-entity consistency checks run by every selftest (BW_INVARIANTS=1 live).
 */
#include "server/World.hpp"

#include <cmath>
#include <string>

namespace
{
struct Checker
{
    World &w;
    std::vector<std::string> out;

    void fail(const std::string &what, uint32_t uid)
    {
        if (out.size() < 40)
            out.push_back(what + " (uid " + std::to_string(uid) + ")");
    }
    // A stored uid of 0 means "none"; anything else must resolve to a live entity.
    void ref(uint32_t target, const char *what, uint32_t owner)
    {
        if (target && w.find(target) == entt::null)
            fail(std::string(what) + " references dead uid " + std::to_string(target), owner);
    }
};
} // namespace

std::vector<std::string> World::checkInvariants()
{
    Checker c{*this, {}};

    reg.view<Uid>().each([&](entt::entity e, const Uid &u)
    {
        auto it = byUid.find(u.id);
        if (it == byUid.end() || it->second != e)
            c.fail("entity missing from byUid", u.id);
        if (const Health *h = reg.try_get<Health>(e); h && !(h->hp == h->hp && h->hp <= h->maxhp + .01f))
            c.fail("hp invalid", u.id);
        if (const Owner *o = reg.try_get<Owner>(e); o && o->id >= FACTION_BASE + FACTION_COUNT && o->id != 255)
            c.fail("owner " + std::to_string(o->id) + " out of range", u.id);
    });
    for (auto &[uid, e] : byUid)
        if (!reg.valid(e) || !reg.all_of<Uid>(e) || reg.get<Uid>(e).id != uid)
            c.fail("byUid entry stale", uid);

    // Footprints: every building owns exactly its tiles in `occ`, and no tile has two owners.
    size_t tiles = 0;
    int halls[MAX_PLAYERS + 1]{};
    reg.view<Uid, Info, Building, Owner>().each([&](const Uid &u, const Info &i, const Building &b, const Owner &o)
    {
        if (i.kind == K_TownHall && isPlayer(o.id))
            halls[o.id]++;
        for (int y = b.ty; y < b.ty + KINFO[i.kind].fh; y++)
            for (int x = b.tx; x < b.tx + KINFO[i.kind].fw; x++)
            {
                tiles++;
                auto it = occ.find(tkey(x, y));
                if (it == occ.end() || it->second != u.id)
                    c.fail("footprint tile not owned in occ", u.id);
            }
    });
    if (tiles != occ.size())
        c.fail("occ has " + std::to_string(occ.size()) + " tiles but buildings cover " + std::to_string(tiles), 0);
    for (int id = 1; id <= MAX_PLAYERS; id++)
        if (halls[id] > 1)
            c.fail("player " + std::to_string(id) + " has " + std::to_string(halls[id]) + " Town Halls", 0);

    for (int id = 1; id <= MAX_PLAYERS; id++)
        for (int r = 0; r < R_Count; r++)
            if (!(pl[id].res[r] >= 0.f) || !std::isfinite(pl[id].res[r]))
                c.fail("player " + std::to_string(id) + " resource " + std::to_string(r) + " invalid", 0);

    // References between entities.
    reg.view<Uid, BuildTask>().each([&](const Uid &u, const BuildTask &t) { c.ref(t.site, "BuildTask.site", u.id); });
    reg.view<Uid, SiteJob>().each([&](const Uid &u, const SiteJob &j) { c.ref(j.pawn, "SiteJob.pawn", u.id); });
    reg.view<Uid, Haul>().each([&](const Uid &u, const Haul &h) { c.ref(h.storage, "Haul.storage", u.id); });
    reg.view<Uid, Worker>().each([&](const Uid &u, const Worker &wk) { c.ref(wk.home, "Worker.home", u.id); });
    reg.view<Uid, Hunt>().each([&](const Uid &u, const Hunt &h) { c.ref(h.storage, "Hunt.storage", u.id); });
    reg.view<Uid, Moving>().each([&](const Uid &u, const Moving &m) { c.ref(m.to, "Moving.to", u.id); });
    reg.view<Uid, RelocNew>().each([&](const Uid &u, const RelocNew &r) { c.ref(r.from, "RelocNew.from", u.id); });
    reg.view<Uid, Extinguish>().each([&](const Uid &u, const Extinguish &x)
    { c.ref(x.target, "Extinguish.target", u.id); });
    reg.view<Uid, Aboard>().each([&](const Uid &u, const Aboard &a) { c.ref(a.ship, "Aboard.ship", u.id); });
    reg.view<Uid, Boarding>().each([&](const Uid &u, const Boarding &b) { c.ref(b.ship, "Boarding.ship", u.id); });
    reg.view<Uid, Ship>().each([&](const Uid &u, const Ship &s)
    {
        for (uint32_t p : s.pax)
            c.ref(p, "Ship.pax", u.id);
    });
    reg.view<Uid, Voyage>().each([&](const Uid &u, const Voyage &v)
    {
        for (uint32_t p : v.units)
            c.ref(p, "Voyage.units", u.id);
        if (!std::isfinite(v.start))
            c.fail("Voyage without start time", u.id);
    });
    reg.view<Uid, Raider>().each([&](const Uid &u, const Raider &r)
    {
        if (!(r.until > 0) || !std::isfinite(r.until))
            c.fail("Raider without expiry", u.id);
    });

    // Ruins are neutral K_Ruin buildings with an expiry; a defeated player has no Town Hall and a sane state.
    reg.view<Uid, Info, Ruin>().each([&](const Uid &u, const Info &i, const Ruin &ru)
    {
        if (i.kind != K_Ruin || !(ru.expiresAt > 0) || !std::isfinite(ru.expiresAt))
            c.fail("Ruin without expiry or on a wrong kind", u.id);
    });
    reg.view<Uid, Info>().each([&](const Uid &u, const Info &i)
    {
        if (i.kind == K_Ruin && !reg.all_of<Ruin>(byUid[u.id]))
            c.fail("K_Ruin without Ruin component", u.id);
    });
    for (int id = 1; id <= MAX_PLAYERS; id++)
        if (bp[id].defeat.phase > DF_Offered || (bp[id].defeat.phase != DF_None && halls[id] > 0))
            c.fail("player " + std::to_string(id) + " has a defeat state but a Town Hall or an invalid phase", 0);
    return std::move(c.out);
}
