#pragma once
// Main-menu backdrop: drifting camera over a few tiny islands with buildings, idle units, sheep and clouds.
class App;

class MenuBackdrop
{
public:
    explicit MenuBackdrop(App &app) : app(app) {}
    void draw(float t);

private:
    App &app;
};
