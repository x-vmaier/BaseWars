#pragma once
// Minimap (top right): 100x100 tiles around the camera, refreshed a few times per second, with the shared map overlay.
#include "rendering/Assets.hpp"

#include <vector>

class App;

class Minimap {
public:
    static constexpr int MM = 100;
    explicit Minimap(App& app) : app(app) {}
    void load();
    void update();                      // throttled texture refresh
    void invalidate() { lastMM = 0; }   // new terrain / fog
    void draw();
    Rectangle frame() const;
    Rectangle rect() const;

private:
    App& app;
    TMap mMini;
    double lastMM = 0;
    std::vector<Color> pix;
    Texture2D texture{};
};
