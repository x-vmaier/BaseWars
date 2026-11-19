#pragma once
// Slide-out build menu (left edge): building list with cost rows and hover tooltips, hotkeys 1..7.
#include "common/Shared.hpp"
#include "rendering/Assets.hpp"
#include "ui/CostRow.hpp"

class App;

class BuildMenu {
public:
    static constexpr int COUNT = 7;
    static constexpr float LEFT = 48;   // open menu x (right of the social tab button)
    static constexpr Kind LIST[COUNT] = {K_Outpost, K_Harbour, K_Storage, K_Archery, K_Mine, K_Lumber, K_Campfire};
    explicit BuildMenu(App& app) : app(app) {}
    void load();

    bool isOpen = false;
    float slide = 0;                    // 0 hidden .. 1 fully shown

    Rectangle rect() const;
    Rectangle tab() const;
    Rectangle item(int i) const;
    bool over(Vector2 m) const;
    void hotkeys();                     // number keys while open
    bool click(Vector2 m);              // left click; true when consumed
    void draw(float dt);

private:
    static ui::CostStyle rowStyle(float maxW, const float* cost);
    void tooltip(Kind k, Rectangle item, int gate);
    App& app;
    TMap mBuild;
};
