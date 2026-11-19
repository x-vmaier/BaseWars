#pragma once
// Context panel of a building (right-click): owner / stats lines, action buttons (train, upgrade, attack, alliance,
// donations, move town, relocate), the auto-haul / defense-quota slider and the next-level preview.
#include "client/Components.hpp"
#include "rendering/Assets.hpp"

#include <string>
#include <utility>
#include <vector>

class App;

class ContextPanel {
public:
    struct MenuAct { std::string label; int act; uint32_t a, b; bool enabled; bool red; };
    explicit ContextPanel(App& app) : app(app) {}
    void load();

    bool isOpen = false, sliderDrag = false;
    uint32_t target = 0;

    void open(uint32_t id, Vector2 at);
    void close() { isOpen = false; }
    void rebuild();                     // re-evaluate lines and buttons for the target (closes when it vanished)
    bool over(Vector2 m) const { return isOpen && CheckCollisionPointRec(m, rect()); }
    bool sliderInput(Vector2 m, bool lp, bool lr);   // true: input consumed
    bool click(Vector2 m);              // left click while open: true when consumed, otherwise the panel closes
    void draw();

private:
    void run(const MenuAct& a);
    void baseContext(const Info& in, bool mine, bool done);   // fire / territory / queue lines, move town, relocate
    bool baseRun(const MenuAct& a);
    static int quotaFromMouse(float mx, const Rectangle& sr);
    Rectangle rect() const;
    float topY() const;
    Rectangle sliderRect() const;
    Rectangle button(int i) const;

    App& app;
    TMap mCtx;
    Vector2 pos{};
    std::vector<MenuAct> acts;
    std::vector<std::pair<std::string, Color>> lines;
    std::vector<std::string> next;
    std::vector<Kind> nextUnits;
    bool slider = false, quota = false;
    int pct = 75;
};
