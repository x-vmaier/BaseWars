#pragma once
// Town Hall window (own Town Hall only), styled like the Archery window: a large paper frame with a tab strip.
//   Main     train pawns (card + queue, C_Train / C_Dequeue), upgrade / repair / cancel the hall with cost rows and the
//            next level's effects, move the town.
//   Storage  one slot per resource: icon, stock / share capacity, a share slider (C_SetAlloc), Dispose with a confirm
//            click (C_Dispose), and the hall's capacity per level.
//   Clan     membership (join via the social menu / leave), and a troop card per defense type with a quota counter
//            flanked by arrow buttons (C_SetQuotas).
// Drawn in panel space (natural size of the "townhall_panel" map), scaled to fit the screen like the Archery window.
#include "common/Shared.hpp"
#include "rendering/Assets.hpp"

#include <string>

class App;
struct Info;

class TownHallWindow
{
  public:
    enum Tab
    {
        T_Main,
        T_Storage,
        T_Clan,
        T_Count
    };
    explicit TownHallWindow(App &app) : app(app) {}
    void load();

    bool isOpen = false;
    int tab = T_Main;
    uint32_t target = 0; // own Town Hall

    bool openFor(uint32_t id); // false (and stays closed) unless id is our own completed Town Hall
    void close()
    {
        isOpen = false;
        dragRes = -1;
    }
    void input(Vector2 mouse);
    void draw();

    // panel-space layout (public for the screenshot test hook)
    Vector2 size() const { return mPanel.size(); }
    Vector2 toScreen(Vector2 p) const; // panel space -> UI space
    Rectangle tabRect(int i) const;
    Rectangle content() const;
    Rectangle trainBtn() const;
    Rectangle upgradeBtn() const;
    Rectangle allocSlider(int r) const;
    Rectangle disposeBtn(int r) const;
    Rectangle quotaArrow(int t, int dir) const;
    void armDisposeForTest(int r)
    {
        disposeArm = r;
        disposeAt = GetTime();
    }

  private:
    struct PanelXf
    {
        float ox, oy, sc;
    };
    PanelXf xf() const;
    Vector2 toPanel(Vector2 m) const;
    const Info *hall() const; // null when the target vanished or is not ours

    // main tab (TownHallWindow.cpp)
    Rectangle pawnCard() const;
    Rectangle queueSlot(int i) const;
    Rectangle repairBtn() const;
    Rectangle moveBtn() const;
    void inputMain(Vector2 m, const Info &in);
    void drawMain(Vector2 m, const Info &in);
    void drawHallSection(Vector2 m, const Info &in);
    // storage + clan tabs (TownHallTabs.cpp)
    Rectangle slot(int r) const;
    Rectangle clanBtn() const;
    Rectangle troopCard(int t) const;
    void inputStorage(Vector2 m);
    void drawStorage(Vector2 m, const Info &in);
    void drawCapacityChart(Rectangle r, int level);
    void inputClan(Vector2 m);
    void drawClan(Vector2 m);
    void card(Rectangle c, Kind k, bool selected, bool hover, bool dim,
              float phase); // Archery-style animated troop card
    void sendQuotas();

    App &app;
    TMap mPanel;
    int dragRes = -1;    // storage share slider being dragged
    int disposeArm = -1; // resource whose Dispose waits for the confirm click
    double disposeAt = 0;
    bool leaveArm = false;
    double leaveAt = 0;
};
