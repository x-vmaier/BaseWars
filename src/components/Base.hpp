#pragma once
// Base mechanics: building flags (fire, territory), relocation, training queues, support patrols, firefighting, site
// jobs.
#include "common/Shared.hpp"

#include <cstdint>
#include <vector>

struct BFlags
{
    uint8_t dis = 0;
    float burn = 0;
    uint8_t burnBy = 0;
}; // BLD_DISABLED (outside functional territory), burn timer

struct Moving
{
    uint32_t to;
}; // old building that is being replaced (BLD_MOVING)

struct RelocNew
{
    uint32_t from;
    bool town;
    float total;
}; // new building replacing `from` (construction time `total`)

struct QItem
{
    Kind k;
    float t, total, wait = 0;
};

struct TrainQ
{
    std::vector<QItem> q;
};

struct Support
{
    Pos dest, home;
    uint8_t victim = 0;
    uint8_t phase = 0;
    bool defense = false;
    float calm = 0, travel = 0;
    int fails = 0;
};

struct Extinguish
{
    uint32_t target;
    float t = 0;
};

// Pawn work at a building site (SiteKind: construction while Building.progress < 1, upgrade, repair). The timer only
// runs while the assigned pawn stands at the site (SitePhase); `paid` is refunded when an upgrade / repair is
// cancelled.
struct SiteJob
{
    uint8_t kind = 0, phase = 0;
    float t = 0, total = 0, hpRate = 0;
    uint32_t pawn = 0;
    float paid[R_Count]{};
    double since = 0;
    int fails = 0;
};
