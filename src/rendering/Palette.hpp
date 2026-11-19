#pragma once
// Team / clan / biome colours and the sprite colour set of an owner.
#include "common/Shared.hpp"

#include <raylib.h>

extern const char* const COLOR_NAME[TEAM_COLORS];   // sprite colour folders: "Blue", "Red", ...
Color teamColor(int owner);
int colorIdx(int owner);                             // index into COLOR_NAME for an owner (players and factions)
Color clanColor(int clan);
Color biomeColor(int biome);                         // map / minimap colour of a biome
