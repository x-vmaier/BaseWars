/**
 * @file WindSystem.hpp
 * @brief The world wind: slow drift of its direction, S_Wind replication and the save section.
 */
#pragma once
#include "common/Wind.hpp"
#include "server/World.hpp"

struct Systems;

class WindSystem
{
  public:
    WindSystem(World &world, Systems &s);

    Wind &wind() { return reg.ctx().get<Wind>(); }
    void second();     // drift (once per second)
    void send(int id); // S_Wind
    void save(Writer &w) const;
    void load(Reader &r);

  private:
    float roll(Wind &w, float lo, float hi); // private generator, so the world rng is not consumed
    World &world;
    entt::registry &reg;
    Systems &s;
};
