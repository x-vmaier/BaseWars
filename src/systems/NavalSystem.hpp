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
                  bool reserved = true); // free capacity incl. units walking to embark
    void initShip(entt::entity ship);    // empty boat at upgrade level 1
    bool crewed(entt::entity ship);      // enough troops aboard to sail
    int firepower(entt::entity ship);    // archers aboard + 1 crossbow from ship_crossbow_level (0 = cannot attack)
    bool startUpgrade(int id, entt::entity ship); // pay + start the timed upgrade; false (with a message) if refused
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
    void walkToBoard(entt::entity e, Unit &u, Boarding &bd, Pos sp, int ow);
    void tickVoyage(entt::entity ship, Voyage &v);
    bool voyageGather(entt::entity ship, Voyage &v, Unit &su, Pos sp);
    void voyageSail(entt::entity ship, Voyage &v, Unit &su, Pos sp);
    void voyageLand(entt::entity ship, Voyage &v, Pos sp);
    void tickShip(entt::entity e, float dt);
    void shipFire(entt::entity e, Ship &sh, float dt);
    bool tickUpgrade(entt::entity e, float dt);
    int dockLevel(entt::entity ship);
    void upgradePacket(int id, Reader &r);
    void offloadOrphans(entt::entity e, Ship &sh, Pos sp);
    void boardPacket(int id, Reader &r);
    void disembarkPacket(int id, Reader &r);
    bool canBoard(entt::entity e, int owner);
    bool sendToShip(entt::entity e, entt::entity ship, Pos sp, int owner);
};
