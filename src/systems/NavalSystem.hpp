/**
 * @file NavalSystem.hpp
 * @brief Ships: capacity, embark / disembark, voyages, ship commands (C_Board, C_Disembark) and ship fields of S_State.
 */
#pragma once
#include "server/World.hpp"

struct Systems;

class NavalSystem
{
  public:
    NavalSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    float cargoTotal(entt::entity e) const;
    void paxStats(entt::entity ship, int &troops, float &res,
                  int *kinds = nullptr); // passengers aboard (stale uids are purged)
    void shipFree(entt::entity ship, int &troopsFree, float &resFree,
                  bool reserved = true);                // free capacity incl. units walking to embark
    void initShip(entt::entity ship, int harbourLevel); // empty boat, tier = harbour level
    void embark(entt::entity e, entt::entity ship);
    std::vector<IV> freeLandTiles(Pos center, int rmax);
    std::vector<entt::entity> landUnits(entt::entity ship, const std::vector<entt::entity> &us, Pos center, int rmax);
    std::vector<entt::entity> passengers(entt::entity ship);
    void cancelVoyage(entt::entity ship);
    void runBoarding(entt::entity e, Unit &u, Boarding &bd, float dt);
    void shipsTick(float dt);
    bool shipPacket(int id, uint8_t type, Reader &r);
    // Called from sendState for every record: false hides it. Fills the ship/boarded fields.
    bool shipStateHook(int viewer, entt::entity e, int ownerId, const Info &in, uint8_t &aux, uint8_t &prog,
                       uint16_t &stock);

  private:
    World &world;
    entt::registry &reg;
    Systems &s;
    void tickVoyage(entt::entity ship, Voyage &v);
};
