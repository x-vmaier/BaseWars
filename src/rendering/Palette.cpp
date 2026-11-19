/**
 * @file Palette.cpp
 * @brief Team / clan / biome colours and the sprite colour set of an owner.
 */
#include "rendering/Palette.hpp"

#include "rendering/Assets.hpp"

const char *const COLOR_NAME[TEAM_COLORS] = {"Blue", "Red", "Yellow", "Purple", "Black"};

Color teamColor(int owner)
{
    static const Color c[TEAM_COLORS] = {rgb(70, 130, 235), rgb(225, 70, 70), rgb(240, 205, 60), rgb(165, 95, 205),
                                         rgb(95, 95, 110)};
    return owner < 1 ? rgb(235, 235, 235)
                     : c[(isFactionOwner(owner) ? FACTION_COLOR[owner - FACTION_BASE] : (owner - 1)) % TEAM_COLORS];
}
int colorIdx(int owner)
{
    return isFactionOwner(owner) ? FACTION_COLOR[owner - FACTION_BASE] : (owner < 1 ? 0 : (owner - 1) % TEAM_COLORS);
}
Color clanColor(int clan)
{
    static const Color c[8] = {rgb(255, 120, 60),  rgb(70, 220, 190), rgb(240, 90, 170),  rgb(150, 220, 70),
                               rgb(120, 140, 255), rgb(255, 210, 60), rgb(200, 120, 255), rgb(90, 200, 255)};
    return c[(clan + 7) % 8];
}
Color biomeColor(int b)
{
    static const Color c[B_Count] = {rgb(46, 102, 172),  rgb(140, 190, 80),  rgb(50, 120, 58),  rgb(122, 120, 128),
                                     rgb(214, 190, 110), rgb(205, 150, 205), rgb(235, 240, 248)};
    return b >= 0 && b < B_Count ? c[b] : rgb(20, 24, 34);
}
