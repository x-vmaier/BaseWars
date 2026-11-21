/**
 * @file KindDefs.hpp
 * @brief The kind registry: one KindDef per building / unit / NPC type (stats, tags, gates, training, texts).
 */
#pragma once
#include <array>
#include <cstdint>

inline constexpr int MAX_LEVEL = 10;

enum Res : uint8_t
{
    R_Wood,
    R_Gold,
    R_Stone,
    R_Meat,
    R_Count
};

// The wire / save id of a type: ids are persisted and sent, so never reorder, only append before K_Count. Each id needs
// one entry in KINDS below, in the same order; KindInfo::building says which family an id belongs to.
enum Kind : uint8_t
{
    K_TownHall,
    K_Outpost,
    K_Harbour,
    K_Storage,
    K_Mine,
    K_Lumber,
    K_Campfire,
    K_Archery,
    K_Pawn,
    K_Warrior,
    K_Archer,
    K_Lancer,
    K_Monk,
    K_Ship,
    K_Sheep,
    K_Ruin,
    K_Longship,
    K_Cog,
    K_Galleon,
    K_Count
};

// Behaviour flags. Systems and UI ask for a tag instead of comparing against a kind.
enum KindTag : uint32_t
{
    KT_Storage = 1u << 0,       // holds resources (Town Hall, Outpost, Storage House)
    KT_Depot = 1u << 1,         // a storage that is not the Town Hall: donation / haul target
    KT_Producer = 1u << 2,      // yields resources over time (mine, lumber camp, harbour)
    KT_Trainer = 1u << 3,       // has a training window besides production
    KT_Army = 1u << 4,          // fights for a base (warrior, archer, lancer)
    KT_Troop = 1u << 5,         // trainable and upgradable unit (pawn included)
    KT_Anchor = 1u << 6,        // claims territory (Town Hall, Outpost)
    KT_Warm = 1u << 7,          // warms nearby units
    KT_SingleLevel = 1u << 8,   // cannot be upgraded or relocated
    KT_Naval = 1u << 9,         // moves on water
    KT_Animal = 1u << 10,       // neutral huntable wildlife
    KT_Fireproof = 1u << 11,    // never catches fire
    KT_Coastal = 1u << 12,      // produces from the sea (needs adjacent ocean)
    KT_GoldBoosted = 1u << 13,  // output grows with nearby gold deposits
    KT_Freestanding = 1u << 14, // needs no territory: never disabled, never relocated
    KT_NonCombat = 1u << 15,    // ignored by "select all combat units" and combat-only orders
    KT_Firefighter = 1u << 16,  // idle units of this kind put out fires
    KT_Worker = 1u << 17,       // builds, clears obstacles, hauls, hunts (pawn)
    KT_Healer = 1u << 18,       // heals allies instead of fighting (dps is healing per second)
    KT_Ruin = 1u << 19,         // burning remains of a defeated town (neutral, indestructible, expires)
};

struct KindInfo
{
    const char *name;
    bool building;
    int fw, fh;          // footprint in tiles
    float cost[R_Count]; // wood, gold, stone, meat
    float hp, speed, dps, range, buildTime;
    int vision; // reveal radius in tiles
};

// Town Hall gates (the one table): Town Hall level needed to build the kind, whether its level is capped by the Town
// Hall level, and the number allowed at each Town Hall level 1..MAX_LEVEL (0 = not yet).
struct BuildGate
{
    int thUnlock = 1;
    bool capByTh = false;
    int count[MAX_LEVEL]{};
};

// Ships only: hold sizes at level 1 (upgrades scale them, see shipTroopCap) and the crew needed to sail.
struct ShipDef
{
    uint8_t look = 0;   // hull drawn: 1 small boat .. 3 galleon
    uint8_t troops = 0; // troop slots (the crew counts as troops aboard)
    uint8_t crew = 1;   // troops aboard needed before the ship accepts a move order
    uint16_t cargo = 0; // resource units
};

struct KindDef
{
    KindInfo stats;
    uint32_t tags = 0;
    BuildGate gate{};           // buildings only
    uint8_t menuSlot = 0;       // 1.. = position in the build menu (hotkey), 0 = not listed
    const char *verb = "Train"; // context-panel action label ("Build" for ships)
    const char *siteNote = "";  // producer hint ("Best in: Forest")

    // Units only: where and how they are produced.
    uint32_t trainedAt = 0;   // bit mask of building kinds (1u << K_Archery)
    uint8_t archeryLevel = 1; // level of the Archery needed
    uint8_t thLevel = 1;      // Town Hall level needed
    float trainSeconds = 8.f;
    const char *role = ""; // short description in the training panel
    const char *blurb = "";
    ShipDef ship{}; // KT_Naval kinds only
};

constexpr uint32_t kindBit(Kind k)
{
    return 1u << k;
}

inline constexpr KindDef KINDS[K_Count] = {
    {.stats = {"Town Hall", true, 4, 3, {250, 200, 150, 0}, 800, 0, 14, 6.5f, 40, 9},
     .tags = KT_Storage | KT_Anchor | KT_Freestanding,
     .gate = {1, false, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1}}},
    {.stats = {"Outpost", true, 2, 2, {80, 40, 60, 0}, 350, 0, 8, 7.f, 20, 8},
     .tags = KT_Storage | KT_Depot | KT_Anchor,
     .gate = {3, true, {0, 0, 1, 1, 2, 2, 3, 3, 4, 4}},
     .menuSlot = 1},
    {.stats = {"Harbour", true, 2, 2, {120, 60, 40, 0}, 300, 0, 0, 0, 25, 7},
     .tags = KT_Producer | KT_Coastal,
     .gate = {4, true, {0, 0, 0, 1, 1, 1, 2, 2, 2, 3}},
     .menuSlot = 2,
     .siteNote = "Coastal fishing"},
    {.stats = {"Storage House", true, 2, 2, {100, 20, 60, 0}, 400, 0, 0, 0, 25, 5},
     .tags = KT_Storage | KT_Depot,
     .gate = {2, true, {0, 1, 1, 2, 2, 2, 3, 3, 3, 4}},
     .menuSlot = 3},
    {.stats = {"Mine", true, 2, 2, {60, 20, 0, 0}, 250, 0, 0, 0, 18, 4},
     .tags = KT_Producer | KT_GoldBoosted,
     .gate = {1, true, {1, 2, 2, 3, 3, 4, 4, 5, 5, 6}},
     .menuSlot = 5,
     .siteNote = "Best in: Highlands, Desert"},
    {.stats = {"Lumber Camp", true, 2, 2, {40, 10, 0, 0}, 250, 0, 0, 0, 15, 4},
     .tags = KT_Producer,
     .gate = {1, true, {1, 2, 2, 3, 3, 4, 4, 5, 5, 6}},
     .menuSlot = 6,
     .siteNote = "Best in: Forest"},
    {.stats = {"Campfire", true, 1, 1, {20, 0, 0, 0}, 60, 0, 0, 0, 5, 4},
     .tags = KT_Warm | KT_SingleLevel | KT_Fireproof | KT_Freestanding,
     .gate = {1, true, {2, 3, 4, 5, 6, 7, 8, 9, 10, 12}},
     .menuSlot = 7,
     .siteNote = "Warms units in the snow"},
    {.stats = {"Archery", true, 3, 2, {120, 60, 80, 0}, 450, 0, 0, 0, 30, 5},
     .tags = KT_Trainer,
     .gate = {1, true, {1, 1, 1, 1, 2, 2, 2, 2, 3, 3}},
     .menuSlot = 4},

    {.stats = {"Pawn", false, 1, 1, {0, 0, 0, 30}, 40, 2.6f, 3, 1.2f, 0, 5},
     .tags = KT_Troop | KT_Firefighter | KT_Worker,
     .trainedAt = kindBit(K_TownHall) | kindBit(K_Archery),
     .trainSeconds = 6.f,
     .role = "Worker",
     .blurb =
         "Carries farmed resources to the nearest storage and hauls stock between outposts, storage houses and the "
         "Town Hall. Weak in a fight, and hauling parties can be raided."},
    {.stats = {"Warrior", false, 1, 1, {10, 20, 0, 40}, 120, 2.6f, 12, 1.4f, 0, 6},
     .tags = KT_Troop | KT_Army | KT_Firefighter,
     .trainedAt = kindBit(K_Archery),
     .trainSeconds = 9.f,
     .role = "Frontline infantry",
     .blurb = "Sturdy sword and shield fighter. Takes 20% less damage from every attack. The backbone of any army "
              "against both units and buildings."},
    {.stats = {"Archer", false, 1, 1, {30, 20, 0, 35}, 60, 2.8f, 9, 6.f, 0, 7},
     .tags = KT_Troop | KT_Army | KT_Firefighter,
     .trainedAt = kindBit(K_Archery),
     .archeryLevel = 2,
     .thLevel = 2,
     .trainSeconds = 10.f,
     .role = "Ranged support",
     .blurb = "Shoots arrows from a distance. Deals only 70% damage to buildings and dies quickly in melee, so keep "
              "them behind your warriors."},
    {.stats = {"Lancer", false, 1, 1, {20, 40, 20, 50}, 100, 3.4f, 15, 2.4f, 0, 6},
     .tags = KT_Troop | KT_Army | KT_Firefighter,
     .trainedAt = kindBit(K_Archery),
     .archeryLevel = 3,
     .thLevel = 4,
     .trainSeconds = 12.f,
     .role = "Fast spear cavalry",
     .blurb = "The fastest troop. Deals 30% extra damage to units but only 60% to buildings. Ideal for hunting "
              "convoys and raiders."},
    {.stats = {"Monk", false, 1, 1, {0, 50, 0, 30}, 50, 2.6f, 8, 4.f, 0, 6},
     .tags = KT_Troop | KT_NonCombat | KT_Firefighter | KT_Healer,
     .trainedAt = kindBit(K_Archery),
     .archeryLevel = 2,
     .thLevel = 3,
     .trainSeconds = 11.f,
     .role = "Healer",
     .blurb = "Automatically heals the most wounded friendly unit nearby. Cannot attack, so escort them. Higher levels "
              "heal faster."},
    {.stats = {"Small Boat", false, 1, 1, {80, 30, 0, 20}, 220, 4.2f, 0, 0, 0, 10},
     .tags = KT_Naval | KT_NonCombat,
     .verb = "Build",
     .trainedAt = kindBit(K_Harbour),
     .thLevel = 4,
     .trainSeconds = 18.f,
     .role = "Light transport",
     .blurb = "Cheap and fast. Needs one troop aboard to sail.",
     .ship = {1, 6, 1, 400}},
    {.stats = {"Sheep", false, 1, 1, {0, 0, 0, 0}, 12, 0.9f, 0, 0, 0, 0}, .tags = KT_Animal},
    {.stats = {"Burning Ruin", true, 4, 3, {0, 0, 0, 0}, 1, 0, 0, 0, 0, 0},
     .tags = KT_Ruin | KT_Fireproof | KT_Freestanding | KT_SingleLevel | KT_NonCombat},
    {.stats = {"Longship", false, 1, 1, {140, 60, 20, 30}, 300, 4.8f, 0, 0, 0, 10},
     .tags = KT_Naval | KT_NonCombat,
     .verb = "Build",
     .trainedAt = kindBit(K_Harbour),
     .archeryLevel = 2,
     .thLevel = 5,
     .trainSeconds = 24.f,
     .role = "Troop carrier",
     .blurb = "Fast raiding ship with a big troop hold. Needs two troops aboard to sail.",
     .ship = {2, 14, 2, 300}},
    {.stats = {"Cargo Cog", false, 1, 1, {160, 40, 30, 25}, 280, 3.6f, 0, 0, 0, 10},
     .tags = KT_Naval | KT_NonCombat,
     .verb = "Build",
     .trainedAt = kindBit(K_Harbour),
     .archeryLevel = 2,
     .thLevel = 5,
     .trainSeconds = 26.f,
     .role = "Cargo hauler",
     .blurb = "Slow, with a huge hold for resources. Needs two troops aboard to sail.",
     .ship = {2, 6, 2, 1800}},
    {.stats = {"Galleon", false, 1, 1, {260, 120, 60, 50}, 520, 4.f, 0, 0, 0, 11},
     .tags = KT_Naval | KT_NonCombat,
     .verb = "Build",
     .trainedAt = kindBit(K_Harbour),
     .archeryLevel = 3,
     .thLevel = 6,
     .trainSeconds = 40.f,
     .role = "Flagship",
     .blurb = "The largest ship: big hold for troops and cargo. Needs three troops aboard to sail.",
     .ship = {3, 24, 3, 1400}},
};

// Build menu order (hotkeys 1..): the kinds with a menuSlot, by slot.
constexpr int buildMenuCount()
{
    int n = 0;
    for (int k = 0; k < K_Count; k++)
        n += KINDS[k].menuSlot ? 1 : 0;
    return n;
}
constexpr std::array<Kind, buildMenuCount()> buildMenuKinds()
{
    std::array<Kind, buildMenuCount()> out{};
    for (int slot = 1; slot <= buildMenuCount(); slot++)
        for (int k = 0; k < K_Count; k++)
            if (KINDS[k].menuSlot == slot)
                out[std::size_t(slot - 1)] = Kind(k);
    return out;
}

// Every id has an entry, in enum order.
constexpr bool kindsConsistent()
{
    for (int k = 0; k < K_Count; k++)
        if (!KINDS[k].stats.name)
            return false;
    return true;
}
static_assert(kindsConsistent(), "KINDS needs one entry per Kind");

// Compatibility accessor: KINFO[kind] is the stats part of the registry entry.
struct KindInfoTable
{
    constexpr const KindInfo &operator[](int k) const { return KINDS[k].stats; }
};
inline constexpr KindInfoTable KINFO{};

constexpr bool hasTag(Kind k, KindTag t)
{
    return (KINDS[k].tags & t) != 0;
}
inline bool isBuilding(Kind k)
{
    return KINFO[k].building;
}
inline bool isResourceBuilding(Kind k)
{
    return hasTag(k, KT_Producer);
}
inline bool isStorage(Kind k)
{
    return hasTag(k, KT_Storage);
}
inline bool isArmy(Kind k)
{
    return hasTag(k, KT_Army);
}
inline bool isTroop(Kind k)
{
    return hasTag(k, KT_Troop);
}

// Troops are trained in the Archery, pawns also in the Town Hall; the Harbour builds ships.
inline bool canTrain(Kind building, Kind unit)
{
    return (KINDS[unit].trainedAt & kindBit(building)) != 0;
}
inline int unitUnlockLevel(Kind u)
{
    return KINDS[u].archeryLevel;
} // level of the training building needed
