#pragma once
// Ship panel (right-click an own ship without troops selected): load, cargo, disembark. Also right-click embarking.
#include "client/Components.hpp"

#include <raylib.h>

#include <string>

class App;

class ShipPanel
{
  public:
    explicit ShipPanel(App &app) : app(app) {}

    uint32_t ship = 0; // ship shown, 0 = closed

    bool over(Vector2 m) const { return ship && CheckCollisionPointRec(m, rect()); }
    bool rightClick(uint32_t id);            // embark order or open the panel; true when consumed
    bool input(Vector2 m, bool lp, bool rp); // true when consumed
    void draw();
    std::string tooltip(const Info &in) const; // hover label suffix for ships

  private:
    Rectangle rect() const;
    Rectangle btn(int i) const;
    App &app;
};
