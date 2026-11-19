#pragma once
// Units per tick: behaviour dispatch (wander, firefighting, workers, convoys, raids, hunting, patrols, monks, combat) and movement.
#include "server/World.hpp"

struct Systems;

class UnitSystem {
public:
    UnitSystem(World& world, Systems& s) : world(world), reg(world.reg), s(s) {}

    void behave(const std::vector<entt::entity>& units, float dt);
    void move(const std::vector<entt::entity>& units, float dt);

private:
    World& world;
    entt::registry& reg;
    Systems& s;
};
