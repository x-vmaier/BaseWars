/**
 * @file Replication.hpp
 * @brief Outgoing world state: S_State, S_Fog, S_Roster, S_Map, S_Queue, S_BaseInfo, S_ShipInfo, S_Paths.
 */
#pragma once
#include "server/World.hpp"

struct Systems;

struct PathCand
{
    uint32_t uid;
    float d; // distance to the viewer camera
    std::vector<IV> pts;
    uint64_t h;
};

class Replication
{
  public:
    Replication(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void sendState(int id);
    void sendFog(int id);
    void sendRoster();
    void sendMap(int id); // faction towns the player has discovered
    void sendQueues(int id);
    void sendBaseInfo(int id);
    void sendShipInfo(int id);
    void sendPaths(int id); // path lines of the viewer's units and those of players sharing vision

  private:
    World &world;
    entt::registry &reg;
    Systems &s;

    bool writeStateEntity(Writer &w, int id, const SrcGrid &grid, entt::entity e, const Uid &uid, const Owner &o,
                          const Info &in, const Health &h);
    void unitFields(entt::entity e, const Unit &u, uint8_t &state, uint8_t &aux, uint8_t &aim);
    void buildingFields(entt::entity e, const Info &in, uint8_t &state, uint8_t &aux, uint8_t &aim, uint8_t &prog,
                        uint16_t &stock);
    std::unordered_map<int64_t, Bits> sharedExplored(int id);
    void writeRosterPlayers(Writer &w, int id, const Pos *thPos, const bool *hasTh);
    void writeRosterClans(Writer &w);
    std::vector<PathCand> collectPaths(int id);
    uint16_t writePathDeltas(Writer &w, std::unordered_map<uint32_t, uint64_t> &sent, const std::vector<PathCand> &cs);

    std::unordered_map<uint32_t, uint64_t> pathSent[MAX_PLAYERS + 1]; // per player: uid -> hash of the path last sent
};
