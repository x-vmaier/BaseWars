#pragma once
// Naval transport: Ship (passenger list, on every K_Ship), Aboard (on a unit while it is on a ship: hidden/untargetable),
// Boarding (unit walking to embark), Voyage (a ship executing a planned transport).
#include "common/Shared.hpp"

#include <cstdint>
#include <vector>

struct Ship { std::vector<uint32_t> pax; double idleSince = 0; };
struct Aboard { uint32_t ship; };
struct Boarding { uint32_t ship; bool meetSet = false; Pos meet{0, 0}; float repath = 0; int stuck = 0; };
struct Voyage {
    uint8_t phase = 0;                                   // 0 gather, 1 sail, 2 unload
    std::vector<uint32_t> units;
    IV board{0, 0}, boardWater{0, 0}, land{0, 0}, landWater{0, 0};
    Pos dest{0, 0};
    uint32_t destUid = 0;
    double start = 0, arrivedAt = -1, lastOrder = -100;
    int fails = 0;
    bool retried = false;
};
