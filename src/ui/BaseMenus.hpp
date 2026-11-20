/**
 * @file BaseMenus.hpp
 * @brief Base-mechanics HUD: clan alert toasts (click to jump), homeless banner, town-move progress, placement-mode
 * banner.
 */
#pragma once
#include "client/ClientState.hpp"
#include "rendering/Assets.hpp"

#include <vector>

class App;

class BaseMenus
{
  public:
    explicit BaseMenus(App &app) : app(app) {}

    void load();
    void push(const Toast &t);
    bool over(Vector2 m) const;
    bool input(Vector2 m); // true when a click was consumed
    void draw();

  private:
    Rectangle toastRect(int i) const;
    Rectangle homelessBanner() const;
    Rectangle defeatPanel() const;
    Rectangle defeatButton(int i) const; // 0 Resettle (banner and offer), 1 Accept (offer only)
    void drawDefeat(Vector2 m);
    void drawOffer(Vector2 m);
    void drawOfferSite() const;
    void drawToast(int i, Vector2 m, double now);
    void drawHomeless(Vector2 m);
    void drawPlacementBanner();
    App &app;
    std::vector<Toast> toasts;
    TMap mPaper;
};
