#pragma once
// Every server system, owned together. Systems reach each other through this aggregate (explicit, no inheritance).
#include "systems/BuildSystem.hpp"
#include "systems/ClanSystem.hpp"
#include "systems/ClearingSystem.hpp"
#include "systems/CombatSystem.hpp"
#include "systems/ConstructionSystem.hpp"
#include "systems/ConvoyHaulSystem.hpp"
#include "systems/DefenseSystem.hpp"
#include "systems/EnvironmentSystem.hpp"
#include "systems/FactionSystem.hpp"
#include "systems/FerryPlanner.hpp"
#include "systems/FireSystem.hpp"
#include "systems/HuntSystem.hpp"
#include "systems/NavalSystem.hpp"
#include "systems/NetSession.hpp"
#include "systems/PacketHandler.hpp"
#include "systems/PatrolSystem.hpp"
#include "systems/RaidSystem.hpp"
#include "systems/Replication.hpp"
#include "systems/SaveSystem.hpp"
#include "systems/SpawnSystem.hpp"
#include "systems/TerritorySystem.hpp"
#include "systems/TownSystem.hpp"
#include "systems/TrainingSystem.hpp"
#include "systems/UnitSystem.hpp"

struct Systems {
    explicit Systems(World& w)
        : spawn(w, *this), session(w, *this), packets(w, *this), repl(w, *this), build(w, *this), units(w, *this), env(w, *this),
          training(w, *this), fire(w, *this), territory(w, *this), clearing(w, *this), convoy(w, *this), hunt(w, *this),
          faction(w, *this), town(w, *this), patrol(w, *this), raid(w, *this), clan(w, *this), defense(w, *this), naval(w, *this),
          ferry(w, *this), combat(w, *this), save(w, *this), construction(w, *this) {}
    Systems(const Systems&) = delete;
    Systems& operator=(const Systems&) = delete;

    SpawnSystem spawn;
    NetSession session;
    PacketHandler packets;
    Replication repl;
    BuildSystem build;
    UnitSystem units;
    EnvironmentSystem env;
    TrainingSystem training;
    FireSystem fire;
    TerritorySystem territory;
    ClearingSystem clearing;
    ConvoyHaulSystem convoy;
    HuntSystem hunt;
    FactionSystem faction;
    TownSystem town;
    PatrolSystem patrol;
    RaidSystem raid;
    ClanSystem clan;
    DefenseSystem defense;
    NavalSystem naval;
    FerryPlanner ferry;
    CombatSystem combat;
    SaveSystem save;
    ConstructionSystem construction;
};
