#pragma once
// Building placement: the kind being placed and the special placement modes (homeless Town Hall, move town,
// relocate building), placement checks, costs and the feature-clearing step before building.
#include "common/Shared.hpp"

#include <raylib.h>

#include <string>

class App;

class Placement {
public:
    enum Mode { Normal = 0, HomelessHall = 1, MoveTown = 2, Relocate = 3 };   // placeMode values
    explicit Placement(App& app) : app(app) {}

    int buildKind = -1;                 // Kind being placed, -1 = none
    int placeMode = Normal;
    uint32_t placeTarget = 0;           // building being relocated

    bool active() const { return buildKind >= 0; }
    void cancel() { buildKind = -1; }
    int effMode() const { return buildKind >= 0 ? placeMode : 0; }
    int curPlaceMode() const;           // PM_* for checkPlace
    const float* placeCost(Kind k) const;
    static IV placeTile(Kind k, Vector2 world);
    int placeCheck(Kind k, IV t);
    void begin(int mode, Kind k, uint32_t target);
    void selectBuildKind(Kind k);       // build menu entry / hotkey (toggles)
    void click(Vector2 world, bool shift);          // left click in the world while placing
    std::string ghostLabel(Kind k, IV t) const;     // "click to clear N tiles first:" or "" (the wood cost is drawn as a cost row)

private:
    void place(Kind k, IV t);
    bool clearClick(Kind k, IV t);      // when only features block the footprint, start clearing exactly those tiles
    App& app;
};
