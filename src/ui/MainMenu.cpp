#include "ui/MainMenu.hpp"

#include "client/App.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

void MainMenu::load() { mMenu = loadTMap("main_menu"); }

Rectangle MainMenu::rect() const
{
    Vector2 s = mMenu.size();
    return {(float(ui::screenW()) - s.x) / 2, (float(ui::screenH()) - s.y) / 2 + 16, s.x, s.y};
}
Rectangle MainMenu::tabBtn(int i) const
{
    Rectangle r = rect();
    float pad = float(mMenu.padding);
    return {r.x + pad + 8 + float(i) * 110, r.y + pad + 6, 104, 26};
}
Rectangle MainMenu::nameBox() const
{
    Rectangle r = rect();
    float pad = float(mMenu.padding);
    return {r.x + r.width - pad - 210, r.y + pad + 6, 202, 26};
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
    std::stable_sort(list.begin() + (tab == 0 ? 1 : 0), list.end(), [](const ServerEntry &a, const ServerEntry &b)
                     { return a.ping < b.ping; });
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
        if (IsKeyPressed(KEY_ESCAPE) || (click && CheckCollisionPointRec(m, btn(0))))
            s.backToMenu("Connection cancelled.");
        else if (GetTime() - s.connectStart > 15)
            s.backToMenu("Could not connect to " + s.connectingTo.name + ".");
        return;
    }
    buildList();
    if (focus == 1)
    {
        std::string n = app.world.myName;
        if (ui::textInput(n, 20))
        {
            app.world.myName = n;
            app.settings.name = n;
        }
    }
    else if (focus == 2)
        ui::textInput(directAddr, 40);
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
    if (CheckCollisionPointRec(m, nameBox()))
    {
        focus = 1;
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
    { // Join / Connect
        if (tab == 2 && !directAddr.empty())
        {
            joinDirect();
            return;
        }
        if (sel >= 0 && sel < int(list.size()))
            s.joinServer(list[size_t(sel)]);
        return;
    }
    Rectangle la = listArea();
    if (tab == 2)
        la.y += 32;
    if (CheckCollisionPointRec(m, la))
    {
        int row = int((m.y - la.y) / 28.f);
        if (row >= 0 && row < int(list.size()))
        {
            if (row == sel && GetTime() - lastClick < .45)
            {
                s.joinServer(list[size_t(row)]);
                return;
            }
            sel = row;
            lastClick = GetTime();
        }
    }
}

void MainMenu::draw()
{
    const Session &s = app.session;
    Vector2 m = ui::mouse();
    Rectangle r = rect();
    drawTMap(mMenu, r);
    Rectangle tr{r.x + r.width / 2 - 200, r.y - 46, 400, 80};
    drawTMap(ui::titleMap(), {tr.x, tr.y, tr.width, tr.height});
    DrawText("BASE WARS", int(tr.x + tr.width / 2) - MeasureText("BASE WARS", 34) / 2 + 2, int(tr.y) + 22, 34, rgb(20, 40, 60));
    DrawText("BASE WARS", int(tr.x + tr.width / 2) - MeasureText("BASE WARS", 34) / 2, int(tr.y) + 20, 34, rgb(255, 245, 210));
    static const char *tabs[3] = {"Internet", "Friends", "Direct"};
    for (int i = 0; i < 3; i++)
        ui::drawButton(tabBtn(i), tabs[i], true, false, CheckCollisionPointRec(m, tabBtn(i)), tab == i, 13);
    ui::textBox(nameBox(), app.world.myName, "Your name", focus == 1);
    DrawText("Name:", int(nameBox().x) - 46, int(nameBox().y) + 6, 13, rgb(70, 45, 30));
    Rectangle la = listArea();
    if (tab == 2)
    {
        ui::textBox(addrBox(), directAddr, "address:port, e.g. 127.0.0.1:7777", focus == 2);
        la.y += 32;
        la.height -= 32;
    }
    // header
    DrawText("Server", int(la.x) + 8, int(la.y) - 18, 12, rgb(110, 80, 50));
    DrawText("Players", int(la.x + la.width) - 200, int(la.y) - 18, 12, rgb(110, 80, 50));
    DrawText("Ping", int(la.x + la.width) - 110, int(la.y) - 18, 12, rgb(110, 80, 50));
    DrawText("Via", int(la.x + la.width) - 54, int(la.y) - 18, 12, rgb(110, 80, 50));
    int rows = int(la.height / 28.f);
    if (list.empty() || (tab < 2 && !s.steam.ok && list.size() <= 1))
    {
        std::string hint = tab == 2 ? "Type an address above and press Connect." : (s.steam.ok ? (s.browser.loading() ? "Searching for servers..." : "No servers found. Press Refresh.") : "Steam is not running or not built in: use the local server or the Direct tab.");
        DrawText(hint.c_str(), int(la.x) + 10, int(la.y) + 6 + (list.empty() ? 0 : 28), 14, rgb(120, 90, 60));
    }
    for (int i = 0; i < rows && i < int(list.size()); i++)
    {
        const ServerEntry &e = list[size_t(i)];
        Rectangle rr{la.x, la.y + float(i) * 28.f, la.width, 28};
        bool selected = i == sel, hov = CheckCollisionPointRec(m, rr);
        DrawRectangleRec(rr, selected ? rgb(120, 180, 240, 160) : (hov ? rgb(255, 255, 255, 90) : (i % 2 ? rgb(0, 0, 0, 14) : rgb(255, 255, 255, 40))));
        DrawText(e.name.c_str(), int(rr.x) + 8, int(rr.y) + 6, 15, rgb(60, 40, 28));
        if (!e.local)
        {
            DrawText(TextFormat("%d / %d", e.players, e.maxPlayers), int(rr.x + rr.width) - 200, int(rr.y) + 7, 14, rgb(60, 40, 28));
            DrawText(TextFormat("%d ms", e.ping), int(rr.x + rr.width) - 110, int(rr.y) + 7, 14, e.ping < 80 ? rgb(30, 120, 40) : (e.ping < 160 ? rgb(170, 120, 20) : rgb(170, 40, 30)));
        }
        const char *via = e.friends ? "Friend" : (e.steamId ? "Steam" : "IP");
        DrawText(via, int(rr.x + rr.width) - 54, int(rr.y) + 7, 13, e.friends ? rgb(30, 130, 60) : rgb(90, 70, 50));
    }
    bool canJoin = tab == 2 ? !directAddr.empty() : (sel >= 0 && sel < int(list.size()));
    ui::drawButton(btn(0), tab == 2 ? "Connect" : "Join", canJoin, false, CheckCollisionPointRec(m, btn(0)), false, 14);
    if (tab < 2)
        ui::drawButton(btn(1), s.browser.loading() ? "Searching..." : "Refresh", s.steam.ok, false, CheckCollisionPointRec(m, btn(1)), false, 14);
    ui::drawButton(quitBtn(), "Quit", true, true, CheckCollisionPointRec(m, quitBtn()), false, 14);
    std::string st = s.steam.ok ? "Steam: " + s.steam.name : "Steam: offline";
    DrawText(st.c_str(), int(r.x) + int(mMenu.padding) + 120, int(r.y + r.height) - int(mMenu.padding) - 34, 12, s.steam.ok ? rgb(30, 110, 50) : rgb(130, 90, 60));
    if (!status.empty())
        DrawText(status.c_str(), int(r.x) + int(mMenu.padding) + 120, int(r.y + r.height) - int(mMenu.padding) - 18, 13, rgb(170, 50, 30));
    if (s.screen == Screen::Connecting)
    {
        int sw = ui::screenW(), sh = ui::screenH();
        DrawRectangle(0, 0, sw, sh, rgb(0, 0, 0, 150));
        std::string c = "Connecting to " + s.connectingTo.name + std::string(int(GetTime() * 3) % 4, '.');
        DrawText(c.c_str(), sw / 2 - MeasureText(c.c_str(), 24) / 2, sh / 2 - 20, 24, WHITE);
        ui::drawButton(btn(0), "Cancel", true, true, CheckCollisionPointRec(m, btn(0)), false, 14);
    }
}
