/**
 * @file Objectives.hpp
 * @brief Objective table (short / medium / long goal chains), per-player objective state and the event bus that systems
 * post progress to. Shared by server, client and selftests.
 */
#pragma once
#include "common/KindDefs.hpp"
#include "common/Shared.hpp"

#include <cstdint>
#include <entt/entt.hpp>
#include <vector>

enum ObjTier : uint8_t
{
    OT_Short, // first-hour steps, each under ~2 minutes
    OT_Medium,
    OT_Long,
    OT_Count
};

// What an objective counts. Gather / Train / Donate use lifetime counters (so earlier play counts); Build, Level and
// Survive read the live state. Either way a step the player already satisfied resolves at once.
enum ObjCond : uint8_t
{
    OC_Gather,    // arg = resource
    OC_Build,     // arg = building kind (completed, currently standing)
    OC_Train,     // arg = unit kind, -1 = any army unit
    OC_Survive,   // raids survived
    OC_HallLevel, // Town Hall level reached
    OC_Donate     // donations / transfers sent
};

enum ObjId : uint8_t
{
    OBJ_Lumber,
    OBJ_Wood,
    OBJ_Mine,
    OBJ_Storage,
    OBJ_Pawn,
    OBJ_Campfire,
    OBJ_Hall2,
    OBJ_Archery,
    OBJ_Army,
    OBJ_Raid1,
    OBJ_Donate,
    OBJ_Harbour,
    OBJ_Raid3,
    OBJ_Hall5,
    OBJ_Hall10,
    OBJ_Count,
    OBJ_None = 255
};

struct ObjDef
{
    ObjTier tier;
    ObjCond cond;
    int arg;
    int count;
    const char *title; // short imperative shown in the tracker
    const char *hint;  // one line of advice under it
    float reward[R_Count];
    int pointKind; // building kind the UI highlights (-1 none)
    uint8_t next;  // next objective of the chain (OBJ_None = chain ends)
};

// The one table. Order = ObjId. Chains: short Lumber..Army (a step's building must be unlocked by the steps before it),
// medium Raid1..Raid3, long Hall5..Hall10.
inline const ObjDef OBJECTIVES[OBJ_Count] = {
    {OT_Short,
     OC_Build,
     K_Lumber,
     1,
     "Build a Lumber Camp",
     "Open the build menu (B) and place it near trees.",
     {0, 0, 0, 30},
     K_Lumber,
     OBJ_Wood},
    {OT_Short,
     OC_Gather,
     R_Wood,
     40,
     "Gather 40 wood",
     "Your Lumber Camp and free pawns bring wood to a storage.",
     {40, 0, 0, 0},
     -1,
     OBJ_Mine},
    {OT_Short,
     OC_Build,
     K_Mine,
     1,
     "Build a Mine",
     "Gold and stone come from mines; best on rocky ground.",
     {0, 25, 25, 0},
     K_Mine,
     OBJ_Pawn},
    {OT_Short,
     OC_Build,
     K_Storage,
     1,
     "Build a Storage House",
     "More storage means fewer wasted deliveries.",
     {40, 0, 0, 0},
     K_Storage,
     OBJ_Archery},
    {OT_Short,
     OC_Train,
     K_Pawn,
     1,
     "Train a Pawn",
     "Select the Town Hall and train a worker.",
     {0, 0, 0, 50},
     K_TownHall,
     OBJ_Campfire},
    {OT_Short,
     OC_Build,
     K_Campfire,
     1,
     "Build a Campfire",
     "It keeps units warm through the cold.",
     {30, 0, 0, 0},
     K_Campfire,
     OBJ_Hall2},
    {OT_Short,
     OC_HallLevel,
     0,
     2,
     "Upgrade the Town Hall to level 2",
     "Select it and press Upgrade: it unlocks more.",
     {60, 40, 40, 60},
     K_TownHall,
     OBJ_Storage},
    {OT_Short,
     OC_Build,
     K_Archery,
     1,
     "Build an Archery",
     "Soldiers are trained here.",
     {0, 30, 30, 0},
     K_Archery,
     OBJ_Army},
    {OT_Short,
     OC_Train,
     -1,
     1,
     "Train a soldier",
     "Select the Archery and queue a fighter before the first raid.",
     {0, 0, 0, 60},
     K_Archery,
     OBJ_None},
    {OT_Medium,
     OC_Survive,
     0,
     1,
     "Survive your first raid",
     "War horns warn you first: gather your troops.",
     {50, 50, 50, 50},
     -1,
     OBJ_Donate},
    {OT_Medium,
     OC_Donate,
     0,
     1,
     "Donate to a faction",
     "Gifts earn reputation and keep the peace.",
     {0, 40, 0, 40},
     -1,
     OBJ_Harbour},
    {OT_Medium,
     OC_Build,
     K_Harbour,
     1,
     "Build a Harbour",
     "Needs a coast: it fishes and builds ships.",
     {0, 60, 60, 60},
     K_Harbour,
     OBJ_Raid3},
    {OT_Medium,
     OC_Survive,
     0,
     3,
     "Survive 3 raids",
     "Walls of troops beat walls of wood.",
     {100, 100, 100, 100},
     -1,
     OBJ_None},
    {OT_Long,
     OC_HallLevel,
     0,
     5,
     "Town Hall level 5",
     "A bigger hall unlocks every building.",
     {150, 150, 150, 150},
     K_TownHall,
     OBJ_Hall10},
    {OT_Long,
     OC_HallLevel,
     0,
     MAX_LEVEL,
     "Town Hall level 10",
     "The capital of your realm.",
     {400, 400, 400, 400},
     K_TownHall,
     OBJ_None},
};
inline constexpr uint8_t OBJ_FIRST[OT_Count] = {OBJ_Lumber, OBJ_Raid1, OBJ_Hall5};

// Per-player objective state (saved). Lifetime counters feed the Gather / Train / Donate objectives.
struct ObjState
{
    uint8_t cur[OT_Count] = {OBJ_FIRST[0], OBJ_FIRST[1], OBJ_FIRST[2]}; // active objective per tier (OBJ_None = done)
    float active[OT_Count]{};     // online seconds the current objective has been shown (timeout)
    uint16_t completed = 0;       // objectives finished (rewarded or skipped)
    uint32_t gathered[R_Count]{}; // resources delivered to a storage
    uint16_t trained[K_Count]{};  // units trained per kind
    uint16_t donations = 0;
    uint8_t doneSeq = 0, lastDone = OBJ_None, lastRewarded = 0; // runtime: completion banner for the client
};

// Events systems post (never polled by UI). Gather / Trained / Donate update lifetime counters; the rest only wake the
// evaluation.
enum ObjEventType : uint8_t
{
    OE_Gather,
    OE_Built,
    OE_Trained,
    OE_Raid,
    OE_Level,
    OE_Donate
};
struct ObjEvent
{
    uint8_t player, type;
    int arg;
    float amount;
};
struct ObjectiveBus
{
    std::vector<ObjEvent> q;
};

// Systems call this; a missing bus (headless pieces without the system) is ignored.
inline void postObjective(entt::registry &reg, int player, ObjEventType type, int arg = 0, float amount = 1.f)
{
    if (auto *bus = reg.ctx().find<ObjectiveBus>(); bus && bus->q.size() < 8192)
        bus->q.push_back({uint8_t(player), uint8_t(type), arg, amount});
}
