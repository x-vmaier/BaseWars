#include "common/ServerConfig.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

ServerConfig &ServerConfig::get()
{
    static ServerConfig c;
    return c;
}

namespace
{
std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
} // namespace

bool ServerConfig::load(const std::string &path)
{
    std::ifstream f(path);
    if (!f)
        return false;

    // biome index order must match the Biome enum in Shared.hpp (Ocean=0 ... Snow=6)
    static const char *biomeKey[7] = {"", "plains", "forest", "highlands", "desert", "flowers", "snow"};

    std::string line;
    while (std::getline(f, line))
    {
        if (auto h = line.find('#'); h != std::string::npos)
            line.erase(h);

        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        double d = std::atof(v.c_str());
        bool b = v == "1" || v == "true" || v == "yes" || v == "on";

        auto F = [&](const char *n, float &dst)
        {
            if (k == n)
                dst = float(d);
        };
        auto D = [&](const char *n, double &dst)
        {
            if (k == n)
                dst = d;
        };
        auto I = [&](const char *n, int &dst)
        {
            if (k == n)
                dst = int(d);
        };
        auto B = [&](const char *n, bool &dst)
        {
            if (k == n)
                dst = b;
        };

        F("biome_scale", biomeScale);
        F("ocean_level", oceanLevel);
        F("highlands_height", highlandsHeight);
        F("snow_cold", snowCold);
        F("desert_heat", desertHeat);
        F("forest_moisture", forestMoisture);
        F("flower_noise", flowerNoise);
        F("island_gate", islandGate);
        for (int i = 1; i < 7; i++)
            if (k == std::string("feature_density_") + biomeKey[i])
                featureDensity[i] = float(d);

        F("clear_time_scale", clearTimeScale);
        I("town_region_chance", townRegionChance);
        I("town_min_distance", townMinDistance);
        I("hostile_faction_percent", hostileFactionPercent);
        I("faction_war_percent", factionWarPercent);
        I("patrol_siege_percent", patrolSiegePercent);
        I("patrol_siege_range", patrolSiegeRange);
        D("raid_first_delay", raidFirstDelay);
        D("raid_first_random", raidFirstRandom);
        D("raid_interval", raidInterval);
        D("raid_interval_random", raidIntervalRandom);
        I("patrols_per_town_min", patrolsPerTownMin);
        I("patrols_per_town_max", patrolsPerTownMax);
        I("patrol_map_groups", patrolMapGroups);
        D("patrol_respawn_min", patrolRespawnMin);
        D("patrol_respawn_random", patrolRespawnRandom);
        F("sheep_density_plains", sheepDensityPlains);
        F("sheep_density_flowers", sheepDensityFlowers);
        I("sheep_max", sheepMax);
        F("build_time_scale", buildTimeScale);
        F("upgrade_cost_growth", upgradeCostGrowth);
        F("upgrade_time_growth", upgradeTimeGrowth);
        F("production_scale", productionScale);
        F("repair_cost_fraction", repairCostFraction);
        F("repair_time_scale", repairTimeScale);
        F("damage_min_efficiency", damageMinEfficiency);
        F("damage_critical_hp", damageCriticalHp);
        F("food_cost_build", foodCostBuild);
        F("food_cost_upgrade", foodCostUpgrade);
        F("food_cost_repair", foodCostRepair);
        F("food_cost_train", foodCostTrain);
        F("donation_cooldown", donationCooldown);
        F("demand_change_min", demandChangeMin);
        F("demand_change_max", demandChangeMax);
        F("demand_bonus", demandBonus);
        F("demand_satisfy", demandSatisfy);
        F("donation_diminish", donationDiminish);
        F("donation_memory", donationMemory);
        B("debug_instant_build", instantBuild);
        B("debug_instant_upgrade", instantUpgrade);
        B("debug_instant_train", instantTrain);
        B("debug_instant_clear", instantClear);
    }

    biomeScale = std::max(.1f, biomeScale);
    townRegionChance = std::clamp(townRegionChance, 0, 100);
    hostileFactionPercent = std::clamp(hostileFactionPercent, 0, 100);
    factionWarPercent = std::clamp(factionWarPercent, 0, 100);
    patrolSiegePercent = std::clamp(patrolSiegePercent, 0, 100);
    patrolMapGroups = std::max(0, patrolMapGroups);
    patrolsPerTownMin = std::max(0, patrolsPerTownMin);
    patrolsPerTownMax = std::max(patrolsPerTownMin, patrolsPerTownMax);
    buildTimeScale = std::max(.01f, buildTimeScale);
    upgradeCostGrowth = std::max(1.f, upgradeCostGrowth);
    upgradeTimeGrowth = std::max(1.f, upgradeTimeGrowth);
    productionScale = std::max(0.f, productionScale);
    damageCriticalHp = std::clamp(damageCriticalHp, 0.f, .95f);
    damageMinEfficiency = std::clamp(damageMinEfficiency, 0.f, 1.f);
    donationCooldown = std::max(0.f, donationCooldown);
    demandChangeMin = std::max(30.f, demandChangeMin);
    demandChangeMax = std::max(demandChangeMin, demandChangeMax);
    demandSatisfy = std::max(1.f, demandSatisfy);
    donationDiminish = std::max(1.f, donationDiminish);
    donationMemory = std::max(1.f, donationMemory);

    std::printf("Loaded server config %s\n", path.c_str());
    return true;
}
