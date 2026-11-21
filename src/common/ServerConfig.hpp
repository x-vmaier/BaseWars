/**
 * @file ServerConfig.hpp
 * @brief Server tuning loaded from config/server.cfg (key = value, '#' comments).
 */
#pragma once
#include <string>

struct ServerConfig
{
    // ---- world generation
    float biomeScale = 1.f;       // >1 = larger biomes (noise frequencies divided by it)
    float oceanLevel = .43f;      // elevation below this is ocean: raise for more water
    float highlandsHeight = .80f; // elevation above this is highlands: lower for more mountains
    float snowCold = .27f;        // temperature below this is snow: raise for bigger snow regions
    float desertHeat = .62f;      // temperature above this (and dry) is desert: lower for bigger deserts
    float forestMoisture = .58f;  // moisture above this is forest: lower for bigger forests
    float flowerNoise = .54f;     // flowers noise threshold: lower for more flower meadows
    float islandGate = .68f;      // regional gate for open-ocean islands: raise = fewer
    // ---- obstacles (trees, rocks, gold) per biome, multiplier on the built-in density; 0 removes them
    float featureDensity[8] = {
        0, 1, 1, 1, 1, 1, 1, 0}; // indexed by Biome enum (Ocean, Plains, Forest, Highlands, Desert, Flowers, Snow, ...)
    float clearTimeScale = 1.f;  // multiplier on how long clearing an obstacle takes
    // ---- connection
    int snapshotInterval = 2;   // simulation ticks between S_State snapshots (TICK_HZ / n snapshots per second)
    double authTimeoutS = 15.0; // a hello whose Steam ticket verdict has not arrived by then is dropped
    // ---- defeat and resettlement
    double defeatRuinHours = 24.0;       // real hours a defeated town stays as a burning ruin (wall clock, persisted)
    double resettleOfferTimeoutS = 60.0; // an offered site unanswered for this long is accepted automatically
    // ---- NPC towns and factions
    int townRegionChance = 85;      // % of 8x8-chunk regions that contain an NPC town
    int townMinDistance = 45;       // minimum tiles between an NPC town and any player Town Hall
    int hostileFactionPercent = 25; // % of factions that start hostile to a newcomer
    int factionWarPercent = 30; // % of faction pairs at war with each other (their patrols attack each other's towns)
    // ---- Pacing Director (calm -> warning -> siege -> aftermath, per player; timers pause while offline)
    double pacingCalmMin = 7.0, pacingWarningS = 75.0, pacingAftermathMin = 3.0, pacingFirstRaidGraceMin = 4.0;
    float pacingMaxRaidFactor = 1.5f;   // adaptive strength factor cap (floor is .5)
    int pacingMaxRaiders = 14;          // hard cap on one raid's size
    float pacingMaxBaseShare = .5f;     // a raid has at most this share of (buildings + troops) raiders
    float pacingRewardPerRaider = 15.f; // survivor reward: this many of each resource per raider
    // ---- objectives
    double objectiveTimeoutMin = 20.0; // online minutes after which a stuck objective resolves without reward
    // ---- patrols
    int patrolsPerTownMin = 1, patrolsPerTownMax = 2; // patrol groups each NPC town maintains
    int patrolMapGroups = 2;     // extra wandering groups per 100 chunks of explored land (map-wide)
    int patrolSiegePercent = 30; // % of new patrol waypoints that march on a hostile town in range
    int patrolSiegeRange = 140;  // tiles: how far a patrol looks for a hostile town to attack
    double patrolRespawnMin = 90.0, patrolRespawnRandom = 90.0;
    // ---- wind and ships
    float windStrength = .3f;       // ships sail x(1 + this) with the wind and x(1 - this) against it
    float windShiftSeconds = 180.f; // average time until the wind picks a new direction
    float windTurnRate = .03f;      // rad/s the wind turns towards its new direction
    float shipRange = 7.f;          // tiles: reach of a ship's archers / crossbow
    int shipCrossbowLevel = 2;      // ship upgrade level from which a crossbow is mounted (counts as one archer)
    // ---- sheep
    float sheepDensityPlains = 1.f, sheepDensityFlowers = 1.f; // multiplier on initial and repopulated sheep (0 = none)
    int sheepMax = 900;
    // ---- progression (shared with clients through S_Welcome so their cost / time previews match)
    float buildTimeScale = 2.f;     // multiplier on every building's base construction time
    float upgradeCostGrowth = 1.9f; // upgrade cost from level L: base cost x .7 x growth^(L-1)
    float upgradeTimeGrowth = 1.6f; // upgrade time from level L: build time x 1.5 x growth^(L-1)
    float productionScale = .6f;    // multiplier on resource building output
    float repairCostFraction = .5f; // repair cost = build cost x this x missing HP fraction (x level HP factor)
    float repairTimeScale = .5f;    // repair time = build time x this x missing HP fraction
    // ---- damaged buildings
    float damageMinEfficiency = .4f; // a damaged building works at max(HP fraction, this)
    float damageCriticalHp = .25f;   // below this HP fraction a building is disabled until repaired
    // ---- food (meat) on actions: multipliers on the food share of each cost
    float foodCostBuild = 1.f, foodCostUpgrade = 1.f, foodCostRepair = 1.f, foodCostTrain = 1.f;
    // ---- donations and NPC town demand
    float donationCooldown = 60.f; // seconds between two donations / clan transfers of one player
    float demandChangeMin = 600.f, demandChangeMax = 1200.f; // seconds until a faction wants a different resource
    float demandBonus = 2.f;                                 // reputation multiplier for gifts of the wanted resource
    float demandSatisfy = 400.f;    // gifts of the wanted resource that satisfy a faction (its demand then moves on)
    float donationDiminish = 150.f; // recent gifts of the same resource: reputation x 1 / (1 + recent / this)
    float donationMemory = 900.f;   // seconds for the "recent gifts" memory to fade (time constant)
    // ---- debug timers: complete things instantly for fast testing
    bool instantBuild = false, instantUpgrade = false, instantTrain = false,
         instantClear = false; // instant build / upgrade (and repair) also skip the pawn walk

    static ServerConfig &get();         // process-wide instance
    bool load(const std::string &path); // false when the file is missing (defaults stay)
};
inline ServerConfig &cfg()
{
    return ServerConfig::get();
}
