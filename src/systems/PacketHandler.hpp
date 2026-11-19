#pragma once
// Decodes client commands (C_*) and dispatches them to the systems.
#include "server/World.hpp"

struct Systems;

class PacketHandler
{
public:
    PacketHandler(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void onPacket(uint64_t conn, const std::vector<uint8_t> &data);

private:
    World &world;
    entt::registry &reg;
    Systems &s;
    void baseCommand(int id, uint8_t type, Reader &r); // quota, support patrol, town move, relocation, dequeue
};
