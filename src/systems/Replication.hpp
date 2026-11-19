#pragma once
// Outgoing world state: S_State, S_Fog, S_Roster, S_Map, S_Queue, S_BaseInfo, S_ShipInfo, S_Paths.
#include "server/World.hpp"

struct Systems;

class Replication {
public:
    Replication(World& world, Systems& s) : world(world), reg(world.reg), s(s) {}

    void sendState(int id);
    void sendFog(int id);
    void sendRoster();
    void sendMap(int id);                        // faction towns the player has discovered
    void sendQueues(int id);
    void sendBaseInfo(int id);
    void sendShipInfo(int id);
    void sendPaths(int id);                      // path lines of the viewer's units and those of players sharing vision

private:
    World& world;
    entt::registry& reg;
    Systems& s;
    std::unordered_map<uint32_t, uint64_t> pathSent[MAX_PLAYERS + 1];   // per player: uid -> hash of the path last sent
};
