#pragma once
// The client application: owns the shared state (settings, camera, replicated world) and every subsystem, and runs
// the frame loop. Subsystems hold an `App&` and reach each other through it.
//
//   client/     Session (connection), PacketHandler (S_*), Commands (C_*), Input, Placement, TestHooks
//   rendering/  WaterRenderer, WorldRenderer (terrain, features, buildings, units, ships, effects), PostFx,
//               Minimap, MapOverlay, MenuBackdrop
//   ui/         Hud, BuildMenu, ContextPanel, TrainingPanel, TownHallWindow (+ TownHallTabs), CostRow, SocialMenu, Chat, PauseMenu, MainMenu, BigMap,
//               ShipPanel, BaseMenus (+ UiScale, Widgets)
#include "client/ClientWorld.hpp"
#include "client/Commands.hpp"
#include "client/Input.hpp"
#include "client/PacketHandler.hpp"
#include "client/Placement.hpp"
#include "client/Session.hpp"
#include "client/Settings.hpp"
#include "client/TestHooks.hpp"
#include "rendering/MapOverlay.hpp"
#include "rendering/MenuBackdrop.hpp"
#include "rendering/Minimap.hpp"
#include "rendering/PostFx.hpp"
#include "rendering/WaterRenderer.hpp"
#include "rendering/WorldRenderer.hpp"
#include "ui/BaseMenus.hpp"
#include "ui/BigMap.hpp"
#include "ui/BuildMenu.hpp"
#include "ui/Chat.hpp"
#include "ui/ContextPanel.hpp"
#include "ui/Hud.hpp"
#include "ui/MainMenu.hpp"
#include "ui/PauseMenu.hpp"
#include "ui/ShipPanel.hpp"
#include "ui/SocialMenu.hpp"
#include "ui/TownHallWindow.hpp"
#include "ui/TrainingPanel.hpp"

#include <entt/entt.hpp>
#include <raylib.h>

#include <string>

class App {
public:
    App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // ---- shared state
    Settings settings;
    Camera2D cam{};
    ClientWorld world;
    entt::dispatcher disp;              // LogEvent -> Hud
    bool wantQuit = false;

    // ---- client logic
    Session session;
    PacketHandler packets;
    Commands cmd;
    Placement placement;
    Input input;
    TestHooks hooks;

    // ---- rendering
    WaterRenderer water;
    PostFx post;
    WorldRenderer worldRenderer;
    Minimap minimap;
    MapOverlay mapOverlay;
    MenuBackdrop backdrop;

    // ---- UI
    Hud hud;
    BuildMenu buildMenu;
    ContextPanel context;
    TrainingPanel training;
    TownHallWindow townHall;
    SocialMenu social;
    Chat chat;
    PauseMenu pause;
    MainMenu mainMenu;
    BigMap bigMap;
    ShipPanel shipPanel;
    BaseMenus baseMenus;

    void run(const std::string& autoJoin);
    void msg(const std::string& s) { disp.enqueue(LogEvent{s}); }
    bool overUI(Vector2 m);             // m in UI space (ui::mouse())
    void resetWorld();                  // drop all replicated state and close panels (join / leave)
    void travelTo(float tx, float ty);  // move the camera to a tile and close the social menu / world map

private:
    void loadGfx();
    void drawUI(float dt);
};
