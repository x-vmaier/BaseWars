#pragma once
// Buildings (sprites, harbour pier, campfire, mine, construction / upgrade / repair sites, damage smoke / flames,
// labels, bars) and the placement preview (footprint, territory, cost / error label).
#include "client/ClientState.hpp"
#include "client/Components.hpp"
#include "rendering/DrawItem.hpp"

class App;

class BuildingRenderer
{
  public:
    explicit BuildingRenderer(App &app) : app(app) {}
    void queue(const RPos &p, const Info &i, uint32_t id, bool selected, bool hover, bool remembered, DrawList &items,
               double t);
    void queuePlacementGhost(DrawList &items);
    void queuePendingSites(DrawList &items); // own buildings waiting for pawns to clear their site

  private:
    void drawPlaceExtras();                       // clan-radius hints while placing a homeless Town Hall
    void mineGhost(IV t, float cx, float bottom); // gold bonus radius while placing a mine
    void ghostLabel(Kind k, IV t, int err, float cx, float bottom); // reason / cost row / clearing cost under the ghost
    void siteOverlay(Kind k, float cx, float bottom, BJob job, bool hasJob, uint32_t id,
                     double t); // scaffold + tool icon
    App &app;
};
