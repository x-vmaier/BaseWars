/**
 * @file KindFactories.hpp
 * @brief Server-side hooks per kind: extra components on creation and what happens when a unit is trained.
 */
#pragma once
#include "common/KindDefs.hpp"

#include <entt/entt.hpp>

class World;
struct Systems;
struct Pos;

// Optional hooks of a kind (see KindFactories.cpp). Behaviour itself lives in systems that iterate the components.
struct KindFactory
{
    // Adds the kind's own components to a fresh entity (loaded = restored from a save, no randomness).
    void (*attach)(World &, entt::entity, bool loaded) = nullptr;
    // Runs after a building trained this unit; null = plain dust puff.
    void (*onTrained)(Systems &, World &, entt::entity unit, entt::entity building, Pos at) = nullptr;
};

const KindFactory &kindFactory(Kind k);
