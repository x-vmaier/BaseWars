/**
 * @file UnitAnims.hpp
 * @brief Sprite selection: unit animations by state, firefighting animations, building sprite metrics.
 */
#pragma once
#include "common/Shared.hpp"
#include "rendering/Assets.hpp"

Anim unitAnim(Kind k, int owner, int state, int aux, int aim, bool moving, int biome, double t);
Anim extinguishAnim(Kind k, int owner); // bucket / hammer duty: pawns scrub, soldiers stamp at the flames

struct BSpr
{
    const char *file;
    float scale, artBottom, artH, artW;
};
BSpr buildingSprite(Kind k, int level); // file is "" for buildings drawn procedurally (mine, campfire)
