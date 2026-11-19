/**
 * @file WaterRenderer.hpp
 * @brief Full-screen animated water (water.fs) with click / splash ripples; plain tiled texture without the shader.
 */
#pragma once
#include <raylib.h>

class App;

class WaterRenderer
{
  public:
    explicit WaterRenderer(App &app) : app(app) {}
    void load();
    void draw(float t); // screen space, uses the camera for the shader's world mapping

  private:
    App &app;
    Shader sh{};
    bool ok = false;
};
