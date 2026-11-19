/**
 * @file BuildingRenderer.hpp
 * @brief Buildings (per-kind drawers, sites, damage fx, labels, bars) and the placement preview.
 */
#pragma once
#include "client/ClientState.hpp"
#include "client/Components.hpp"
#include "rendering/BuildingView.hpp"
#include "rendering/DrawItem.hpp"

class App;
class BuildingRenderer;

// One entry of the building drawer table (BuildingDrawers.cpp). A kind that is only a sprite needs no entry.
struct BuildingDrawer
{
    using DrawFn = void (BuildingRenderer::*)(const BuildingView &) const;
    using QueueFn = void (BuildingRenderer::*)(const BuildingView &, DrawList &) const;

    DrawFn underlay = nullptr; // drawn before the body (pier)
    DrawFn body = nullptr;     // replaces the sprite body; null = sprite from buildingSprite()
    QueueFn extras = nullptr;  // additional sorted items (smoke, name label)
    DrawFn overlay = nullptr;  // drawn in the status item (hover rings)
    bool siteFx = true;        // scaffold while under construction / repair
    bool damageFx = true;      // smoke and flames when damaged
};

class BuildingRenderer
{
  public:
    explicit BuildingRenderer(App &app) : app(app) {}
    void queue(const RPos &p, const Info &i, uint32_t id, bool selected, bool hover, bool remembered, DrawList &items,
               double t);
    void queuePlacementGhost(DrawList &items);
    void queuePendingSites(DrawList &items); // own buildings waiting for pawns to clear their site

  private:
    BuildingView makeView(const RPos &p, const Info &i, uint32_t id, bool selected, bool hover, bool remembered,
                          double t) const;
    void queueBody(const BuildingView &v, DrawList &items) const;
    void queueDamageFx(const BuildingView &v, DrawList &items) const;
    void queueStatus(const BuildingView &v, DrawList &items) const;
    void drawBody(const BuildingView &v) const;
    void drawSpriteBody(const BuildingView &v) const;
    void drawCrippledLabel(const BuildingView &v) const;
    void drawLastSeen(const BuildingView &v) const;
    void drawStatus(const BuildingView &v) const;
    void drawProgressBars(const BuildingView &v) const;
    void siteOverlay(const BuildingView &v) const; // scaffold + tool icon

    // Per-kind drawers listed in the table in BuildingDrawers.cpp.
    void drawPier(const BuildingView &v) const;
    void drawCampfire(const BuildingView &v) const;
    void drawMine(const BuildingView &v) const;
    void queueCampfireSmoke(const BuildingView &v, DrawList &items) const;
    void queueTownHallName(const BuildingView &v, DrawList &items) const;
    void drawWarmthRing(const BuildingView &v) const;
    static const BuildingDrawer &drawerOf(Kind k);

    // Placement preview (BuildingGhost.cpp).
    void drawGhost(Kind k, IV tt, int err, bool ok) const;
    void drawTerritory() const;
    void drawGhostFootprint(Kind k, IV tt, int err, bool ok) const;
    void drawGhostSprite(Kind k, IV tt, float cx, float bottom) const;
    void drawPendingSite(const PendingSite &site) const;
    void drawPlaceExtras() const;                       // clan-radius hints while placing a homeless Town Hall
    void mineGhost(IV t, float cx, float bottom) const; // gold bonus radius while placing a mine
    void ghostLabel(Kind k, IV t, int err, float cx, float bottom) const; // reason / cost row / clearing cost
    App &app;
};
