/**
 * @file NetSession.hpp
 * @brief Connection lifecycle: hello (profile lookup / creation / respawn), disconnect.
 */
#pragma once
#include "server/World.hpp"

struct Systems;

class NetSession
{
  public:
    NetSession(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void onHello(uint64_t conn, Reader &r);
    void onDisconnect(uint64_t conn);

  private:
    void bindConnection(uint64_t conn, int id, const std::string &name, uint64_t steamId,
                        const std::vector<uint64_t> &friends);
    World &world;
    entt::registry &reg;
    Systems &s;

    int claimSlot();
    void initNewProfile(int id, const std::string &uuid);
    void respawnIfDestroyed(int id, const std::vector<uint64_t> &friends);
    void sendWelcome(uint64_t conn, int id);
};
