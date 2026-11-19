#pragma once
// Progression rules shared by server (enforcement) and client (previews, greyed-out buttons with the reason):
// building levels 1..MAX_LEVEL gated by the Town Hall level (one table), exponential upgrade cost / time curves,
// storage capacity, food (meat) cost shares, site jobs (construction / upgrade / repair) and damage efficiency.
// Tunables come from ServerConfig; the server sends them to clients in S_Welcome (putRules / getRules).
#include "common/Shared.hpp"

#include <cmath>
#include <cstring>
#include <string>

inline constexpr int MAX_LEVEL = 10;

// Town Hall gates (the one table)
// Per building kind: Town Hall level needed to build it, whether its level is capped by the Town Hall level, and the
// number allowed at each Town Hall level 1..MAX_LEVEL (0 = not yet).
struct BuildGate
{
    int thUnlock;
    bool capByTh;
    int count[MAX_LEVEL];
};

inline constexpr BuildGate BUILD_GATE[K_Archery + 1] = {
    /* Town Hall */ {1, false, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1}},
    /* Outpost   */ {3, true, {0, 0, 1, 1, 2, 2, 3, 3, 4, 4}},
    /* Harbour   */ {4, true, {0, 0, 0, 1, 1, 1, 2, 2, 2, 3}},
    /* Storage   */ {2, true, {0, 1, 1, 2, 2, 2, 3, 3, 3, 4}},
    /* Mine      */ {1, true, {1, 2, 2, 3, 3, 4, 4, 5, 5, 6}},
    /* Lumber    */ {1, true, {1, 2, 2, 3, 3, 4, 4, 5, 5, 6}},
    /* Campfire  */ {1, true, {2, 3, 4, 5, 6, 7, 8, 9, 10, 12}},
    /* Archery   */ {1, true, {1, 1, 1, 1, 2, 2, 2, 2, 3, 3}},
};

inline int thUnlock(Kind k)
{
    return k <= K_Archery ? BUILD_GATE[k].thUnlock : 1;
}

inline int maxCount(Kind k, int th)
{
    return k <= K_Archery && th >= 1 ? BUILD_GATE[k].count[std::min(th, MAX_LEVEL) - 1] : 0;
}

// Highest level a building may reach with the given Town Hall level (never above it, except the Town Hall itself).
inline int maxLevelFor(Kind k, int th)
{
    if (k == K_Campfire)
        return 1;
    if (k > K_Archery || !BUILD_GATE[k].capByTh)
        return MAX_LEVEL;
    return std::clamp(th, 1, MAX_LEVEL);
}

// Troops: Town Hall level needed (the Archery level gate is unitUnlockLevel in Shared.hpp).
inline int unitThLevel(Kind u)
{
    return u == K_Archer ? 2 : (u == K_Monk ? 3 : (u == K_Lancer || u == K_Ship ? 4 : 1));
}

enum GateErr
{
    G_OK,
    G_NoHall,
    G_Locked,
    G_Count
};

// Can one more building of kind k be placed? th = own Town Hall level (0 = none), have = existing + queued of that kind.
inline int buildGate(Kind k, int th, int have)
{
    if (k == K_TownHall)
        return G_OK; // homeless founding / town move are handled separately
    if (th < 1)
        return G_NoHall;
    if (th < thUnlock(k))
        return G_Locked;
    if (have >= maxCount(k, th))
        return G_Count;
    return G_OK;
}

inline std::string aName(Kind k)
{
    std::string n = KINFO[k].name;
    return (std::strchr("AEIOU", n[0]) ? "an " : "a ") + n;
} // "an Outpost"

inline std::string pluralName(Kind k)
{
    std::string n = KINFO[k].name;
    return n + (n.back() == 's' ? "es" : "s");
}

inline std::string gateText(int err, Kind k, int th)
{
    switch (err)
    {
    case G_NoHall:
        return "Requires a Town Hall";
    case G_Locked:
        return "Requires Town Hall level " + std::to_string(thUnlock(k));
    case G_Count:
    {
        std::string s = "Limit reached: " + std::to_string(maxCount(k, th)) + " " + (maxCount(k, th) == 1 ? std::string(KINFO[k].name) : pluralName(k)) + " at Town Hall level " + std::to_string(th);
        for (int t = th + 1; t <= MAX_LEVEL; t++)
            if (maxCount(k, t) > maxCount(k, th))
                return s + " (level " + std::to_string(t) + " allows " + std::to_string(maxCount(k, t)) + ")";
        return s;
    }
    default:
        return "";
    }
}

// Why a building of kind k at `level` cannot be upgraded ("" = it can, cost aside).
inline std::string upgradeBlock(Kind k, int level, int th)
{
    if (k == K_Campfire)
        return "Campfires cannot be upgraded";
    if (level >= MAX_LEVEL)
        return "Already at the maximum level";
    if (level >= maxLevelFor(k, th))
        return "Requires Town Hall level " + std::to_string(level + 1);
    return "";
}

// costs (wood, gold, stone + a food share)
inline float foodShare(const float *c, float mul)
{
    return std::ceil((c[R_Wood] + c[R_Gold] + c[R_Stone]) * .2f * mul);
}

inline void buildCost(Kind k, float *out)
{
    for (int r = 0; r < R_Count; r++)
        out[r] = KINFO[k].cost[r];
    out[R_Meat] += foodShare(out, cfg().foodCostBuild);
}

inline void unitCost(Kind k, float *out)
{
    for (int r = 0; r < R_Count; r++)
        out[r] = KINFO[k].cost[r];
    out[R_Meat] = std::ceil(out[R_Meat] * cfg().foodCostTrain);
}

inline float upgradeCostMul(int level)
{
    return .7f * std::pow(cfg().upgradeCostGrowth, float(level - 1));
} // from `level` to level+1

inline void upgradeCost(Kind k, int level, float *out)
{
    for (int r = 0; r < R_Count; r++)
        out[r] = r == R_Meat ? 0.f : std::ceil(KINFO[k].cost[r] * upgradeCostMul(level));
    out[R_Meat] = std::ceil(KINFO[k].cost[R_Meat] * upgradeCostMul(level)) + foodShare(out, cfg().foodCostUpgrade);
}

inline void repairCost(Kind k, int level, float missing, float *out)
{
    float m = cfg().repairCostFraction * std::clamp(missing, 0.f, 1.f) * levelHp(level);
    for (int r = 0; r < R_Count; r++)
        out[r] = r == R_Meat ? 0.f : std::ceil(KINFO[k].cost[r] * m);
    out[R_Meat] = foodShare(out, cfg().foodCostRepair);
}

// times (seconds of pawn work at the site)
inline float buildSeconds(Kind k)
{
    return KINFO[k].buildTime * cfg().buildTimeScale;
}

inline float upgradeSeconds(Kind k, int level)
{
    return KINFO[k].buildTime * cfg().buildTimeScale * 1.5f * std::pow(cfg().upgradeTimeGrowth, float(level - 1));
}

inline float repairSeconds(Kind k, float missing)
{
    return std::max(3.f, KINFO[k].buildTime * cfg().buildTimeScale * cfg().repairTimeScale * std::clamp(missing, 0.f, 1.f));
}

// storage and production
inline float storageCap(Kind k, int level)
{
    float base = k == K_TownHall ? 3000.f : (k == K_Outpost ? 300.f : (k == K_Storage ? 800.f : 0.f));
    return std::round(base * std::pow(1.6f, float(std::clamp(level, 1, MAX_LEVEL) - 1)));
}

// Town Hall storage shares: each resource may fill at most `share` % of the hall's total capacity (C_SetAlloc).
inline constexpr int ALLOC_STEP = 5;

inline float allocCap(float hallCap, int sharePct)
{
    return std::floor(hallCap * float(std::clamp(sharePct, 0, 100)) / 100.f);
}

inline float productionMul(int level)
{
    return levelMul(level) * cfg().productionScale;
}

// damage efficiency
// A damaged building works at max(HP fraction, damage_min_efficiency); below damage_critical_hp it is disabled.
inline bool critical(float hpFrac)
{
    return hpFrac < cfg().damageCriticalHp;
}

inline float hpEfficiency(float hpFrac)
{
    return critical(hpFrac) ? 0.f : std::clamp(hpFrac, cfg().damageMinEfficiency, 1.f);
}

// site jobs (S_Jobs)
enum SiteKind : uint8_t
{
    SJ_Build,
    SJ_Upgrade,
    SJ_Repair
};

enum SitePhase : uint8_t
{
    SP_Waiting,
    SP_Walking,
    SP_Working
}; // waiting for a pawn / pawn on the way / pawn at work

// tunables sent in S_Welcome (same order on both sides)
template <class W>
void putRules(W &w)
{
    const ServerConfig &c = cfg();
    for (float v : {c.buildTimeScale, c.upgradeCostGrowth, c.upgradeTimeGrowth, c.productionScale, c.repairCostFraction, c.repairTimeScale,
                    c.damageMinEfficiency, c.damageCriticalHp, c.foodCostBuild, c.foodCostUpgrade, c.foodCostRepair, c.foodCostTrain,
                    c.donationCooldown, c.demandBonus})
        w.template put<float>(v);
}

template <class R>
void getRules(R &r)
{
    ServerConfig &c = cfg();
    for (float *v : {&c.buildTimeScale, &c.upgradeCostGrowth, &c.upgradeTimeGrowth, &c.productionScale, &c.repairCostFraction, &c.repairTimeScale,
                     &c.damageMinEfficiency, &c.damageCriticalHp, &c.foodCostBuild, &c.foodCostUpgrade, &c.foodCostRepair, &c.foodCostTrain,
                     &c.donationCooldown, &c.demandBonus})
    {
        float x = r.template get<float>();
        if (r.ok && std::isfinite(x))
            *v = x;
    }
}
