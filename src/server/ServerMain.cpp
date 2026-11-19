// Authoritative server: infinite biome world, persistence, economy with physical supply lines, combat, naval units, alliances.
// main(), signal handling and the fixed-rate tick loop that owns the World and all systems.
#include "server/World.hpp"
#include "systems/Systems.hpp"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <thread>

namespace
{

    volatile std::sig_atomic_t g_quit = 0;

    class Server
    {
    public:
        Server(uint16_t port, uint32_t seed, std::string path, const std::string &serverName, bool localOnly) : world(seed, std::move(path)), sys(world)
        {
            bool existed = std::filesystem::exists(world.savePath);
            bool loaded = sys.save.loadGame();
            if (!loaded && existed)
            {
                std::error_code ec;
                std::filesystem::rename(world.savePath, world.savePath + ".bad", ec); // never lose the old file
                if (world.loadStarted)
                {
                    std::printf("Savegame could not be read; kept as %s.bad. Restart the server to begin a fresh world.\n", world.savePath.c_str());
                    std::fflush(stdout);
                    std::_Exit(2); // no destructor: never overwrite the save with a half-loaded state
                }
                std::printf("Old savegame kept as %s.bad; starting a fresh world.\n", world.savePath.c_str());
            }
            if (!world.net.start(port, serverName, localOnly))
            {
                std::puts("Failed to bind port");
                std::exit(1);
            }
            std::printf("Server listening on port %d (Steam Networking Sockets) (seed %u, %s, steam %s)\n", port, world.seed, loaded ? "savegame loaded" : "fresh world", world.net.relayActive() ? "on" : "off");
        }
        ~Server() { sys.save.saveGame(); }
        Server(const Server &) = delete;
        Server &operator=(const Server &) = delete;

        void run()
        {
            using clk = std::chrono::steady_clock;
            auto next = clk::now();
            const auto step = std::chrono::milliseconds(1000 / TICK_HZ);
            while (!g_quit)
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
                if (world.clock - world.lastPublish > 5)
                { // keep the Steam server-browser entry fresh
                    int online = 0;
                    for (int i = 1; i <= MAX_PLAYERS; i++)
                        if (world.pl[i].online)
                            online++;
                    world.net.publish(online, MAX_PLAYERS, "Base Wars");
                    world.lastPublish = world.clock;
                }
                update(1.f / TICK_HZ);
                world.net.flush();
                next += step;
                auto now = clk::now();
                if (next < now - std::chrono::seconds(1))
                    next = now;
                std::this_thread::sleep_until(next);
            }
            std::puts("Shutting down, saving world...");
        }

    private:
        World world;
        Systems sys;

        // Base mechanics, once per second: test hook, territory, relocation, fire, support patrols, homeless players, base info.
        void baseSecond()
        {
            for (int id = 1; id <= MAX_PLAYERS; id++)
                if (world.pl[id].used)
                    sys.spawn.baseTestInit(id);
            sys.territory.evalFunctional();
            sys.territory.relocSecond();
            sys.fire.burnSecond();
            sys.defense.supportSecond();
            sys.clan.homelessSecond();
            sys.faction.demandSecond(); // NPC town demand, gift memory, S_Econ
            for (int id = 1; id <= MAX_PLAYERS; id++)
                if (world.pl[id].online)
                    sys.repl.sendBaseInfo(id);
        }

        void update(float dt)
        {
            auto &reg = world.reg;
            world.tick++;
            world.clock += dt;
            std::vector<entt::entity> units, blds;
            reg.view<Info>().each([&](entt::entity e, const Info &i)
                                  { (isBuilding(i.kind) ? blds : units).push_back(e); });

            sys.build.tick(blds, dt); // construction, production, towers
            sys.units.behave(units, dt);
            sys.combat.processKills();
            sys.units.move(units, dt);
            sys.naval.shipsTick(dt);

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

            sys.clearing.clearTick(dt); // clearing jobs
            sys.construction.tick(dt);  // building sites: pawn assignment, S_Jobs
            if (world.tick % 20 == 7)
            {
                sys.hunt.sheepSecond();
                sys.patrol.patrolSecond();
            } // sheep repopulation, patrols
            sys.defense.flushAlerts();
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
            if (world.clock - world.lastSave > 30)
            {
                sys.save.saveGame();
                world.lastSave = world.clock;
            }
        }
    };

} // namespace

int main(int argc, char **argv)
{
    int port = argc > 1 ? std::atoi(argv[1]) : DEFAULT_PORT;
    uint32_t seed = argc > 2 ? uint32_t(std::strtoul(argv[2], nullptr, 10)) : std::random_device{}();
    std::string save = argc > 3 ? argv[3] : "savegame.bin";
    std::string serverName = argc > 4 ? argv[4] : "Base Wars Server";
    bool localOnly = false; // --local: no Steam backend login/advertising (direct IP only)
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]) == "--local")
            localOnly = true;
    for (const char *path : {"server.cfg", "config/server.cfg"})
        if (cfg().load(path))
            break; // optional tuning file, see config/server.cfg
    std::signal(SIGINT, [](int)
                { g_quit = 1; });
    std::signal(SIGTERM, [](int)
                { g_quit = 1; });
    {
        Server s(uint16_t(port), seed, save, serverName, localOnly);
        s.run();
    }
}
