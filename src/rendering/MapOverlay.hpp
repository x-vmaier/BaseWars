#pragma once
// Icons shared by the world map and the minimap: territory, towns, faction towns, convoys and support patrols.
#include <raylib.h>

#include <string>

class App;

class MapOverlay
{
  public:
    explicit MapOverlay(App &app) : app(app) {}
    // `in`: map rectangle, `c`: tile at its centre, `tpp`: tiles per pixel.
    void draw(Rectangle in, Vector2 c, float tpp, bool mini);

  private:
    static void icon(const std::string &file, Vector2 p, float width, float artW, float artH, Color tint = WHITE);
    void supportIcons(Rectangle in, Vector2 c, float tpp, bool mini);
    App &app;
};
