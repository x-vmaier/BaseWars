#pragma once
// Server tuning loaded from config/server.cfg (key = value, '#' comments). Every key has a default that reproduces
// the previous hard-coded behaviour, so a missing file or key changes nothing. Only the server reads it; the client
// receives the generated world through the protocol.
#include <string>

struct ServerConfig {
    // ---- world generation
    float biomeScale = 1.f;            // >1 = larger biomes (noise frequencies divided by it)
    float oceanLevel = .43f;           // elevation below this is ocean: raise for more water
    float highlandsHeight = .80f;      // elevation above this is highlands: lower for more mountains
    float snowCold = .27f;             // temperature below this is snow: raise for bigger snow regions
    float desertHeat = .62f;           // temperature above this (and dry) is desert: lower for bigger deserts
    float forestMoisture = .58f;       // moisture above this is forest: lower for bigger forests
    float flowerNoise = .54f;          // flowers noise threshold: lower for more flower meadows
    float islandGate = .68f;           // regional gate for open-ocean islands: raise = fewer
    // ---- obstacles (trees, rocks, gold) per biome, multiplier on the built-in density; 0 removes them
    float featureDensity[8] = {0, 1, 1, 1, 1, 1, 1, 0};     // indexed by Biome enum (Ocean, Plains, Forest, Highlands, Desert, Flowers, Snow, ...)
    float clearTimeScale = 1.f;        // multiplier on how long clearing an obstacle takes
    // ---- NPC towns and factions
    int townRegionChance = 85;         // % of 8x8-chunk regions that contain an NPC town
    int townMinDistance = 45;          // minimum tiles between an NPC town and any player Town Hall
    int hostileFactionPercent = 25;    // % of factions that start hostile to a newcomer
    int factionWarPercent = 30;        // % of faction pairs at war with each other (their patrols attack each other's towns)
    // ---- raids
    double raidFirstDelay = 180.0, raidFirstRandom = 120.0;     // seconds until a hostile faction's first raid on a player
    double raidInterval = 360.0, raidIntervalRandom = 420.0;    // seconds between later raids
    // ---- patrols
    int patrolsPerTownMin = 1, patrolsPerTownMax = 2;           // patrol groups each NPC town maintains
    int patrolMapGroups = 2;                                    // extra wandering groups per 100 chunks of explored land (map-wide)
    int patrolSiegePercent = 30;                                // % of new patrol waypoints that march on a hostile town in range
    int patrolSiegeRange = 140;                                 // tiles: how far a patrol looks for a hostile town to attack
    double patrolRespawnMin = 90.0, patrolRespawnRandom = 90.0;
    // ---- sheep
    float sheepDensityPlains = 1.f, sheepDensityFlowers = 1.f;  // multiplier on initial and repopulated sheep (0 = none)
    int sheepMax = 900;
    // ---- debug timers: complete things instantly for fast testing
    bool instantBuild = false, instantUpgrade = false, instantTrain = false, instantClear = false;

    static ServerConfig& get();                                 // process-wide instance
    bool load(const std::string& path);                         // false when the file is missing (defaults stay)
};
inline ServerConfig& cfg() { return ServerConfig::get(); }
