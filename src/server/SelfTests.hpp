/**
 * @file SelfTests.hpp
 * @brief Headless selftest registry and helpers (`server --selftest <name|list|all>`).
 */
#pragma once
#include "server/Sim.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace selftest
{

// One scenario's world: temp save file, fake connections, no Steam. Fixed seed => deterministic.
class Fixture
{
  public:
    explicit Fixture(uint32_t seed = 42);
    ~Fixture();
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;

    Sim sim;
    World &world = sim.world;
    Systems &sys = sim.sys;
    std::string savePath;

    int join(const std::string &uuid, const std::string &name, uint64_t steamId = 0, uint64_t conn = 0);
    void leave(uint64_t conn);
    void send(uint64_t conn, const Writer &w);
    void run(double seconds) { sim.run(int(seconds * TICK_HZ)); }
    entt::entity hallOf(int id) { return world.townHallOf(id); }
    // First legal site for `k` near the player's Town Hall (placement rules of the real server); false if none.
    bool findSite(int id, Kind k, int &tx, int &ty);
    uint64_t nextConn = 1000;
};

// Collects failures; a scenario passes when none were recorded.
class Ctx
{
  public:
    explicit Ctx(std::string scenario) : name(std::move(scenario)) {}
    void check(bool ok, const std::string &what);
    // Runs World::checkInvariants and records every violation.
    void invariants(Fixture &f, const char *when);
    void log(const std::string &s) const { std::printf("  [%s] %s\n", name.c_str(), s.c_str()); }
    int failures = 0;
    std::string name;
};

struct Scenario
{
    const char *name;
    const char *about;
    void (*run)(Ctx &);
};

// Scenarios live in selftests/*.cpp and are listed in SelfTests.cpp.
void bootTest(Ctx &);
void saveloadTest(Ctx &);
void economyTest(Ctx &);
void raidTest(Ctx &);
void netGarbageTest(Ctx &);
void invariantsTest(Ctx &);

int runCli(const std::string &what);

// Order-independent digest of the persisted state (the save file of a fixture, hashed).
uint64_t stateHash(Fixture &f);

} // namespace selftest
