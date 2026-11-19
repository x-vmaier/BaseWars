#pragma once
// Buildings (sprites, harbour pier, campfire, mine, construction, damage smoke / flames, labels, bars) and the
// placement preview (footprint, territory, cost / error label).
#include "client/Components.hpp"
#include "rendering/DrawItem.hpp"

class App;

class BuildingRenderer {
public:
    explicit BuildingRenderer(App& app) : app(app) {}
    void queue(const RPos& p, const Info& i, uint32_t id, bool selected, bool hover, DrawList& items, double t);
    void queuePlacementGhost(DrawList& items);

private:
    void drawPlaceExtras();             // clan-radius hints while placing a homeless Town Hall
    void mineGhost(IV t, float cx, float bottom);   // gold bonus radius while placing a mine
    App& app;
};
