#pragma once
// Client -> server commands (C_*). Each method writes one message (cmdClear may split into several).
#include "common/Shared.hpp"

#include <raylib.h>

#include <string>
#include <vector>

class App;

class Commands
{
  public:
    explicit Commands(App &app) : app(app) {}
    void move(Vector2 world); // selected units; adds a click marker (and a ripple on water)
    void attackMove(Vector2 world);
    void attack(uint32_t target);
    void ally(int to, int act);
    void train(uint32_t building, Kind k);
    void upgrade(uint32_t building);
    void clanCreate(const std::string &name);
    void clanJoin(int clan);
    void clanLeave();
    void transfer(int to, int res, float amount);
    void donate(int faction, int res, float amount);
    void setHaul(uint32_t building, int pct);
    void upgradeUnit(Kind k);
    void chat(int ch, const std::string &text);
    void clanLeader(int id);
    void build(Kind k, IV t);
    void clear(const std::vector<IV> &tiles,
               const std::vector<uint32_t> &pawns = {}); // pawns: who should go (empty = nearest free pawn)
    void board(uint32_t ship, const std::vector<uint32_t> &units);
    void disembark(uint32_t ship);
    void quotas(const int *perType); // QUOTA_TYPES values (Warrior, Archer, Lancer, Monk)
    void setAlloc(int res, int pct); // Town Hall storage share of a resource
    void dispose(int res);           // discard all of a resource
    void support(float x, float y, int n);
    void moveTown(IV t);
    void relocate(uint32_t building, IV t);
    void dequeue(uint32_t building, int idx);
    void repair(uint32_t building, int mode); // 0 start a repair, 1 cancel the running upgrade / repair
    void camera(Vector2 camTarget);           // where we look (server streams nearby chunks)

  private:
    App &app;
};
