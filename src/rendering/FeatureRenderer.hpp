#pragma once
// Surface features (trees, bushes, rocks, gold veins) with clearing progress and gold sparkle near own mines.
#include "rendering/DrawItem.hpp"

#include <raylib.h>

#include <cstdint>
#include <vector>

class App;
struct TerrainView;

class FeatureRenderer {
public:
    explicit FeatureRenderer(App& app) : app(app) {}
    void frame();                       // once per frame before queue(): own mines (+ the mine being placed)
    void queue(const TerrainView& v, int x0, int y0, int x1, int y1, DrawList& items, double t);

private:
    bool goldNearMine(int x, int y) const;
    static void goldGlints(float cx, float by, uint32_t h, double t, bool strong);
    App& app;
    std::vector<Vector2> mineCtr;       // centres (tiles) of own mines and the mine being placed
};
