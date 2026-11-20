/**
 * @file NetSession.hpp
 * @brief Connection lifecycle: hello, SteamID authentication, profile lookup / creation / respawn, disconnect.
 */
#pragma once
#include "server/World.hpp"

#include <unordered_map>

struct Systems;

class NetSession
{
  public:
    NetSession(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void onHello(uint64_t conn, Reader &r);
    void onAuthResult(uint64_t conn, bool ok); // asynchronous Steam verdict for a pending hello
    void onDisconnect(uint64_t conn);
    void authSecond(); // drops hellos whose verdict never arrived (stuck-state exit)
    size_t pendingCount() const { return pending.size(); }

  private:
    struct PendingHello
    {
        uint64_t steamId;
        std::vector<uint64_t> friends;
        double since;
    };
    void finishHello(uint64_t conn, uint64_t steamId, const std::vector<uint64_t> &friends);
    void reject(uint64_t conn, const char *why);
    void bindConnection(uint64_t conn, int id, uint64_t steamId, const std::vector<uint64_t> &friends);
    std::unordered_map<uint64_t, PendingHello> pending; // conn -> hello waiting for Steam
    World &world;
    entt::registry &reg;
    Systems &s;

    int claimSlot();
    void initNewProfile(int id, uint64_t steamId);
    void respawnIfDestroyed(int id, const std::vector<uint64_t> &friends);
    void sendWelcome(uint64_t conn, int id);
};
