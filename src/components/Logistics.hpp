/**
 * @file Logistics.hpp
 * @brief Pawns carrying resources between players / storage buildings.
 */
#pragma once
#include "common/Shared.hpp"

#include <cstdint>

struct Convoy
{
    uint8_t from, to;
    float cargo[R_Count];
    float repath = 0, bestDist = 1e9f;
    int stuck = 0;
}; // tribute pawn walking to a clan mate

struct Haul
{
    uint32_t storage;
    uint8_t phase = 0;
    float cargo[R_Count]{};
    float repath = 0, bestDist = 1e9f;
    int stuck = 0;
    uint8_t boatPhase = 255;
}; // pawn troop fetching stock from an outpost / storage house
