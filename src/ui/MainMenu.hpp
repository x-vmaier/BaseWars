/**
 * @file MainMenu.hpp
 * @brief Main menu and server browser: Internet / Friends / Direct tabs, server list, connecting overlay.
 */
#pragma once
#include "networking/NetTypes.hpp"
#include "rendering/Assets.hpp"

#include <string>
#include <vector>

class App;

class MainMenu
{
  public:
    explicit MainMenu(App &app) : app(app) {}
    void load();

    std::string status, directAddr; // status line under the panel, Direct-tab address

    void refresh(); // re-query the server browser
    void update();
    void draw();

  private:
    void buildList();
    Rectangle rect() const;
    Rectangle tabBtn(int i) const;
    Rectangle nameBox() const;
    Rectangle listArea() const;
    Rectangle btn(int i) const;
    Rectangle addrBox() const;
    Rectangle quitBtn() const;
    void joinDirect();
    void updateConnecting(Vector2 m, bool click);
    void updateTextFields();
    void onClick(Vector2 m);
    void clickList(Vector2 m);
    void drawTitle(const Rectangle &r);
    void drawTopBar(Vector2 m);
    void drawServerList(Vector2 m);
    void drawServerRow(int i, const Rectangle &rr, Vector2 m);
    void drawButtons(Vector2 m);
    void drawFooter(const Rectangle &r);
    void drawConnecting(Vector2 m);

    App &app;
    TMap mMenu;
    int tab = 0, sel = -1, focus = 0; // tab, selected row, focused text field (0 none, 2 address)
    double lastClick = 0;
    std::vector<ServerEntry> list;
};
