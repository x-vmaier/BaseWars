#pragma once
// Resource logistics: building workers, clan tribute convoys (+ clan spotting), hauling parties.
#include "server/World.hpp"

struct Systems;

class ConvoyHaulSystem {
public:
    ConvoyHaulSystem(World& world, Systems& s) : world(world), reg(world.reg), s(s) {}

    void runWorker(entt::entity e, Unit& u, Worker& w, float dt);
    void runConvoy(entt::entity e, Unit& u, Convoy& cv, float dt);
    void updateSpotting();                                   // convoys seen by a clan member are shown to the whole clan
    bool spottedForClan(int id, uint32_t uid) const;
    void spawnHauls();
    void runHaul(entt::entity e, Unit& u, Haul& h, float dt);

private:
    World& world;
    entt::registry& reg;
    Systems& s;
    void refundConvoy(entt::entity e, const Convoy& cv, const std::string& why);
    struct Spot { double until; uint8_t by; };
    std::unordered_map<uint8_t, std::unordered_map<uint32_t, Spot>> clanSpot;   // clan -> convoys its members have spotted
};
