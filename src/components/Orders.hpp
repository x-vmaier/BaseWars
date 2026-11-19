#pragma once
// Unit orders and simple behaviours.
#include "common/Shared.hpp"

#include <cstdint>

struct Attack { uint32_t target; };
struct AMove { Pos dest; };
struct Worker { uint32_t home; uint8_t phase = 0; float cargo[R_Count]{}; float timer = 0; uint32_t dest = 0; };  // 0 wait, 1 loading, 2 carry, 3 return
struct Wander { float timer; };
