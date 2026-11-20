/**
 * @file MainMenu.cpp
 * @brief Main menu and server browser: Internet / Friends / Direct tabs, server list, connecting overlay.
 */
#include "ui/MainMenu.hpp"

#include "client/App.hpp"
#include "rendering/Text.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

void MainMenu::load()
{
    mMenu = loadTMap("main_menu");
}

Rectangle MainMenu::rect() const
{
    Vector2 s = mMenu.size();
    return {(float(ui::screenW()) - s.x) / 2, (float(ui::screenH()) - s.y) / 2 + 16, s.x, s.y};
}
Rectangle MainMenu::tabBtn(int i) const
{
    Rectangle r = rect();
    float pad = float(mMenu.padding);
    return {r.x + pad + 8 + float(i) * 118, r.y + pad + 4, 112, 30};
}
Rectangle MainMenu::nameBox() const
{
    Rectangle r = rect();
    float pad = float(mMenu.padding);
    return {r.x + r.width - pad - 250, r.y + pad + 4, 242, 30};
}
Rectangle MainMenu::listArea() const
{
    Rectangle r = rect();
    float pad = float(mMenu.padding);
    return {r.x + pad + 8, r.y + pad + 66, r.width - 2 * pad - 16, r.height - 2 * pad - 66 - 62};
}
Rectangle MainMenu::btn(int i) const
{
    Rectangle r = rect();
    float pad = float(mMenu.padding);
    return {r.x + r.width - pad - 8 - 120 * float(i + 1) - 6 * float(i), r.y + r.height - pad - 40, 120, 30};
}
Rectangle MainMenu::addrBox() const
{
    Rectangle r = rect();
    float pad = float(mMenu.padding);
    return {r.x + pad + 8, r.y + pad + 36, r.width - 2 * pad - 16, 24};
}
Rectangle MainMenu::quitBtn() const
{
    Rectangle r = rect();
    float pad = float(mMenu.padding);
    return {r.x + pad + 8, r.y + r.height - pad - 40, 100, 30};
}

void MainMenu::refresh()
{
    sel = -1;
    if (app.session.steam.ok && tab < 2)
        app.session.browser.refresh(tab == 1);
}

void MainMenu::buildList()
{
    const Session &s = app.session;
    list.clear();
    if (tab == 2)
    { // direct connect: remembered servers
        ServerEntry e;
        e.name = "Last server: " + app.settings.lastServer;
        e.address = app.settings.lastServer;
        e.local = true;
        list.push_back(e);
        ServerEntry cfg;
        cfg.name = "Configured server (config.txt): " + s.ip + ":" + std::to_string(s.port);
        cfg.address = s.ip;
        cfg.port = uint16_t(s.port);
        cfg.local = true;
        list.push_back(cfg);
        return;
    }
    if (tab == 0)
    { // a local server is always offered so offline play works without Steam
        ServerEntry e;
        e.name = "Local server (this computer)";
        e.address = "127.0.0.1";
        e.port = DEFAULT_PORT;
        e.local = true;
        list.push_back(e);
    }
    for (auto &e : s.browser.entries())
        if (e.friends == (tab == 1))
            list.push_back(e);
    std::stable_sort(list.begin() + (tab == 0 ? 1 : 0), list.end(),
                     [](const ServerEntry &a, const ServerEntry &b) { return a.ping < b.ping; });
}

void MainMenu::joinDirect()
{
    ServerEntry e;
    e.name = directAddr;
    e.address = directAddr;
    e.local = true;
    app.session.joinServer(e);
}

void MainMenu::update()
{
    Session &s = app.session;
    Vector2 m = ui::mouse();
    bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    if (s.screen == Screen::Connecting)
    {
        updateConnecting(m, click);
        return;
    }

    buildList();
    updateTextFields();

    if (IsKeyPressed(KEY_ESCAPE))
    {
        if (focus)
            focus = 0;
        return;
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
    {
        if (focus == 2 && !directAddr.empty())
        {
            joinDirect();
            return;
        }
        focus = 0;
    }
    if (!click)
        return;

    focus = 0;
    onClick(m);
}

void MainMenu::updateConnecting(Vector2 m, bool click)
{
    Session &s = app.session;
    if (IsKeyPressed(KEY_ESCAPE) || (click && CheckCollisionPointRec(m, btn(0))))
        s.backToMenu("Connection cancelled.");
    else if (GetTime() - s.connectStart > 15)
        s.backToMenu("Could not connect to " + s.connectingTo.name + ".");
}

void MainMenu::updateTextFields()
{
    if (focus == 2)
        ui::textInput(directAddr, 40);
}

void MainMenu::onClick(Vector2 m)
{
    Session &s = app.session;
    if (CheckCollisionPointRec(m, quitBtn()))
    {
        app.wantQuit = true;
        return;
    }
    for (int i = 0; i < 3; i++)
        if (CheckCollisionPointRec(m, tabBtn(i)))
        {
            tab = i;
            refresh();
            return;
        }
    if (tab == 2 && CheckCollisionPointRec(m, addrBox()))
    {
        focus = 2;
        return;
    }
    if (CheckCollisionPointRec(m, btn(1)) && tab < 2)
    {
        refresh();
        return;
    }
    if (CheckCollisionPointRec(m, btn(0)))
    {
        if (tab == 2 && !directAddr.empty())
        {
            joinDirect();
            return;
        }
        if (sel >= 0 && sel < int(list.size()))
            s.joinServer(list[size_t(sel)]);
        return;
    }
    clickList(m);
}

void MainMenu::clickList(Vector2 m)
{
    Rectangle la = listArea();
    if (tab == 2)
        la.y += 32;
    if (!CheckCollisionPointRec(m, la))
        return;

    int row = int((m.y - la.y) / 28.f);
    if (row < 0 || row >= int(list.size()))
        return;

    if (row == sel && GetTime() - lastClick < .45)
    {
        app.session.joinServer(list[size_t(row)]);
        return;
    }
    sel = row;
    lastClick = GetTime();
}

void MainMenu::draw()
{
    const Session &s = app.session;
    Vector2 m = ui::mouse();
    Rectangle r = rect();

    drawTMap(mMenu, r);
    drawTitle(r);
    drawTopBar(m);
    drawServerList(m);
    drawButtons(m);
    drawFooter(r);

    if (s.screen == Screen::Connecting)
        drawConnecting(m);
}

void MainMenu::drawTitle(const Rectangle &r)
{
    Rectangle tr{r.x + r.width / 2 - 200, r.y - 46, 400, 80};
    drawTMap(ui::titleMap(), {tr.x, tr.y, tr.width, tr.height});

    // Drop shadow first, then the lit text one pixel up-left.
    drawText("BASE WARS", int(tr.x + tr.width / 2) - measureText("BASE WARS", 34, TextWeight::Bold) / 2 + 2,
             int(tr.y) + 22, 34, rgb(20, 40, 60), TextWeight::Bold);
    drawText("BASE WARS", int(tr.x + tr.width / 2) - measureText("BASE WARS", 34, TextWeight::Bold) / 2, int(tr.y) + 20,
             34, rgb(255, 245, 210), TextWeight::Bold);
}

void MainMenu::drawTopBar(Vector2 m)
{
    static const char *tabs[3] = {"Internet", "Friends", "Direct"};
    for (int i = 0; i < 3; i++)
        ui::drawButton(tabBtn(i), tabs[i], true, false, CheckCollisionPointRec(m, tabBtn(i)), tab == i, 16);

    // The name is the Steam persona of the account that will own the town; it cannot be edited here.
    const Session &s = app.session;
    std::string who = s.devId ? "Dev " + std::to_string(s.devId) : (s.steam.ok ? s.steam.name : "Steam is not running");
    drawText(("Playing as: " + who).c_str(), int(nameBox().x) + 4, int(nameBox().y) + 6, 17, rgb(70, 45, 30));
}

void MainMenu::drawServerList(Vector2 m)
{
    const Session &s = app.session;
    Rectangle la = listArea();
    if (tab == 2)
    {
        ui::textBox(addrBox(), directAddr, "address:port, e.g. 127.0.0.1:7777", focus == 2);
        la.y += 32;
        la.height -= 32;
    }

    drawText("Server", int(la.x) + 8, int(la.y) - 22, 14, rgb(110, 80, 50));
    drawText("Players", int(la.x + la.width) - 200, int(la.y) - 22, 14, rgb(110, 80, 50));
    drawText("Ping", int(la.x + la.width) - 110, int(la.y) - 22, 14, rgb(110, 80, 50));
    drawText("Via", int(la.x + la.width) - 54, int(la.y) - 22, 14, rgb(110, 80, 50));

    if (list.empty() || (tab < 2 && !s.steam.ok && list.size() <= 1))
    {
        std::string hint =
            tab == 2
                ? "Type an address above and press Connect."
                : (s.steam.ok ? (s.browser.loading() ? "Searching for servers..." : "No servers found. Press Refresh.")
                              : "Steam is not running or not built in: use the local server or the Direct tab.");
        drawText(hint.c_str(), int(la.x) + 10, int(la.y) + 6 + (list.empty() ? 0 : 28), 16, rgb(120, 90, 60));
    }

    int rows = int(la.height / 28.f);
    for (int i = 0; i < rows && i < int(list.size()); i++)
        drawServerRow(i, {la.x, la.y + float(i) * 28.f, la.width, 28}, m);
}

void MainMenu::drawServerRow(int i, const Rectangle &rr, Vector2 m)
{
    const ServerEntry &e = list[size_t(i)];
    bool selected = i == sel, hov = CheckCollisionPointRec(m, rr);
    DrawRectangleRec(rr, selected
                             ? rgb(120, 180, 240, 160)
                             : (hov ? rgb(255, 255, 255, 90) : (i % 2 ? rgb(0, 0, 0, 14) : rgb(255, 255, 255, 40))));
    drawText(e.name.c_str(), int(rr.x) + 8, int(rr.y) + 5, 17, rgb(60, 40, 28));

    if (!e.local)
    {
        drawText(TextFormat("%d / %d", e.players, e.maxPlayers), int(rr.x + rr.width) - 200, int(rr.y) + 6, 16,
                 rgb(60, 40, 28));
        drawText(TextFormat("%d ms", e.ping), int(rr.x + rr.width) - 110, int(rr.y) + 6, 16,
                 e.ping < 80 ? rgb(30, 120, 40) : (e.ping < 160 ? rgb(170, 120, 20) : rgb(170, 40, 30)));
    }

    const char *via = e.friends ? "Friend" : (e.steamId ? "Steam" : "IP");
    drawText(via, int(rr.x + rr.width) - 54, int(rr.y) + 6, 15, e.friends ? rgb(30, 130, 60) : rgb(90, 70, 50));
}

void MainMenu::drawButtons(Vector2 m)
{
    const Session &s = app.session;
    bool canJoin = tab == 2 ? !directAddr.empty() : (sel >= 0 && sel < int(list.size()));
    ui::drawButton(btn(0), tab == 2 ? "Connect" : "Join", canJoin, false, CheckCollisionPointRec(m, btn(0)), false, 17);
    if (tab < 2)
        ui::drawButton(btn(1), s.browser.loading() ? "Searching..." : "Refresh", s.steam.ok, false,
                       CheckCollisionPointRec(m, btn(1)), false, 17);
    ui::drawButton(quitBtn(), "Quit", true, true, CheckCollisionPointRec(m, quitBtn()), false, 17);
}

void MainMenu::drawFooter(const Rectangle &r)
{
    const Session &s = app.session;
    std::string st = s.steam.ok ? "Steam: " + s.steam.name : "Steam: offline";
    drawText(st.c_str(), int(r.x) + int(mMenu.padding) + 120, int(r.y + r.height) - int(mMenu.padding) - 38, 15,
             s.steam.ok ? rgb(30, 110, 50) : rgb(130, 90, 60));
    if (!status.empty())
        drawText(status.c_str(), int(r.x) + int(mMenu.padding) + 120, int(r.y + r.height) - int(mMenu.padding) - 20, 15,
                 rgb(170, 50, 30));
}

void MainMenu::drawConnecting(Vector2 m)
{
    const Session &s = app.session;
    int sw = ui::screenW(), sh = ui::screenH();
    DrawRectangle(0, 0, sw, sh, rgb(0, 0, 0, 150));

    std::string c = "Connecting to " + s.connectingTo.name + std::string(int(GetTime() * 3) % 4, '.');
    drawText(c.c_str(), sw / 2 - measureText(c.c_str(), 24) / 2, sh / 2 - 20, 24, WHITE);
    ui::drawButton(btn(0), "Cancel", true, true, CheckCollisionPointRec(m, btn(0)), false, 17);
}
