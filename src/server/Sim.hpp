/**
 * @file Sim.hpp
 * @brief One server world with all its systems and the per-tick update; shared by the server loop and selftests.
 */
#pragma once
#include "server/World.hpp"
#include "systems/Systems.hpp"

class Sim
{
  public:
    Sim(uint32_t seed, std::string savePath) : world(seed, std::move(savePath)), sys(world) {}
    Sim(const Sim &) = delete;
    Sim &operator=(const Sim &) = delete;

    World world;
    Systems sys;

    void pollNet();
    void publishPresence();
    void update(float dt);
    void run(int ticks); // headless fast-forward: `ticks` updates at TICK_HZ

  private:
    void baseSecond();
    void slowSystems(const std::vector<entt::entity> &units, const std::vector<entt::entity> &blds);
    void replicate();
};
