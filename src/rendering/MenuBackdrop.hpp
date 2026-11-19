/**
 * @file MenuBackdrop.hpp
 * @brief Main-menu backdrop: drifting camera over a few tiny islands with buildings, idle units, sheep and clouds.
 */
#pragma once
class App;

class MenuBackdrop
{
  public:
    explicit MenuBackdrop(App &app) : app(app) {}
    void draw(float t);

  private:
    App &app;
};
