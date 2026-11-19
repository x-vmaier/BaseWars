/**
 * @file UnitRenderer.hpp
 * @brief Units: animated sprites, shadows, selection, outline, health bars, labels, per-kind drawers.
 */
#pragma once
#include "client/Components.hpp"

#include "rendering/DrawItem.hpp"
#include <raylib.h>

class App;
class UnitRenderer;

// Everything one unit needs while it is drawn; copied into the draw items.
struct UnitView
{
    Info i;
    bool left, selected, hover, moving, convoy;
    float px, py;
    int carry, biome;
    uint32_t id;
    Color outline;
    double t;
};

// One entry of the unit drawer table (UnitRenderer.cpp). A kind drawn like a plain humanoid needs no entry.
struct UnitDrawer
{
    using DrawFn = void (UnitRenderer::*)(const UnitView &) const;
    using QueueFn = void (UnitRenderer::*)(const UnitView &, DrawList &) const;

    DrawFn body = nullptr;    // replaces the whole unit drawing (ships)
    DrawFn overlay = nullptr; // extra effect drawn over the sprite (heal aura)
    QueueFn extras = nullptr; // additional sorted items (wake splash)
    bool healthBar = true;
    bool fixedOutline = false; // hover outline does not depend on diplomacy
};

class UnitRenderer
{
  public:
    explicit UnitRenderer(App &app) : app(app) {}
    void queue(const RPos &p, Info &i, Face &fc, uint32_t id, bool selected, bool hover, DrawList &items, double t,
               int biome);

  private:
    UnitView makeView(const RPos &p, const Info &i, const Face &fc, uint32_t id, bool selected, bool hover, double t,
                      int biome) const;
    void drawUnit(const UnitView &v) const;
    void drawLabels(const UnitView &v) const;
    void drawShip(const UnitView &v) const;
    void drawHealAura(const UnitView &v) const;
    void queueShipWake(const UnitView &v, DrawList &items) const;
    void drawShipWake(const UnitView &v) const;
    static const UnitDrawer &drawerOf(Kind k);
    static void extinguishSplash(float px, float py, bool left, double t, uint32_t id);
    App &app;
};
