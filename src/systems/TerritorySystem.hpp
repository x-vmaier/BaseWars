/**
 * @file TerritorySystem.hpp
 * @brief Functional territory of buildings, town move and building relocation.
 */
#pragma once
#include "server/World.hpp"

struct Systems;
struct Anchors;

class TerritorySystem
{
  public:
    TerritorySystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void evalFunctional();
    void doMoveTown(int id, int tx, int ty);
    void doRelocate(int id, uint32_t uid, int tx, int ty);
    void relocSecond();

  private:
    World &world;
    entt::registry &reg;
    Systems &s;
    void markOutposts(const std::vector<entt::entity> &mine, Anchors &an, std::vector<uint8_t> &want);
    void applyFunctional(const std::vector<entt::entity> &mine, const Anchors &an, std::vector<uint8_t> &want);
    void migrateTown(int ow, entt::entity nth);
    void finishReloc(entt::entity n);
};
