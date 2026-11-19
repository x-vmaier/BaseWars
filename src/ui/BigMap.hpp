#pragma once
// World map (N / minimap click): pan, zoom, travel, friend arrows, and the support-patrol menu (right-click an
// allied / clan town).
#include "rendering/Assets.hpp"

#include <string>
#include <vector>

class App;

class BigMap
{
  public:
    explicit BigMap(App &app) : app(app) {}
    void load();

    bool isOpen = false;
    int zoom = 4; // index into the tiles-per-pixel table

    void open();
    void input(Vector2 m);
    void draw();

  private:
    struct Cand
    {
        std::string name;
        float x, y;
    };
    struct Menu
    {
        bool open = false;
        Vector2 at{};
        float tx = 0, ty = 0;
        std::string name;
    };
    float tpp() const;
    Rectangle frameRect() const;
    Rectangle inner() const;
    Vector2 mapToTile(Vector2 p) const;
    Vector2 tileToMap(float tx, float ty) const;
    void refresh();
    std::vector<Cand> candidates();
    Rectangle menuRect() const;
    Rectangle minusBtn() const;
    Rectangle plusBtn() const;
    Rectangle sendBtn() const;
    bool rightClick(Vector2 m); // true when the click opened the menu
    bool menuInput(Vector2 m);  // consumes all input while the menu is open
    void drawMenu();

    App &app;
    TMap mMapFrame;
    bool dragging = false;
    int w = 0, h = 0, patrolN = 5;
    Vector2 center{}, dragMouse{};
    float dragMoved = 0;
    double last = 0;
    std::vector<Color> pix;
    Texture2D texture{};
    Menu menu;
};
