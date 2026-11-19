#pragma once
// Core components shared by every server entity: identity, ownership, health, kind/level, building and unit state, last attacker.
#include "common/Shared.hpp"

#include <climits>
#include <cstdint>
#include <vector>

struct Uid
{
    uint32_t id;
};

struct Owner
{
    uint8_t id;
}; // 0 = neutral

struct Health
{
    float hp, maxhp;
    uint8_t lastHit;
};

struct Info
{
    Kind kind;
    uint8_t level;
};

struct Building
{
    int tx, ty;
    float progress, stock[R_Count], respawn, cd;
    uint8_t haulPct = 75;
}; // storage buildings keep their stock here

struct Unit
{
    std::vector<Pos> path;
    size_t i = 0;
    uint8_t state = US_Idle, aim = 0;
    float cd = 0, repath = 0;
    IV lastRev{INT_MIN, INT_MIN};
};

struct Hit
{
    uint32_t by = 0;
    double t = -100;
}; // last attacker
