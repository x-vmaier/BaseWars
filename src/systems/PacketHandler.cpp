/**
 * @file PacketHandler.cpp
 * @brief Decodes client commands (C_*) and dispatches them to the systems.
 */
#include "systems/PacketHandler.hpp"

#include "common/Progression.hpp"
#include "systems/Systems.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void PacketHandler::onPacket(uint64_t conn, const std::vector<uint8_t> &data)
{
    Reader r{data.data(), data.data() + data.size()};
    uint8_t type = r.get<uint8_t>();
    auto cp = world.connPlayer.find(conn);
    int id = cp == world.connPlayer.end() ? 0 : cp->second;

    if (type == C_Hello)
    {
        if (id < 1)
            s.session.onHello(conn, r);
        return;
    }

    if (id < 1 || !r.ok)
        return;

    Profile &p = world.pl[id];
    switch (type)
    {
    case C_Cam:
        onCam(p, r);
        break;
    case C_Build:
        onBuild(id, p, r);
        break;
    case C_Train:
        onTrain(id, r);
        break;
    case C_ClanCreate:
        onClanCreate(id, p, r);
        break;
    case C_ClanJoin:
        onClanJoin(id, p, r);
        break;
    case C_ClanLeader:
        onClanLeader(id, p, r);
        break;
    case C_SetHaul:
        onSetHaul(id, r);
        break;
    case C_UpgradeUnit:
        onUpgradeUnit(id, p, r);
        break;
    case C_Chat:
        onChat(id, p, r);
        break;
    case C_Transfer:
        onTransfer(id, p, r);
        break;
    case C_Donate:
        onDonate(id, r);
        break;
    case C_Upgrade:
        onUpgrade(id, r);
        break;
    case C_Move:
        onMove(id, r);
        break;
    case C_AttackMove:
        onAttackMove(id, r);
        break;
    case C_Attack:
        onAttack(id, p, r);
        break;
    case C_Ally:
        onAlly(id, p, r);
        break;
    case C_Clear:
        s.clearing.onClear(id, r);
        break;
    default:
        // Each command set ignores the other's message types.
        s.naval.shipPacket(id, type, r);
        baseCommand(id, type, r);
        break;
    }
}

void PacketHandler::baseCommand(int id, uint8_t type, Reader &r)
{
    switch (type)
    {
    case C_SetQuota:
        onSetQuota(id, r);
        break;
    case C_SetQuotas:
        onSetQuotas(id, r);
        break;
    case C_SetAlloc:
        onSetAlloc(id, r);
        break;
    case C_Dispose:
        onDispose(id, r);
        break;
    case C_Support:
        onSupport(id, r);
        break;
    case C_MoveTown:
        onMoveTown(id, r);
        break;
    case C_Relocate:
        onRelocate(id, r);
        break;
    case C_Dequeue:
        onDequeue(id, r);
        break;
    default:
        break;
    }
}

std::vector<entt::entity> PacketHandler::readUnits(int id, Reader &r, bool combatOnly)
{
    std::vector<entt::entity> us;
    int n = r.get<uint8_t>();
    for (int i = 0; i < n; i++)
    {
        auto e = world.find(r.get<uint32_t>());

        // The player takes a clearing pawn or a pawn on a building site over.
        if (e != entt::null && reg.all_of<ClearTask>(e) && reg.get<Owner>(e).id == id)
            s.clearing.releasePawn(e, true);
        if (e != entt::null && reg.all_of<BuildTask>(e) && reg.get<Owner>(e).id == id)
            s.construction.releasePawn(e, true);

        // Units aboard a ship cannot be commanded.
        if (e == entt::null || !reg.all_of<Unit, Owner, Info>(e) || reg.get<Owner>(e).id != id ||
            reg.all_of<Worker>(e) || reg.all_of<Convoy>(e) || reg.all_of<Haul>(e) || reg.all_of<Raider>(e) ||
            reg.all_of<Aboard>(e))
            continue;
        Kind k = reg.get<Info>(e).kind;
        if (combatOnly && hasTag(k, KT_NonCombat))
            continue;

        // Any manual order ends patrols, firefighting, hunting, embarking and supply runs.
        world.releaseUnit(e);
        if (hasTag(k, KT_Worker))
            s.hunt.huntPause(e);
        reg.remove<Boarding>(e);
        if (k == K_Ship)
            s.naval.cancelVoyage(e);
        us.push_back(e);
    }
    return us;
}

void PacketHandler::onCam(Profile &p, Reader &r)
{
    float x = r.get<float>(), y = r.get<float>();
    if (r.ok && std::isfinite(x) && std::isfinite(y))
        p.cam = {x, y};
}

void PacketHandler::onBuild(int id, Profile &p, Reader &r)
{
    auto k = Kind(r.get<uint8_t>());
    int tx = r.get<int32_t>(), ty = r.get<int32_t>();
    if (!r.ok || k >= K_Count || !isBuilding(k))
        return;
    if (k == K_TownHall)
    {
        s.clan.homelessBuild(id, tx, ty);
        return;
    }

    if (!hasTag(k, KT_Freestanding) && world.townMoveActive(id))
    {
        world.say(id, "Wait until the town move is finished before building.");
        return;
    }

    PlaceWorld w{world, id};
    int err = checkPlace(w, k, tx, ty);
    int th = world.thLevel(id);
    if (int g = buildGate(k, th, world.countKind(id, k) + s.clearing.pendingOf(id, k)))
    {
        world.say(id, "Cannot build " + aName(k) + ": " + gateText(g, k, th) + ".");
        return;
    }

    if (err)
    {
        world.say(id, std::string("Cannot build here: ") + PLACE_TEXT[err] + ".");
        return;
    }

    if (auto blocked = s.clearing.blockedTiles(k, tx, ty); !blocked.empty())
    {
        s.clearing.queueBuild(id, k, tx, ty, blocked);
        return;
    }

    float cost[R_Count];
    buildCost(k, cost);
    if (!world.afford(id, cost))
    {
        world.say(id, cost[R_Meat] > p.res[R_Meat] ? "Not enough food: hunt sheep or build a Harbour."
                                                   : "Not enough resources.");
        return;
    }

    world.spend(id, cost);
    auto e = world.makeBuilding(k, uint8_t(id), tx, ty, false);
    Pos c = world.posOf(e);
    world.fx(E_Dust, c.x, c.y);
}

void PacketHandler::onTrain(int id, Reader &r)
{
    auto be = world.find(r.get<uint32_t>());
    auto k = Kind(r.get<uint8_t>());
    if (!r.ok || be == entt::null || k >= K_Count || isBuilding(k) || !reg.all_of<Building>(be))
        return;
    s.training.enqueueTrain(id, be, k);
}

void PacketHandler::onClanCreate(int id, Profile &p, Reader &r)
{
    std::string nm = r.str();
    for (char &ch : nm)
        if (ch < 32 || ch == 127)
            ch = '?';
    if (!r.ok || nm.size() < 3 || nm.size() > 16)
    {
        world.say(id, "Clan names need 3 to 16 characters.");
        return;
    }

    if (p.clan)
    {
        world.say(id, "Leave your current clan first.");
        return;
    }

    int slot = 0;
    for (int c = 1; c <= MAX_CLANS; c++)
    {
        if (world.clans[c].used && world.clans[c].name == nm)
        {
            world.say(id, "That clan name is taken.");
            return;
        }
        if (!world.clans[c].used && !slot)
            slot = c;
    }

    if (!slot)
    {
        world.say(id, "The server has no free clan slots.");
        return;
    }

    world.clans[slot] = Clan{true, nm, uint8_t(id)};
    p.clan = uint8_t(slot);
    world.say(id, "Clan '" + nm + "' founded. Friends can join it from the Clans tab.");
    s.repl.sendRoster();
}

void PacketHandler::onClanJoin(int id, Profile &p, Reader &r)
{
    int c = r.get<uint8_t>();
    if (!r.ok || c < 1 || c > MAX_CLANS || !world.clans[c].used)
        return;
    if (p.clan)
    {
        world.say(id, "Leave your current clan first.");
        return;
    }

    p.clan = uint8_t(c);
    world.notifyClan(c, p.name + " joined the clan.");
    s.repl.sendRoster();
}

void PacketHandler::onClanLeader(int id, Profile &p, Reader &r)
{
    int to = r.get<uint8_t>();
    int c = p.clan;
    if (!r.ok || !c || world.clans[c].leader != id || !isPlayer(to) || to == id || world.pl[to].clan != c)
    {
        world.say(id, "Only the clan leader can hand over leadership to a clan member.");
        return;
    }

    world.clans[c].leader = uint8_t(to);
    world.notifyClan(c, world.pl[to].name + " is now the clan leader. Their Town Hall is the clan hall.");
    s.repl.sendRoster();
}

void PacketHandler::onSetHaul(int id, Reader &r)
{
    auto be = world.find(r.get<uint32_t>());
    int pct = r.get<uint8_t>();
    if (!r.ok || be == entt::null || !reg.all_of<Building>(be) || reg.get<Owner>(be).id != id)
        return;
    Kind k = reg.get<Info>(be).kind;
    if (!hasTag(k, KT_Depot))
        return;
    reg.get<Building>(be).haulPct = uint8_t(std::clamp(pct, 10, 100));
}

void PacketHandler::onUpgradeUnit(int id, Profile &p, Reader &r)
{
    auto k = Kind(r.get<uint8_t>());
    if (!r.ok || !isTroop(k))
        return;
    int cur = std::max<int>(1, p.ulevel[k]), to = cur + 1;
    if (to > MAX_UNIT_LEVEL)
    {
        world.say(id, "Already at the maximum troop level.");
        return;
    }

    int archery = 0;
    reg.view<Info, Building, Owner>().each([&](const Info &i, const Building &b, const Owner &o)
    {
        if (i.kind == K_Archery && o.id == id && b.progress >= 1.f)
            archery = std::max<int>(archery, i.level);
    });
    if (archery < troopUpgradeNeedsArchery(to))
    {
        world.say(id, "Upgrade your Archery to level " + std::to_string(troopUpgradeNeedsArchery(to)) + " first.");
        return;
    }

    float cost[R_Count];
    for (int i = 0; i < R_Count; i++)
        cost[i] = std::ceil(KINFO[k].cost[i] * 2.5f * float(cur) + (i == R_Gold ? 30.f * float(cur) : 0.f));
    if (!world.afford(id, cost))
    {
        world.say(id, "Not enough resources.");
        return;
    }

    world.spend(id, cost);
    float oldMul = unitHpMul(cur);
    p.ulevel[k] = uint8_t(to);
    float ratio = unitHpMul(to) / oldMul;
    reg.view<Info, Owner, Health>().each([&](const Info &i, const Owner &o, Health &h)
    {
        if (i.kind == k && o.id == id)
        {
            h.maxhp *= ratio;
            h.hp *= ratio;
        }
    });
    world.say(id, std::string(KINFO[k].name) + "s upgraded to level " + std::to_string(to) + ".");
}

void PacketHandler::onChat(int id, Profile &p, Reader &r)
{
    int ch = r.get<uint8_t>();
    std::string text = r.str();
    for (char &c : text)
        if (c < 32 || c == 127)
            c = ' ';
    if (!r.ok || ch > 2 || text.empty())
        return;
    if (text.size() > 160)
        text.resize(160);
    if (ch == 1 && !p.clan)
    {
        world.say(id, "You are not in a clan.");
        return;
    }

    Writer w;
    w.put<uint8_t>(S_Chat).put<uint8_t>(uint8_t(ch)).put<uint8_t>(uint8_t(id));
    w.str(p.name);
    w.str(text);
    for (int i = 1; i <= MAX_PLAYERS; i++)
    {
        if (!world.pl[i].online)
            continue;
        bool to = ch == 0 || i == id || (ch == 1 && world.pl[i].clan == p.clan) || (ch == 2 && world.pl[id].allied[i]);
        if (to)
            world.net.send(world.pl[i].conn, w);
    }
}

void PacketHandler::onTransfer(int id, Profile &p, Reader &r)
{
    int to = r.get<uint8_t>();
    float amt[R_Count];
    float tot = 0;
    for (float &a : amt)
    {
        a = r.get<float>();
        if (!std::isfinite(a) || a < 0 || a > 500)
            return;
        tot += a;
    }

    if (!r.ok || tot <= 0 || !isPlayer(to) || to == id || !world.pl[to].used || !world.sameClan(id, to))
    {
        world.say(id, "You can only send resources to members of your clan.");
        return;
    }

    for (int i = 0; i < R_Count; i++)
        if (p.res[i] + 1e-3f < amt[i])
        {
            world.say(id, "Not enough resources.");
            return;
        }
    if (!s.faction.donationReady(id))
        return;
    entt::entity src = entt::null;
    reg.view<Info, Building, Owner>().each([&](entt::entity b, const Info &i, const Building &bb, const Owner &o)
    {
        if (src == entt::null && i.kind == K_TownHall && o.id == id && bb.progress >= 1.f)
            src = b;
    });
    Pos at;
    if (src == entt::null || !world.spawnSpot(src, false, at))
    {
        world.say(id, "No room at your Town Hall to send a convoy.");
        return;
    }

    for (int i = 0; i < R_Count; i++)
        p.res[i] -= amt[i];
    auto u = world.makeUnit(K_Pawn, uint8_t(id), at.x, at.y);
    Convoy cv{uint8_t(id), uint8_t(to), {amt[0], amt[1], amt[2], amt[3]}};
    reg.emplace<Convoy>(u, cv);
    world.fx(E_Dust, at.x, at.y);
    world.say(id, "A convoy with " + std::to_string(int(tot)) + " resources set out for " + world.pl[to].name +
                      ". Other players can see and raid it!");
    world.say(to, "A convoy from " + p.name + " is on its way.");
    s.faction.donated(id);
}

void PacketHandler::onDonate(int id, Reader &r)
{
    int f = r.get<uint8_t>(), res = r.get<uint8_t>();
    float amount = r.get<float>();
    if (!r.ok || f < 0 || f >= FACTION_COUNT || res >= R_Count || !std::isfinite(amount) || amount <= 0 || amount > 500)
        return;
    s.faction.donate(id, f, res, amount);
}

void PacketHandler::onUpgrade(int id, Reader &r)
{
    auto be = world.find(r.get<uint32_t>());
    if (r.ok)
        s.construction.upgrade(id, be);
}

void PacketHandler::onMove(int id, Reader &r)
{
    auto us = readUnits(id, r, false);
    float tx = r.get<float>(), ty = r.get<float>();
    if (!r.ok || !std::isfinite(tx) || !std::isfinite(ty))
        return;
    int fail = 0, n = 0;
    for (auto e : us)
    {
        reg.remove<Attack>(e);
        reg.remove<AMove>(e);
        float a = float(n++) * 2.4f, rad = n > 1 ? .35f * std::sqrt(float(n)) : 0.f;
        if (!world.order(e, {tx + std::cos(a) * rad, ty + std::sin(a) * rad}))
        {
            fail++;
            reg.get<Unit>(e).path.clear();
        }
    }

    if (fail)
        world.say(id, "Some units cannot reach that spot.");
}

void PacketHandler::onAttackMove(int id, Reader &r)
{
    auto us = readUnits(id, r, true);
    float tx = r.get<float>(), ty = r.get<float>();
    if (!r.ok || !std::isfinite(tx) || !std::isfinite(ty))
        return;
    for (auto e : us)
    {
        reg.remove<Attack>(e);
        if (world.order(e, {tx, ty}))
            reg.emplace_or_replace<AMove>(e, AMove{{tx, ty}});
    }
}

void PacketHandler::onAttack(int id, Profile &p, Reader &r)
{
    auto us = readUnits(id, r, true);
    auto tg = world.find(r.get<uint32_t>());
    if (!r.ok || us.empty() || tg == entt::null)
        return;
    int to = reg.get<Owner>(tg).id;
    if (to == id)
        return;
    if (isPlayer(to) && world.sameClan(id, to))
    {
        world.say(id, "You cannot attack your own clan. Leave the clan first.");
        return;
    }

    if (isPlayer(to) && world.allied(id, to))
    {
        s.clan.breakAlliance(id, to);
    }

    Pos tp = world.posOf(tg);
    uint32_t tuid = reg.get<Uid>(tg).id;
    bool bld = isBuilding(reg.get<Info>(tg).kind);
    int fail = 0;
    for (auto e : us)
    {
        reg.remove<AMove>(e);
        if (!world.order(e, tp, bld ? tuid : 0))
        {
            fail++;
            continue;
        }
        reg.emplace_or_replace<Attack>(e, Attack{tuid});
    }

    if (fail)
        world.say(id, "Some units cannot reach the target.");
    if (isPlayer(to))
        world.say(to, p.name + " is attacking you!");
}

void PacketHandler::onAlly(int id, Profile &p, Reader &r)
{
    int to = r.get<uint8_t>(), act = r.get<uint8_t>();
    if (!r.ok || to == id || to < 1 || to > MAX_PLAYERS || !world.pl[to].used)
        return;
    if (act == AA_Break)
    {
        if (p.allied[to])
            s.clan.breakAlliance(id, to);
        return;
    }

    if (p.allied[to])
    {
        world.say(id, "Already allied.");
        return;
    }

    if (world.pl[to].pending[id])
    {
        p.allied[to] = world.pl[to].allied[id] = true;
        p.pending[to] = world.pl[to].pending[id] = false;
        world.say(id, "You are now allied with " + world.pl[to].name + ": shared vision and economy.");
        world.say(to, p.name + " accepted your alliance.");
    }
    else
    {
        p.pending[to] = true;
        world.say(id, "Alliance invite sent to " + world.pl[to].name + ".");
        world.say(to, p.name + " invites you to an alliance. Right-click one of their buildings to accept.");
    }

    s.repl.sendRoster();
}

void PacketHandler::onSetQuota(int id, Reader &r)
{
    // Legacy single number: all warriors.
    int q = r.get<uint8_t>();
    if (r.ok)
    {
        std::fill(std::begin(world.bp[id].quota), std::end(world.bp[id].quota), uint8_t(0));
        world.bp[id].quota[0] = uint8_t(std::min(q, MAX_QUOTA));
    }
}

void PacketHandler::onSetQuotas(int id, Reader &r)
{
    std::array<int, QUOTA_TYPES> q{};
    for (int t = 0; t < QUOTA_TYPES; t++)
        q[size_t(t)] = r.get<uint8_t>();
    if (r.ok)
        for (int t = 0; t < QUOTA_TYPES; t++)
            world.bp[id].quota[t] = uint8_t(std::min(q[size_t(t)], MAX_QUOTA));
}

void PacketHandler::onSetAlloc(int id, Reader &r)
{
    int res = r.get<uint8_t>(), pct = r.get<uint8_t>();
    if (r.ok && res < R_Count)
        world.bp[id].alloc[res] = uint8_t(std::clamp(pct / ALLOC_STEP * ALLOC_STEP, 0, 100));
}

void PacketHandler::onDispose(int id, Reader &r)
{
    int res = r.get<uint8_t>();
    if (!r.ok || res >= R_Count)
        return;
    int n = int(world.pl[id].res[res]);
    world.pl[id].res[res] = 0;
    world.say(id, "Disposed of " + std::to_string(n) + " " + RES_NAME[res] + ".");
}

void PacketHandler::onSupport(int id, Reader &r)
{
    float x = r.get<float>(), y = r.get<float>();
    int n = r.get<uint8_t>();
    if (r.ok)
        s.defense.doSupport(id, {x, y}, n);
}

void PacketHandler::onMoveTown(int id, Reader &r)
{
    int tx = r.get<int32_t>(), ty = r.get<int32_t>();
    if (r.ok)
        s.territory.doMoveTown(id, tx, ty);
}

void PacketHandler::onRelocate(int id, Reader &r)
{
    uint32_t uid = r.get<uint32_t>();
    int tx = r.get<int32_t>(), ty = r.get<int32_t>();
    if (r.ok)
        s.territory.doRelocate(id, uid, tx, ty);
}

void PacketHandler::onDequeue(int id, Reader &r)
{
    auto be = world.find(r.get<uint32_t>());
    int idx = r.get<uint8_t>();
    if (r.ok && be != entt::null && reg.all_of<Building>(be))
        s.training.dequeueTrain(id, be, idx);
}
