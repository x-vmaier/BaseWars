/**
 * @file EffectsRenderer.hpp
 * @brief Transient effects: fx events, drops, damage numbers, ghosts, markers, building status and land clouds.
 */
#pragma once
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
    void queueDamageNumbers(DrawList &items, double t);
    void queueBuildingStatus(const BuildingView &v, DrawList &items);
    void drawLandClouds(const TerrainView &v, float t);

  private:
    App &app;
};
