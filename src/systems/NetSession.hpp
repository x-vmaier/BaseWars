#pragma once
// Connection lifecycle: hello (profile lookup / creation / respawn), disconnect.
#include "server/World.hpp"

struct Systems;

class NetSession
{
public:
    NetSession(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void onHello(uint64_t conn, Reader &r);
    void onDisconnect(uint64_t conn);

private:
    World &world;
    entt::registry &reg;
    Systems &s;
};
