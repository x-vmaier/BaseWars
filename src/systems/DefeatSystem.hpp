/**
 * @file DefeatSystem.hpp
 * @brief Losing the last Town Hall: burning ruin, defeat banner state, resettlement offers (S_Defeat).
 */
#pragma once
#include "server/World.hpp"

struct Systems;

class DefeatSystem
{
  public:
    DefeatSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    // The player's last Town Hall (at tile `hall`) was destroyed by `killer` (an owner id, 0 = unknown).
    void defeat(int victim, int killer, IV hall);
    // A defeated player without a town yet (old save, town lost while away): same flow without a ruin.
    void beginWithoutRuin(int id);
    bool blocked(int id) const { return world.bp[id].defeat.phase != DF_None; } // only resettle commands work then

    void onResettle(int id); // C_Resettle: first offer, or the next candidate while one is pending
    void onAccept(int id);   // C_ResettleAccept
    void second();           // ruin expiry, offer timeout, state refresh for online players
    void onLogin(int id);    // the offer timer only runs while the player is online
    void sendState(int id);

  private:
    void makeOffer(int id);
    void accept(int id);
    void expireRuins(double now);
    void testDefeat();
    World &world;
    entt::registry &reg;
    Systems &s;
};
