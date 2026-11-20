/**
 * @file SaveSystem.hpp
 * @brief Binary savegame: a mandatory part followed by optional appended sections.
 */
#pragma once
#include "server/World.hpp"

struct Systems;

inline constexpr uint32_t SAVE_MAGIC =
    0x39535742u; // "BWS9" (BWS8 + profiles keyed by SteamID only: no uuid, no stored name)

class SaveSystem
{
  public:
    SaveSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s) {}

    void saveGame();
    bool loadGame();

  private:
    // Mandatory part (writes and reads must stay in the same order).
    void writePlayers(Writer &w);
    void writeClans(Writer &w);
    void writeWorldState(Writer &w);
    void writeEntities(Writer &w);
    void writeEntity(Writer &w, entt::entity e, const Uid &uid, const Owner &o, const Info &in, const Health &h);
    void writeFile(const Writer &w);
    bool readPlayers(Reader &r);
    void readClans(Reader &r);
    void readWorldState(Reader &r);
    bool readEntities(Reader &r);
    void readBuilding(Reader &r, entt::entity e, Kind k, uint32_t uid);
    void readUnit(Reader &r, entt::entity e, Kind k, uint16_t town);
    void readOptionalSections(Reader &r);

    // Appended sections.
    void worldSave(Writer &w); // cleared tiles
    void worldLoad(Reader &r);
    void shipsSave(Writer &w); // [u32 nShips]{u32 shipUid, u16 n, u32 passengerUid * n}
    void shipsLoad(Reader &r);
    void saveBase(Writer &w); // "BSE2": per-player base state, building / unit base components
    void loadBase(Reader &r);
    void writeBasePlayers(Writer &w);
    void writeBaseEntity(Writer &w, entt::entity e);
    void readBasePlayers(Reader &r);
    void readBaseEntity(Reader &r);
    template <class F> std::vector<entt::entity> byUid(F keep);
    World &world;
    entt::registry &reg;
    Systems &s;
};
