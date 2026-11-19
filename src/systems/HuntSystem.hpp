#pragma once
// Wildlife: sheep spawning / repopulation and free pawns hunting them.
#include "server/World.hpp"

struct Systems;

class HuntSystem
{
public:
    HuntSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void spawnChunkNpcs(int cx, int cy);
    entt::entity nearestOwnStorage(int ow, Pos p, bool needRoom);
    void huntPause(entt::entity e); // the player gave this pawn an order
    bool isFreePawn(entt::entity e) const;
    uint8_t worldAux(entt::entity e) const;                           // sendState: show the meat a hunter carries
    bool huntKill(entt::entity sheep, int killer);                    // a hunter pawn that killed a sheep carries the meat home
    bool huntTick(entt::entity e, Unit &u, int ow, Pos &p, float dt); // true when it handled the unit's behaviour
    void sheepSecond();

private:
    World &world;
    entt::registry &reg;
    Systems &s;
    struct SheepRec
    {
        uint32_t uid;
        Pos p;
    };
    std::vector<SheepRec> sheepList;
    std::unordered_map<uint32_t, uint32_t> sheepClaim; // sheep uid -> hunter uid
    double nextRepop = 8;
};
