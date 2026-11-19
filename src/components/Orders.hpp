#pragma once
// Unit orders and simple behaviours.
#include "common/Shared.hpp"

#include <cstdint>

struct Attack
{
    uint32_t target;
};

struct AMove
{
    Pos dest;
};

struct Worker
{
    uint32_t home;
    uint8_t phase = 0;
    float cargo[R_Count]{};
    float timer = 0;
    uint32_t dest = 0;
}; // 0 wait, 1 loading, 2 carry, 3 return

// Heals the most wounded ally in range (a Monk); healing power is the kind's dps.
struct Healer
{
};

struct Wander
{
    float timer;
};

// Pawn sent to clear a surface feature: the tile, plus the work it was doing before (restored when the job ends).
struct ClearTask
{
    int64_t tile;
    bool hadWorker = false;
    Worker worker{};
    bool hadDest = false;
    Pos dest{0, 0};
    float repath = 0, bestDist = 1e9f;
    int stuck = 0;
};

// Pawn sent to a building site (construction / upgrade / repair), plus its previous work (restored when the job ends).
struct BuildTask
{
    uint32_t site;
    bool hadWorker = false;
    Worker worker{};
    bool hadDest = false;
    Pos dest{0, 0};
    float repath = 0, bestDist = 1e9f;
    int stuck = 0;
};
