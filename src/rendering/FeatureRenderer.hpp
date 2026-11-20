/**
 * @file FeatureRenderer.hpp
 * @brief Surface features (trees, bushes, rocks, gold veins) with clearing progress and gold sparkle near own mines.
 */
#pragma once
#include "common/Shared.hpp"
#include "rendering/DrawItem.hpp"

#include <raylib.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class App;
struct TerrainView;

// One surface feature on screen; copied into its draw item.
struct FeatureView
{
    uint8_t f, js;
    bool hover, nearMine;
    float cx, by, jp;
    float alpha; // canopy fade (1 = opaque), trees only
    uint32_t h;
    double t;
};

class FeatureRenderer
{
  public:
    explicit FeatureRenderer(App &app) : app(app) {}
    void frame(); // once per frame before queue(): own mines (+ the mine being placed)
    void queue(const TerrainView &v, int x0, int y0, int x1, int y1, DrawList &items, double t);

  private:
    struct TLine
    {
        std::string text;
        int res = -1;
        float amt = 0;
        bool cost = false;
    };
    struct TipBox
    {
        std::vector<TLine> lines;
        int sz, lh;
        float zoom, cx, by;
        float have[R_Count];
    };

    FeatureView makeView(uint8_t f, int x, int y, double t) const;
    void drawFeature(const FeatureView &fv) const;
    static void drawTooltipBox(const TipBox &tip);
    bool goldNearMine(int x, int y) const;
    static void goldGlints(float cx, float by, uint32_t h, double t, bool strong);
    void queueTooltip(uint8_t f, int x, int y, float cx, float by, DrawList &items);
    float canopyAlpha(int x, int y, float cx, float by, float top, float halfW);
    App &app;
    std::vector<Vector2> mineCtr;            // centres (tiles) of own mines and the mine being placed
    std::vector<Vector2> ownFeet;            // world-pixel foot points of own units (canopy occlusion)
    std::unordered_map<int64_t, float> fade; // per-tree alpha keyed by tile; entries drop back to 1 and are erased
};
