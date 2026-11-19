#pragma once
// Transient effects: S_State effect events (explosions, dust, splashes, heals, arrows, fire arrows, extinguishing),
// resource drops, dissolving ghosts of destroyed entities, command markers, building fire / disabled / moving status,
// and translucent clouds drifting over discovered land.
#include "client/ClientState.hpp"
#include "client/Components.hpp"
#include "rendering/BuildingView.hpp"
#include "rendering/DrawItem.hpp"

class App;
struct TerrainView;

class EffectsRenderer
{
  public:
    explicit EffectsRenderer(App &app) : app(app) {}
    void queueMarkers(DrawList &items);
    void queueGhosts(DrawList &items, double t);
    void queueFx(DrawList &items, double t);
    void queueDrops(DrawList &items, double t);
    void queueBuildingStatus(const BuildingView &v, DrawList &items);
    void drawLandClouds(const TerrainView &v, float t);

  private:
    App &app;
};
