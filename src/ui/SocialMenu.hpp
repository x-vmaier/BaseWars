#pragma once
// Social menu: players / friends / clans / my clan / factions, with search, travel and diplomacy buttons.
#include "rendering/Assets.hpp"

#include <string>
#include <vector>

class App;

class SocialMenu
{
  public:
    explicit SocialMenu(App &app) : app(app) {}
    void load();

    bool isOpen = false, textFocus = false;
    int tab = 0, scroll = 0;
    std::string searchText;

    void open()
    {
        isOpen = true;
        textFocus = false;
    }
    void input(Vector2 m);
    void draw();

  private:
    struct SBtn
    {
        std::string label;
        int act;
        float a, b;
        int c;
        bool enabled, red;
        float w;
        int icon = -1;
    }; // icon: resource drawn instead of a label
    struct SRow
    {
        std::string text, sub;
        Color col;
        std::vector<SBtn> btns;
    };
    static constexpr float SROW = 30.f;
    std::vector<SRow> rows();
    void run(const SBtn &b);
    bool bottomBtn(SBtn &out);
    float scale() const;
    Rectangle rect() const;
    Rectangle tabRect(int i) const;
    Rectangle search() const;
    Rectangle list() const;
    Rectangle bottomBtnRect() const;
    Rectangle closeRect() const;
    static Rectangle rowBtnRect(const Rectangle &row, const std::vector<SBtn> &btns, size_t i);

    App &app;
    TMap mSocial;
};
