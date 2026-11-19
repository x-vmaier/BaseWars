#pragma once
// Draws the world in camera space: terrain, then all sprites sorted by foot y, then fog and clouds.
#include "rendering/BuildingRenderer.hpp"
#include "rendering/EffectsRenderer.hpp"
#include "rendering/FeatureRenderer.hpp"
#include "rendering/ShipRenderer.hpp"
#include "rendering/TerrainRenderer.hpp"
#include "rendering/UnitRenderer.hpp"

class App;

class WorldRenderer {
public:
    explicit WorldRenderer(App& app) : app(app), terrain(app), features(app), buildings(app), units(app), ships(app), effects(app) {}
    void draw(float t);                 // inside BeginMode2D(cam)

private:
    App& app;

public:
    TerrainRenderer terrain;
    FeatureRenderer features;
    BuildingRenderer buildings;
    UnitRenderer units;
    ShipRenderer ships;
    EffectsRenderer effects;
};
