#pragma once
// Client-only ECS components (one entity per server entity in the last S_State).
#include "common/Shared.hpp"

#include <cstdint>

struct NetId
{
    uint32_t id;
}; // server entity id

struct Info
{
    Kind kind;
    uint8_t owner, state, aux, aim, prog;
    float hp, maxhp;
    uint16_t stock;
}; // replicated state

struct RPos
{
    float x, y;
}; // rendered (interpolated) position, tiles

struct Goal
{
    float x, y;
}; // last authoritative position, tiles

struct Face
{
    bool left = false;
}; // sprite facing

struct Seen
{
    uint32_t stamp;
}; // S_State counter of the last update

struct Remembered
{
}; // NPC faction building outside vision: last known state, drawn dimmed

// Units aboard a ship arrive with prog == UNIT_ABOARD_PROG and are not drawn / picked / selected.
inline bool aboardUnit(const Info &i)
{
    return !isBuilding(i.kind) && i.prog == UNIT_ABOARD_PROG;
}
