/**
 * @file PauseMenu.hpp
 * @brief Pause menu (Esc): UI scale / volume sliders, window mode, save settings, resume, leave, quit.
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
    Rectangle slider(int rowIdx) const; // rows 0 (UI scale), 2 (master volume), 3 (effects volume)
    Rectangle arrow(int dir) const;
    Rectangle saveBtn() const;
    Rectangle hintsBtn() const;
    void dragSlider();
    void drawVolumeRow(int rowIdx, const char *label, float value);
    void onClick(Vector2 m);
    void drawScaleRow();
    void drawWindowRow(Vector2 m);
    void drawButtons(Vector2 m);

    App &app;
    TMap mPause;
    int sliderDrag = -1; // row of the slider being dragged, -1 = none
    double savedAt = -100;
};
