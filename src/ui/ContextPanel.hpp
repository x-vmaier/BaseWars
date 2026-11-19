#pragma once
// Context panel of a building (right-click): owner / stats lines, action buttons (train, upgrade, attack, alliance,
// donations, relocate) each with its cost row beside it, the auto-haul slider and the next-level preview.
// (An own Town Hall opens the TownHallWindow instead.)
#include "client/Components.hpp"
#include "common/Shared.hpp"
#include "rendering/Assets.hpp"

#include <string>
#include <utility>
#include <vector>

class App;

class ContextPanel
{
  public:
    struct MenuAct
    {
        std::string label;
        int act;
        uint32_t a, b;
        bool enabled;
        bool red;
        bool hasCost = false;
        float cost[R_Count]{};
        std::string note; // cost row (and a short note) beside the button
    };
    explicit ContextPanel(App &app) : app(app) {}
    void load();

    bool isOpen = false, sliderDrag = false;
    uint32_t target = 0;

    void open(uint32_t id, Vector2 at);
    void close() { isOpen = false; }
    void rebuild(); // re-evaluate lines and buttons for the target (closes when it vanished)
    bool over(Vector2 m) const { return isOpen && CheckCollisionPointRec(m, rect()); }
    bool sliderInput(Vector2 m, bool lp, bool lr); // true: input consumed
    bool click(Vector2 m); // left click while open: true when consumed, otherwise the panel closes
    void draw();

  private:
    void run(const MenuAct &a);
    void baseContext(const Info &in, bool mine, bool done); // fire / territory / queue lines, move town, relocate
    void siteLines(const Info &in, bool friendly);          // site job progress, damage efficiency, paused production
    bool baseRun(const MenuAct &a);
    void addAct(MenuAct a, const float *cost = nullptr, const std::string &note = "");
    void layout(); // row heights of the action list and the panel height
    Rectangle rect() const;
    float topY() const;
    float innerW() const;
    Rectangle sliderRect() const;
    Rectangle button(int i) const;
    Rectangle costArea(int i) const; // where act i's cost row (and note) goes

    App &app;
    TMap mCtx;
    Vector2 pos{};
    std::vector<MenuAct> acts;
    std::vector<std::pair<std::string, Color>> lines;
    std::vector<std::string> next;
    std::vector<Kind> nextUnits;
    std::vector<float> rowY, rowH; // per act, relative to the first act row
    float panelH = 0;
    bool slider = false;
    int pct = 75;
};
