/**
 * @file ServerMain.cpp
 * @brief Authoritative server entry point and tick loop.
 */
#include "server/SelfTests.hpp"
#include "server/Sim.hpp"

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
    Server(uint16_t port, uint32_t seed, std::string path, const std::string &serverName, bool localOnly)
        : sim(seed, std::move(path))
    {
        World &world = sim.world;
        bool existed = std::filesystem::exists(world.savePath);
        bool loaded = sim.sys.save.loadGame();
        if (!loaded && existed)
        {
            std::error_code ec;
            std::filesystem::rename(world.savePath, world.savePath + ".bad", ec); // never lose the old file
            if (world.loadStarted)
            {
                std::printf("Savegame could not be read; kept as %s.bad. Restart the server to begin a fresh world.\n",
                            world.savePath.c_str());
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
        std::printf("Server listening on port %d (Steam Networking Sockets) (seed %u, %s, steam %s)\n", port,
                    world.seed, loaded ? "savegame loaded" : "fresh world", world.net.relayActive() ? "on" : "off");
    }
    ~Server() { sim.sys.save.saveGame(); }
    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    void run()
    {
        using clk = std::chrono::steady_clock;
        auto next = clk::now();
        const auto step = std::chrono::milliseconds(1000 / TICK_HZ);
        while (!g_quit)
        {
            sim.pollNet();
            sim.publishPresence();
            sim.update(1.f / TICK_HZ);
            sim.world.net.flush();
            next += step;
            auto now = clk::now();
            if (next < now - std::chrono::seconds(1))
                next = now;
            std::this_thread::sleep_until(next);
        }
        std::puts("Shutting down, saving world...");
    }

  private:
    Sim sim;
};

} // namespace

int main(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; i++)
        if (std::string(argv[i]) == "--selftest")
            return selftest::runCli(argv[i + 1]);
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
    std::signal(SIGINT, [](int) { g_quit = 1; });
    std::signal(SIGTERM, [](int) { g_quit = 1; });
    {
        Server s(uint16_t(port), seed, save, serverName, localOnly);
        s.run();
    }
}
