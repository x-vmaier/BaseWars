#pragma once
// Base-mechanics HUD: clan alert toasts (click to jump), homeless banner, town-move progress, placement-mode banner.
#include "client/ClientState.hpp"

#include <vector>

class App;

class BaseMenus {
public:
    explicit BaseMenus(App& app) : app(app) {}

    void push(const Toast& t);
    bool over(Vector2 m) const;
    bool input(Vector2 m);              // true when a click was consumed
    void draw();

private:
    Rectangle toastRect(int i) const;
    Rectangle homelessBanner() const;
    App& app;
    std::vector<Toast> toasts;
};
