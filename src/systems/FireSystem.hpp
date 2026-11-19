#pragma once
// Fire arrows, burning buildings and firefighting units.
#include "server/World.hpp"

struct Systems;

inline constexpr float BURN_TIME = 12.f, BURN_FRAC_PER_SEC = .016f, EXTINGUISH_PER_SEC = 3.f;

class FireSystem
{
public:
    FireSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void ignite(entt::entity b, int byOwner);
    void archerShot(int ow, Pos from, Pos to, entt::entity tg); // fire arrows at troop level 3
    bool canExtinguish(entt::entity e);
    void burnSecond();
    bool extinguishTick(entt::entity e, Unit &u, Pos &p, float dt); // true while the unit is busy firefighting

private:
    World &world;
    entt::registry &reg;
    Systems &s;
};
