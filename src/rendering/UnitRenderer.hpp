#pragma once
// Units: animated sprites, shadows, selection, outline, health bars, convoy / patrol labels, firefighting splash.
#include "client/Components.hpp"
#include "rendering/DrawItem.hpp"

class App;

class UnitRenderer
{
  public:
    explicit UnitRenderer(App &app) : app(app) {}
    void queue(const RPos &p, Info &i, Face &fc, uint32_t id, bool selected, bool hover, DrawList &items, double t,
               int biome);

  private:
    static void extinguishSplash(float px, float py, bool left, double t, uint32_t id);
    App &app;
};
