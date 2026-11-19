#pragma once
// Buildings per tick: construction progress, production + worker respawn, defensive fire.
#include "server/World.hpp"

struct Systems;

class BuildSystem {
public:
    BuildSystem(World& world, Systems& s) : world(world), reg(world.reg), s(s) {}

    float buildTimeFor(entt::entity e, Kind k) const;   // construction time; relocations take much longer
    bool buildingTick(entt::entity e, float dt);        // false when the building must not work (non-functional)
    void tick(const std::vector<entt::entity>& blds, float dt);

private:
    World& world;
    entt::registry& reg;
    Systems& s;
};
