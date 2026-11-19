#pragma once
// NPC faction units (raids, town garrisons, patrols) and free hunting pawns.
#include <cstdint>

struct Raider
{
    uint8_t target;
    double until;
    float repath = 0;
}; // faction raiding party

struct Home
{
    float x, y, helpUntil = 0;
}; // faction units idle around their town

struct TownTag
{
    uint16_t town;
};

struct Patrol
{
    uint32_t group;
    uint8_t slot;
    float repath = 0;
}; // member of an AI patrol group

struct Hunt
{
    float meat = 0;
    double pause = 0, scan = 0;
    uint32_t storage = 0;
}; // free pawn: hunts sheep, carries meat home
