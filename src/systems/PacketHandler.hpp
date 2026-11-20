/**
 * @file PacketHandler.hpp
 * @brief Decodes client commands (C_*) and dispatches them to the systems.
 */
#pragma once
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
    void baseCommand(int id, uint8_t type, Reader &r);
    std::vector<entt::entity> readUnits(int id, Reader &r, bool combatOnly);

    void onCam(Profile &p, Reader &r);
    void onBuild(int id, Profile &p, Reader &r);
    void onTrain(int id, Reader &r);
    void onClanCreate(int id, Profile &p, Reader &r);
    void onClanJoin(int id, Profile &p, Reader &r);
    void onClanLeader(int id, Profile &p, Reader &r);
    void onSetHaul(int id, Reader &r);
    void onUpgradeUnit(int id, Profile &p, Reader &r);
    void onChat(int id, Profile &p, Reader &r);
    void onTransfer(int id, Profile &p, Reader &r);
    void onDonate(int id, Reader &r);
    void onUpgrade(int id, Reader &r);
    void onMove(int id, Reader &r);
    void onAttackMove(int id, Reader &r);
    void onAttack(int id, Profile &p, Reader &r);
    void onAlly(int id, Profile &p, Reader &r);

    // Base-management commands, dispatched by baseCommand.
    void onSetQuota(int id, Reader &r);
    void onSetQuotas(int id, Reader &r);
    void onSetAlloc(int id, Reader &r);
    void onDispose(int id, Reader &r);
    void onSupport(int id, Reader &r);
    void onMoveTown(int id, Reader &r);
    void onRelocate(int id, Reader &r);
    void onDequeue(int id, Reader &r);
};
