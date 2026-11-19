#pragma once
// Clearing of surface features (trees, rocks, gold veins), resource drops, gold-deposit mine bonus.
#include "server/World.hpp"

struct Systems;

class ClearingSystem {
public:
    ClearingSystem(World& world, Systems& s) : world(world), reg(world.reg), s(s) {}

    std::vector<IV> blockedTiles(Kind k, int tx, int ty);   // feature tiles under a footprint that still have to be cleared
    bool worldBuildOk(int id, Kind k, int tx, int ty);      // C_Build hook: surface features must be cleared first
    bool inClearReach(int id, int x, int y);
    void onClear(int id, Reader& r);
    void sendDrop(Pos p, int res, int amt, int credited);   // visible bouncing "+N" drop for everyone who can see the spot
    void creditDrop(int player, Pos p, int res, int amt);
    void sendClearedList(int id, const std::vector<int64_t>& tiles, bool full);
    void sendClearedFull(int id);
    void clearTick(float dt);
    float mineMul(entt::entity e, Kind k, Pos c);           // output multiplier from nearby gold deposits (cached per clear-version)

private:
    World& world;
    entt::registry& reg;
    Systems& s;
    struct ClearJob { int owner; uint8_t feat; float t, total; };
    std::unordered_map<int64_t, ClearJob> clearJobs;
    bool jobsWereSent = false;
    std::unordered_map<uint32_t, std::pair<uint32_t, float>> mineCache;
    std::vector<Source> dropSrc[MAX_PLAYERS + 1];
    uint32_t dropSrcTick[MAX_PLAYERS + 1]{};
};
