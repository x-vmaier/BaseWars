/**
 * @file MapOverlay.hpp
 * @brief Icons shared by the world map and the minimap: territory, towns, faction towns, convoys and support patrols.
 */
#pragma once
#include <raylib.h>

#include <set>
#include <string>

class App;

class MapOverlay
{
  public:
    explicit MapOverlay(App &app) : app(app) {}
    // `in`: map rectangle, `c`: tile at its centre, `tpp`: tiles per pixel.
    void draw(Rectangle in, Vector2 c, float tpp, bool mini);

  private:
    struct View
    {
        Rectangle in;
        Vector2 c;
        float tpp;
        bool mini;

        Vector2 toScreen(float tx, float ty) const
        {
            return {in.x + in.width / 2 + (tx - c.x) / tpp, in.y + in.height / 2 + (ty - c.y) / tpp};
        }
        bool inside(Vector2 p, float pad) const
        {
            return p.x > in.x + pad && p.y > in.y + pad && p.x < in.x + in.width - pad && p.y < in.y + in.height - pad;
        }
    };

    static void icon(const std::string &file, Vector2 p, float width, float artW, float artH, Color tint = WHITE);
    void territory(const View &v);
    void factionTowns(const View &v);
    void playerTowns(const View &v, std::set<int> &drawnTH);
    void clanBanner(const View &v, Vector2 sp, float castleH, int clan);
    void hiddenTowns(const View &v, const std::set<int> &drawnTH);
    void convoys(const View &v);
    void supportIcons(Rectangle in, Vector2 c, float tpp, bool mini);
    App &app;
};
