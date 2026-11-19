#pragma once
// Ships (three visibly different tiers), their load label, and the path lines the server shares with us.
#include "client/Components.hpp"
#include "rendering/DrawItem.hpp"

class App;

class ShipRenderer
{
public:
    explicit ShipRenderer(App &app) : app(app) {}
    void drawShip(float px, float py, int owner, bool flip, bool hover, double t, uint32_t id, int tier, int troops);
    void drawLabel(float px, float py, const Info &i, bool selected, bool hover);
    void queuePathLines(DrawList &items);

private:
    App &app;
};
