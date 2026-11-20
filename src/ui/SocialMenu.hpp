/**
 * @file SocialMenu.hpp
 * @brief Social menu: players / friends / clans / my clan / factions, with search, travel and diplomacy buttons.
 */
#pragma once
#include "rendering/Assets.hpp"

#include <string>
#include <vector>

class App;
struct Peer;
struct FacTown;

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
    static SBtn friendBtn(const Peer &p);
    SRow peerRow(const Peer &p);
    SRow memberRow(const Peer &p, int myClan);
    void addPeerRows(std::vector<SRow> &rows, bool friendsOnly);
    void addClanRows(std::vector<SRow> &rows);
    void addMyClanRows(std::vector<SRow> &rows);
    void addFactionRows(std::vector<SRow> &rows);
    const FacTown *nearestTown(int faction) const;
    void typeSearch();
    void click(Vector2 m);
    void clickRows(Vector2 m);
    void drawSearchBox();
    std::string drawRows(Vector2 m);
    void drawRowButtons(const SRow &row, const Rectangle &rr, Vector2 m, std::string &tip);
    void drawBottom(Vector2 m, const Rectangle &sr);
    static void drawTip(Vector2 m, const std::string &tip);
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
