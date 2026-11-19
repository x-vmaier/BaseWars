/**
 * @file PostFx.hpp
 * @brief Scene render target, bloom and the post shader (grading, day/night, vignette, lights).
 */
#pragma once
#include <raylib.h>

class App;

class PostFx
{
  public:
    explicit PostFx(App &app) : app(app) {}
    void load();
    void ensureTargets();  // (re)create targets on resize
    void beginScene();     // render the world into the scene target
    void endScene();       // ... and run the bloom passes
    void present(float t); // draw the scene target to the screen through the post shader

    float shake = 0, damage = 0; // screen shake (pixels) and damage vignette, decay over time

  private:
    void runBloom();
    void uniforms(float t);
    App &app;
    RenderTexture2D rt{}, bloomA{}, bloomB{};
    Shader shPost{}, shBright{}, shBlur{};
    bool okPost = false, okBloom = false;
    float cold = 0, warm = 0;
};
