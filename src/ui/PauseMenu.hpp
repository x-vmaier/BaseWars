/**
 * @file PauseMenu.hpp
 * @brief Pause menu (Esc): UI scale slider, window mode, save settings, resume, leave, quit.
 */
#pragma once
#include "rendering/Assets.hpp"

class App;

class PauseMenu
{
  public:
    explicit PauseMenu(App &app) : app(app) {}
    void load();

    bool isOpen = false;

    static void applyWindowMode(int modeIdx);
    void setWindowMode(int modeIdx);
    void input();
    void draw();

  private:
    Rectangle rect() const;
    Rectangle row(int i) const;
    Rectangle slider() const;
    Rectangle arrow(int dir) const;
    void dragSlider(const Rectangle &sl);
    void onClick(Vector2 m);
    void drawScaleRow();
    void drawWindowRow(Vector2 m);
    void drawButtons(Vector2 m);

    App &app;
    TMap mPause;
    bool sliderDrag = false;
    double savedAt = -100;
};
