/**
 * @file TrainingSystem.hpp
 * @brief Per-building training queues (C_Train / C_Dequeue) and spawning of trained units.
 */
#pragma once
#include "server/World.hpp"

struct Systems;
inline constexpr int MAX_QUEUE = 5;

class TrainingSystem
{
  public:
    TrainingSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    int queuedUnits(int id);
    // Creates the unit that a finished queue entry produces. Null if there is no room.
    entt::entity spawnTrained(entt::entity be, Kind k);
    void enqueueTrain(int id, entt::entity be, Kind k); // C_Train: pay now, produce over time
    void dequeueTrain(int id, entt::entity be, int idx);
    void trainTick(entt::entity e, float dt);

  private:
    World &world;
    entt::registry &reg;
    Systems &s;
};
