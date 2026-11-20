/**
 * @file ServerConfig.cpp
 * @brief Server tuning loaded from config/server.cfg (key = value, '#' comments).
 */
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

// One parsed "key = value" line; each setter assigns only when the key matches.
struct Entry
{
    std::string k;
    double d;
    bool b;

    void F(const char *n, float &dst) const
    {
        if (k == n)
            dst = float(d);
    }
    void D(const char *n, double &dst) const
    {
        if (k == n)
            dst = d;
    }
    void I(const char *n, int &dst) const
    {
        if (k == n)
            dst = int(d);
    }
    void B(const char *n, bool &dst) const
    {
        if (k == n)
            dst = b;
    }
};

void applyWorldGen(ServerConfig &c, const Entry &e)
{
    // Index order must match the Biome enum in Shared.hpp (Ocean=0 ... Snow=6).
    static const char *biomeKey[7] = {"", "plains", "forest", "highlands", "desert", "flowers", "snow"};

    e.F("biome_scale", c.biomeScale);
    e.F("ocean_level", c.oceanLevel);
    e.F("highlands_height", c.highlandsHeight);
    e.F("snow_cold", c.snowCold);
    e.F("desert_heat", c.desertHeat);
    e.F("forest_moisture", c.forestMoisture);
    e.F("flower_noise", c.flowerNoise);
    e.F("island_gate", c.islandGate);
    for (int i = 1; i < 7; i++)
        if (e.k == std::string("feature_density_") + biomeKey[i])
            c.featureDensity[i] = float(e.d);
    e.F("clear_time_scale", c.clearTimeScale);
}

void applyNpcs(ServerConfig &c, const Entry &e)
{
    e.D("auth_timeout_s", c.authTimeoutS);
    e.I("town_region_chance", c.townRegionChance);
    e.I("town_min_distance", c.townMinDistance);
    e.I("hostile_faction_percent", c.hostileFactionPercent);
    e.I("faction_war_percent", c.factionWarPercent);
    e.I("patrol_siege_percent", c.patrolSiegePercent);
    e.I("patrol_siege_range", c.patrolSiegeRange);
    e.D("raid_first_delay", c.raidFirstDelay);
    e.D("raid_first_random", c.raidFirstRandom);
    e.D("raid_interval", c.raidInterval);
    e.D("raid_interval_random", c.raidIntervalRandom);
    e.I("patrols_per_town_min", c.patrolsPerTownMin);
    e.I("patrols_per_town_max", c.patrolsPerTownMax);
    e.I("patrol_map_groups", c.patrolMapGroups);
    e.D("patrol_respawn_min", c.patrolRespawnMin);
    e.D("patrol_respawn_random", c.patrolRespawnRandom);
    e.F("sheep_density_plains", c.sheepDensityPlains);
    e.F("sheep_density_flowers", c.sheepDensityFlowers);
    e.I("sheep_max", c.sheepMax);
}

void applyEconomy(ServerConfig &c, const Entry &e)
{
    e.F("build_time_scale", c.buildTimeScale);
    e.F("upgrade_cost_growth", c.upgradeCostGrowth);
    e.F("upgrade_time_growth", c.upgradeTimeGrowth);
    e.F("production_scale", c.productionScale);
    e.F("repair_cost_fraction", c.repairCostFraction);
    e.F("repair_time_scale", c.repairTimeScale);
    e.F("damage_min_efficiency", c.damageMinEfficiency);
    e.F("damage_critical_hp", c.damageCriticalHp);
    e.F("food_cost_build", c.foodCostBuild);
    e.F("food_cost_upgrade", c.foodCostUpgrade);
    e.F("food_cost_repair", c.foodCostRepair);
    e.F("food_cost_train", c.foodCostTrain);
    e.F("donation_cooldown", c.donationCooldown);
    e.F("demand_change_min", c.demandChangeMin);
    e.F("demand_change_max", c.demandChangeMax);
    e.F("demand_bonus", c.demandBonus);
    e.F("demand_satisfy", c.demandSatisfy);
    e.F("donation_diminish", c.donationDiminish);
    e.F("donation_memory", c.donationMemory);
}

void applyDebug(ServerConfig &c, const Entry &e)
{
    e.B("debug_instant_build", c.instantBuild);
    e.B("debug_instant_upgrade", c.instantUpgrade);
    e.B("debug_instant_train", c.instantTrain);
    e.B("debug_instant_clear", c.instantClear);
}

void clampAll(ServerConfig &c)
{
    c.biomeScale = std::max(.1f, c.biomeScale);
    c.townRegionChance = std::clamp(c.townRegionChance, 0, 100);
    c.hostileFactionPercent = std::clamp(c.hostileFactionPercent, 0, 100);
    c.factionWarPercent = std::clamp(c.factionWarPercent, 0, 100);
    c.patrolSiegePercent = std::clamp(c.patrolSiegePercent, 0, 100);
    c.patrolMapGroups = std::max(0, c.patrolMapGroups);
    c.patrolsPerTownMin = std::max(0, c.patrolsPerTownMin);
    c.patrolsPerTownMax = std::max(c.patrolsPerTownMin, c.patrolsPerTownMax);
    c.buildTimeScale = std::max(.01f, c.buildTimeScale);
    c.upgradeCostGrowth = std::max(1.f, c.upgradeCostGrowth);
    c.upgradeTimeGrowth = std::max(1.f, c.upgradeTimeGrowth);
    c.productionScale = std::max(0.f, c.productionScale);
    c.damageCriticalHp = std::clamp(c.damageCriticalHp, 0.f, .95f);
    c.damageMinEfficiency = std::clamp(c.damageMinEfficiency, 0.f, 1.f);
    c.donationCooldown = std::max(0.f, c.donationCooldown);
    c.demandChangeMin = std::max(30.f, c.demandChangeMin);
    c.demandChangeMax = std::max(c.demandChangeMin, c.demandChangeMax);
    c.demandSatisfy = std::max(1.f, c.demandSatisfy);
    c.donationDiminish = std::max(1.f, c.donationDiminish);
    c.donationMemory = std::max(1.f, c.donationMemory);
}
} // namespace

bool ServerConfig::load(const std::string &path)
{
    std::ifstream f(path);
    if (!f)
        return false;

    std::string line;
    while (std::getline(f, line))
    {
        if (auto h = line.find('#'); h != std::string::npos)
            line.erase(h);

        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        bool b = v == "1" || v == "true" || v == "yes" || v == "on";
        Entry e{k, std::atof(v.c_str()), b};

        applyWorldGen(*this, e);
        applyNpcs(*this, e);
        applyEconomy(*this, e);
        applyDebug(*this, e);
    }
    clampAll(*this);

    std::printf("Loaded server config %s\n", path.c_str());
    return true;
}
