/**
 * @file GameText.hpp
 * @brief Game rules rendered as text for panels (costs are drawn with ui/CostRow): unlocks, next-level previews, troop
 * descriptions.
 */
#pragma once
#include "client/ClientCommon.hpp"
#include "client/Components.hpp"

#include <string>
#include <vector>

int mapUpgradeCost(const Info &in, int r); // client-side estimate of a building upgrade's cost
std::string timeText(float seconds);       // "45 s", "3 min 20 s", "1 h 5 min"
std::string unlockText(int level);         // troops unlocked at an Archery level
// What the next building level changes (plus animated previews of newly unlocked troops).
void nextLevelInfo(const Info &in, std::vector<std::string> &out, std::vector<Kind> &units);

// What reaching Town Hall level `level` unlocks: new buildings, higher building limits, troops, level caps.
void townHallUnlocks(int level, std::vector<std::string> &out);

struct TroopText
{
    const char *role;
    const char *desc;
};
TroopText troopText(Kind k);
void troopCost(Kind k, int toLevel, float *cost); // cost of upgrading a troop type to `toLevel`
