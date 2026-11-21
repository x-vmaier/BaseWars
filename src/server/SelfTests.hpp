/**
 * @file SelfTests.hpp
 * @brief Headless selftest registry and helpers (`server --selftest <name|list|all>`).
 */
#pragma once
#include "common/Snapshot.hpp"
#include "server/Sim.hpp"

#include <cstdio>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
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

    // Joins with the dev ticket (fixtures run the transport in dev-auth mode); returns the player slot or 0.
    int join(uint64_t steamId, uint64_t conn = 0);
    int joinWithTicket(uint64_t steamId, const std::vector<uint8_t> &ticket, uint64_t conn = 0);
    void leave(uint64_t conn);
    void send(uint64_t conn, const Writer &w);
    void run(double seconds) { sim.run(int(seconds * TICK_HZ)); }
    entt::entity hallOf(int id) { return world.townHallOf(id); }
    // First legal site for `k` near the player's Town Hall (placement rules of the real server); false if none.
    bool findSite(int id, Kind k, int &tx, int &ty);
    uint64_t nextConn = 1000;
    std::unordered_map<uint64_t, uint32_t> seqOf; // last command seq `send` stamped per connection
};

// Collects failures; a scenario passes when none were recorded.
class Ctx
{
  public:
    explicit Ctx(std::string scenario) : name(std::move(scenario)) {}
    void check(bool ok, const std::string &what);
    // Runs World::checkInvariants and records every violation.
    void invariants(Fixture &f, const char *when);
    void log(const std::string &s) const { spdlog::info("  [{}] {}", name, s); }
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
void identityTest(Ctx &);
void defeatTest(Ctx &);
void netSnapshotTest(Ctx &);
void interpTest(Ctx &);
void netPredictTest(Ctx &);
void logRotateTest(Ctx &);

int runCli(const std::string &what);

// Order-independent digest of the persisted state (the save file of a fixture, hashed).
uint64_t stateHash(Fixture &f);

} // namespace selftest
