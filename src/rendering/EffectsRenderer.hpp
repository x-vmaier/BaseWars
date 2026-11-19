#pragma once
// Transient effects: S_State effect events (explosions, dust, splashes, heals, arrows, fire arrows, extinguishing),
// resource drops, dissolving ghosts of destroyed entities, command markers, building fire / disabled / moving status,
// and translucent clouds drifting over discovered land.
#include "client/ClientState.hpp"
#include "client/Components.hpp"
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
    void queueBuildingStatus(const Info &i, uint32_t id, float cx, float bottom, int fw, int fh, float sortY,
                             DrawList &items, double t);
    void drawLandClouds(const TerrainView &v, float t);

  private:
    void queueBaseFx(const Fx &f, DrawList &items, double t); // fire arrows, extinguishing
    App &app;
};
