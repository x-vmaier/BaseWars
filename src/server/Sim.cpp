/**
 * @file Sim.cpp
 * @brief Per-tick server update: system order, slow systems and replication cadence.
 */
#include "server/Sim.hpp"

#include <cstdio>
#include <cstdlib>

void Sim::baseSecond()
{
    for (int id = 1; id <= MAX_PLAYERS; id++)
        if (world.pl[id].used)
            sys.spawn.baseTestInit(id);
    sys.territory.evalFunctional();
    sys.territory.relocSecond();
    sys.fire.burnSecond();
    sys.defense.supportSecond();
    sys.clan.homelessSecond();
    sys.faction.demandSecond();
    for (int id = 1; id <= MAX_PLAYERS; id++)
        if (world.pl[id].online)
            sys.repl.sendBaseInfo(id);
}

void Sim::pollNet()
{
    NetEvent ev;
    while (world.net.poll(ev))
    {
        switch (ev.type)
        {
        case NetEvent::Connect:
            break;
        case NetEvent::Receive:
            sys.packets.onPacket(ev.conn, ev.data);
            break;
        case NetEvent::Disconnect:
            sys.session.onDisconnect(ev.conn);
            break;
        }
    }
}

// Keeps the Steam server-browser entry fresh.
void Sim::publishPresence()
{
    if (world.clock - world.lastPublish <= 5)
        return;

    int online = 0;
    for (int i = 1; i <= MAX_PLAYERS; i++)
        if (world.pl[i].online)
            online++;
    world.net.publish(online, MAX_PLAYERS, "Base Wars");
    world.lastPublish = world.clock;
}

// Systems that only need to run a few times per second.
void Sim::slowSystems(const std::vector<entt::entity> &units, const std::vector<entt::entity> &blds)
{
    if (world.tick % 20 == 0)
    {
        sys.env.coldAndWake(units, blds);
        baseSecond();
        sys.town.townTick();
        sys.faction.callHelp();
        sys.convoy.updateSpotting();
        sys.convoy.spawnHauls();
        sys.raid.raidTick();
    }
    if (world.tick % 4 == 0)
        sys.env.explore(units, blds);
}

// Staggered ticks spread the per-player packets over the second.
void Sim::replicate()
{
    for (int id = 1; id <= MAX_PLAYERS; id++)
    {
        if (!world.pl[id].online)
            continue;
        if (world.tick % 10 == 0)
            sys.repl.sendQueues(id);
        if (world.tick % 2 == 0)
            sys.repl.sendState(id);
        if (world.tick % 8 == 0)
            sys.repl.sendPaths(id);
        if (world.tick % 10 == 3 && world.pl[id].online)
            sys.repl.sendShipInfo(id);
        if (world.tick % 10 == 0)
            sys.repl.sendFog(id);
    }
    if (world.tick % 2 == 0)
        world.effects.clear();

    if (world.tick % 20 == 0)
    {
        sys.repl.sendRoster();
        for (int id = 1; id <= MAX_PLAYERS; id++)
            if (world.pl[id].online)
                sys.repl.sendMap(id);
    }
}

void Sim::update(float dt)
{
    auto &reg = world.reg;
    world.tick++;
    world.clock += dt;
    std::vector<entt::entity> units, blds;
    reg.view<Info>().each([&](entt::entity e, const Info &i) { (isBuilding(i.kind) ? blds : units).push_back(e); });

    sys.build.tick(blds, dt);
    sys.units.behave(units, dt);
    sys.combat.processKills();
    sys.units.move(units, dt);
    sys.naval.shipsTick(dt);
    slowSystems(units, blds);

    sys.clearing.clearTick(dt);
    sys.construction.tick(dt);
    if (world.tick % 20 == 7)
    {
        sys.hunt.sheepSecond();
        sys.patrol.patrolSecond();
    }
    sys.defense.flushAlerts();
    replicate();
    static const bool checkLive = std::getenv("BW_INVARIANTS") != nullptr; // BW_INVARIANTS=1: log violations live
    if (checkLive && world.tick % TICK_HZ == 0)
        for (const std::string &v : world.checkInvariants())
            std::fprintf(stderr, "[invariant] %s\n", v.c_str());

    if (world.clock - world.lastSave > 30)
    {
        sys.save.saveGame();
        world.lastSave = world.clock;
    }
}

void Sim::run(int ticks)
{
    for (int i = 0; i < ticks; i++)
    {
        pollNet();
        update(1.f / TICK_HZ);
    }
}
