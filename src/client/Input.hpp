#pragma once
// Per-frame input routing while playing: modal panels first, then camera, hotkeys, selection and world commands.
#include "common/Shared.hpp"

#include <raylib.h>

#include <cstdint>
#include <set>
#include <vector>

class App;

class Input
{
  public:
    enum Mode
    {
        M_None,
        M_Move,
        M_Attack
    };

    explicit Input(App &app) : app(app) {}

    Mode mode = M_None;
    bool dragging = false;
    Vector2 dragStart{}; // real pixels
    std::set<uint32_t> sel;
    uint32_t hovered = 0;
    bool hasFeat = false; // a surface feature (tree, bush, rock, gold vein) is under the mouse
    IV hoverFeat{0, 0};
    double lastCam = 0;

    void update(float dt);
    uint32_t pick(Vector2 mouse, bool includeBuildings = true); // real pixels
    std::vector<uint32_t> selUnits();
    std::vector<uint32_t> selTroops(); // own troops that can board a ship
    int armyCountSel();
    void forget(uint32_t id)
    {
        sel.erase(id);
        if (hovered == id)
            hovered = 0;
    }

  private:
    void camera(float dt, Vector2 rm, bool ui);
    bool hotkeys(); // true: stop processing this frame
    void doSelect(Vector2 a, Vector2 b, bool add);
    bool pickFeature(Vector2 mouse, IV &out);           // real pixels; tall trees are picked over their whole sprite
    void worldClick(Vector2 w, Vector2 rm, bool shift); // left click in the world (not placing)
    void worldRightClick(Vector2 rm, Vector2 m, Vector2 w);
    App &app;
};
