/**
 * @file MainMenu.hpp
 * @brief Main menu and server browser: Internet / Friends / Direct tabs, name box, server list, connecting overlay.
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

    App &app;
    TMap mMenu;
    int tab = 0, sel = -1, focus = 0; // tab, selected row, focused text field (0 none, 1 name, 2 address)
    double lastClick = 0;
    std::vector<ServerEntry> list;
};
