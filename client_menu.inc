// Main menu and server browser. Included inside `struct App` (client.cpp) so it shares the App's state.
// Layout: animated island backdrop, a central paper panel with Internet / Friends / Direct tabs and the server list.

// ---- menu state
enum class Screen { Menu, Connecting, Playing };
Screen screen = Screen::Menu;
int menuTab = 0, menuSel = -1, menuFocus = 0;          // tab, selected row, focused text field (0 none, 1 name, 2 address)
double menuClick = 0, connectStart = 0;
std::string menuStatus, directAddr;
bool wantQuit = false;
std::vector<ServerEntry> menuList;
ServerEntry connectingTo;
steamlayer::Client steam;
steamlayer::Browser browser;
ClientNet cnet;
TMap mMenu;

Rectangle menuRect() { Vector2 s = mMenu.size(); return {(float(SW()) - s.x) / 2, (float(SH()) - s.y) / 2 + 16, s.x, s.y}; }
Rectangle menuTabBtn(int i) { Rectangle r = menuRect(); float pad = float(mMenu.padding); return {r.x + pad + 8 + float(i) * 110, r.y + pad + 6, 104, 26}; }
Rectangle menuNameBox() { Rectangle r = menuRect(); float pad = float(mMenu.padding); return {r.x + r.width - pad - 210, r.y + pad + 6, 202, 26}; }
Rectangle menuListArea() { Rectangle r = menuRect(); float pad = float(mMenu.padding); return {r.x + pad + 8, r.y + pad + 66, r.width - 2 * pad - 16, r.height - 2 * pad - 66 - 62}; }
Rectangle menuBtn(int i) { Rectangle r = menuRect(); float pad = float(mMenu.padding); return {r.x + r.width - pad - 8 - 120 * float(i + 1) - 6 * float(i), r.y + r.height - pad - 40, 120, 30}; }
Rectangle menuAddrBox() { Rectangle r = menuRect(); float pad = float(mMenu.padding); return {r.x + pad + 8, r.y + pad + 36, r.width - 2 * pad - 16, 24}; }
Rectangle menuQuitBtn() { Rectangle r = menuRect(); float pad = float(mMenu.padding); return {r.x + pad + 8, r.y + r.height - pad - 40, 100, 30}; }

std::string menuDefaultName() { return myName.empty() ? (steam.ok ? steam.name : std::string("Player")) : myName; }

void menuRefresh() {
    menuSel = -1;
    if (steam.ok && menuTab < 2) browser.refresh(menuTab == 1);
}
void buildMenuList() {
    menuList.clear();
    if (menuTab == 2) {                                   // direct connect: remembered servers
        ServerEntry e;
        e.name = "Last server: " + settings.lastServer;
        e.address = settings.lastServer;
        e.local = true;
        menuList.push_back(e);
        ServerEntry cfg;
        cfg.name = "Configured server (config.txt): " + ip + ":" + std::to_string(port);
        cfg.address = ip;
        cfg.port = uint16_t(port);
        cfg.local = true;
        menuList.push_back(cfg);
        return;
    }
    if (menuTab == 0) {                                   // a local server is always offered so offline play works without Steam
        ServerEntry e;
        e.name = "Local server (this computer)";
        e.address = "127.0.0.1";
        e.port = DEFAULT_PORT;
        e.local = true;
        menuList.push_back(e);
    }
    for (auto& e : browser.entries()) if (e.friends == (menuTab == 1)) menuList.push_back(e);
    std::stable_sort(menuList.begin() + (menuTab == 0 ? 1 : 0), menuList.end(), [](const ServerEntry& a, const ServerEntry& b) { return a.ping < b.ping; });
}
static bool parseAddr(const std::string& s, std::string& host, uint16_t& port) {
    auto c = s.rfind(':');
    host = c == std::string::npos ? s : s.substr(0, c);
    int p = c == std::string::npos ? DEFAULT_PORT : std::atoi(s.c_str() + c + 1);
    port = uint16_t(p > 0 && p < 65536 ? p : DEFAULT_PORT);
    return !host.empty();
}
void resetWorld() {
    reg.clear();
    ents.clear();
    chunks.clear();
    occ.clear();
    roster.clear();
    clansList.clear();
    facTowns.clear();
    sel.clear();
    fxs.clear();
    ghosts.clear();
    chatLines.clear();
    logs.clear();
    markers.clear();
    ripples.clear();
    ctxOpen = trainOpen = socialOpen = bigMapOpen = chatOpen = false;
    buildKind = -1;
    mode = M_None;
    welcomed = lost = connected = helloSent = false;
    me = 0;
    hovered = 0;
    stamp = 0;
}
void joinServer(const ServerEntry& e) {
    resetWorld();
    connectingTo = e;
    bool ok;
    if (e.steamId && steam.ok) ok = cnet.connectSteam(e.steamId);
    else {
        std::string host = e.address;
        uint16_t pt = e.port;
        if (e.local && e.address.find(':') != std::string::npos) parseAddr(e.address, host, pt);
        ok = cnet.connectIP(host, pt);
        settings.lastServer = host + ":" + std::to_string(pt);
    }
    if (!ok) { menuStatus = "Could not start a connection to " + e.name; return; }
    screen = Screen::Connecting;
    connectStart = GetTime();
    menuStatus.clear();
}
void backToMenu(const std::string& why) {
    cnet.close();
    resetWorld();
    screen = Screen::Menu;
    menuStatus = why;
    SetWindowTitle("Base Wars");
    menuRefresh();
}
void sendHello() {
    Writer w;
    w.put<uint8_t>(C_Hello);
    w.str(uuid);
    w.str(myName);
    w.put<uint64_t>(steam.ok ? steam.id : 0);
    w.put<uint16_t>(uint16_t(steam.friends.size()));
    for (uint64_t f : steam.friends) w.put<uint64_t>(f);
    send(w);
}
// drift of tiny islands behind the menu
void drawMenuBackdrop(float t) {
    cam.target = {std::sin(t * .05f) * 200.f + 640.f, std::cos(t * .04f) * 120.f + 360.f};
    cam.offset = {float(GetScreenWidth()) / 2, float(GetScreenHeight()) / 2};
    cam.zoom = 1.f;
    drawWater(t);
    BeginMode2D(cam);
    Texture2D grass = tex("Terrain/Tileset/Tilemap_color1.png"), foam = tex("Terrain/Tileset/Water Foam.png");
    auto patch = [&](int x0, int y0, int w, int h) {
        for (int y = y0 - 1; y <= y0 + h; y++) for (int x = x0 - 1; x <= x0 + w; x++) {
            int fr = int(t * 10.f + float(hash2(x, y, 4) % 16)) % 16;
            DrawTexturePro(foam, {float(fr * 192), 0, 192, 192}, {float(x) * TILE - 64, float(y) * TILE - 64, 192, 192}, {0, 0}, 0, WHITE);
        }
        for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) {
            int c = x == x0 ? 0 : (x == x0 + w - 1 ? 2 : 1), r = y == y0 ? 0 : (y == y0 + h - 1 ? 2 : 1);
            DrawTexturePro(grass, {float(c * 64), float(r * 64), 64, 64}, {float(x) * TILE, float(y) * TILE, TILE, TILE}, {0, 0}, 0, WHITE);
        }
    };
    patch(-2, 7, 8, 5);
    patch(15, 0, 7, 5);
    patch(-3, -2, 5, 4);
    patch(15, 8, 6, 4);
    auto sprite = [&](const std::string& file, float cx, float bottom, float w, float h, float scale, float ab) {
        DrawTexturePro(tex(file), {0, 0, w, h}, {cx - w * scale / 2, bottom - ab * scale, w * scale, h * scale}, {0, 0}, 0, WHITE);
    };
    sprite("Buildings/Blue Buildings/Castle.png", 1.5f * TILE, 11.6f * TILE, 320, 256, .8f, 249);
    sprite("Buildings/Red Buildings/Archery.png", 18.5f * TILE, 4.6f * TILE, 192, 256, 1.f, 240);
    sprite("Buildings/Yellow Buildings/House1.png", -.5f * TILE, 1.4f * TILE, 128, 192, .9f, 173);
    sprite("Buildings/Purple Buildings/Tower.png", 18.f * TILE, 11.4f * TILE, 128, 256, .8f, 230);
    auto unit = [&](const std::string& rel, int fw, int frames, float fps, float x, float y, bool flip, float phase) {
        Anim a = mkAnim(rel, fw, fw, fps, float(fw) / 2, fw == 320 ? 198.f : 135.f);
        drawAnim(a, int(std::fmod(t * fps + phase, float(frames))) % frames, x, y, flip, 1.f);
    };
    unit("Units/Blue Units/Warrior/Warrior_Idle.png", 192, 8, 7, 0.2f * TILE, 11.9f * TILE, false, 0);
    unit("Units/Blue Units/Archer/Archer_Idle.png", 192, 6, 7, 3.4f * TILE, 11.7f * TILE, true, 2);
    unit("Units/Blue Units/Pawn/Pawn_Idle.png", 192, 8, 7, 2.2f * TILE, 12.1f * TILE, false, 3);
    unit("Units/Red Units/Lancer/Lancer_Idle.png", 320, 12, 9, 16.6f * TILE, 4.9f * TILE, false, 1);
    unit("Units/Red Units/Monk/Idle.png", 192, 6, 7, 20.3f * TILE, 4.7f * TILE, true, 4);
    unit("Units/Purple Units/Warrior/Warrior_Idle.png", 192, 8, 7, 16.f * TILE, 11.6f * TILE, false, 5);
    for (int i = 0; i < 4; i++) {                         // sheep wandering on the top-left islet
        Anim a = mkAnim("Terrain/Resources/Meat/Sheep/Sheep_Grass.png", 128, 128, 7, 64, 84);
        drawAnim(a, int(t * 7.f + float(i) * 3) % a.n, (-2.4f + float(i) * .9f) * TILE, (0.2f + float(i % 2) * .6f) * TILE, i % 2, 1.f);
    }
    EndMode2D();
    for (int i = 0; i < 7; i++) {                         // drifting clouds
        Texture2D c = tex(std::string("Terrain/Decorations/Clouds/Clouds_0") + char('1' + i % 8) + ".png");
        float x = std::fmod(float(i) * 311.f + t * (14.f + float(i) * 3.f), float(GetScreenWidth()) + 700.f) - 600.f;
        DrawTexturePro(c, {0, 0, 576, 256}, {x, 30.f + float((i * 97) % 520), 576, 256}, {0, 0}, 0, rgb(255, 255, 255, 120));
    }
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), rgb(10, 20, 40, 60));
}

bool menuTextInput(std::string& target, size_t maxLen) {
    bool changed = false;
    for (int ch = GetCharPressed(); ch > 0; ch = GetCharPressed()) if (ch >= 32 && ch < 127 && target.size() < maxLen) { target.push_back(char(ch)); changed = true; }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !target.empty()) { target.pop_back(); changed = true; }
    return changed;
}
void menuTextBox(Rectangle r, const std::string& text, const std::string& hint, bool focus) {
    DrawRectangleRounded(r, .3f, 6, rgb(255, 250, 235, 235));
    DrawRectangleRoundedLinesEx(r, .3f, 6, 2, focus ? rgb(60, 120, 200) : rgb(120, 90, 60));
    std::string shown = text.empty() && !focus ? hint : text + (focus && int(GetTime() * 2) % 2 == 0 ? "|" : "");
    DrawText(shown.c_str(), int(r.x) + 8, int(r.y) + 5, 15, text.empty() && !focus ? rgb(150, 130, 110) : rgb(70, 45, 30));
}

void menuUpdate() {
    Vector2 m = MP();
    bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (screen == Screen::Connecting) {
        if (IsKeyPressed(KEY_ESCAPE) || (click && CheckCollisionPointRec(m, menuBtn(0)))) backToMenu("Connection cancelled.");
        else if (GetTime() - connectStart > 15) backToMenu("Could not connect to " + connectingTo.name + ".");
        return;
    }
    buildMenuList();
    if (menuFocus == 1) { std::string n = myName; if (menuTextInput(n, 20)) { myName = n; settings.name = n; } }
    else if (menuFocus == 2) menuTextInput(directAddr, 40);
    if (IsKeyPressed(KEY_ESCAPE)) { if (menuFocus) menuFocus = 0; return; }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        if (menuFocus == 2 && !directAddr.empty()) { ServerEntry e; e.name = directAddr; e.address = directAddr; e.local = true; joinServer(e); return; }
        menuFocus = 0;
    }
    if (!click) return;
    menuFocus = 0;
    if (CheckCollisionPointRec(m, menuQuitBtn())) { wantQuit = true; return; }
    for (int i = 0; i < 3; i++) if (CheckCollisionPointRec(m, menuTabBtn(i))) { menuTab = i; menuRefresh(); return; }
    if (CheckCollisionPointRec(m, menuNameBox())) { menuFocus = 1; return; }
    if (menuTab == 2 && CheckCollisionPointRec(m, menuAddrBox())) { menuFocus = 2; return; }
    if (CheckCollisionPointRec(m, menuBtn(1)) && menuTab < 2) { menuRefresh(); return; }
    if (CheckCollisionPointRec(m, menuBtn(0))) {                                  // Join / Connect
        if (menuTab == 2 && !directAddr.empty()) { ServerEntry e; e.name = directAddr; e.address = directAddr; e.local = true; joinServer(e); return; }
        if (menuSel >= 0 && menuSel < int(menuList.size())) joinServer(menuList[size_t(menuSel)]);
        return;
    }
    Rectangle la = menuListArea();
    if (menuTab == 2) la.y += 32;
    if (CheckCollisionPointRec(m, la)) {
        int row = int((m.y - la.y) / 28.f);
        if (row >= 0 && row < int(menuList.size())) {
            if (row == menuSel && GetTime() - menuClick < .45) { joinServer(menuList[size_t(row)]); return; }
            menuSel = row;
            menuClick = GetTime();
        }
    }
}

void drawMenu(float t) {
    drawMenuBackdrop(t);
    Vector2 m = MP();
    Rectangle r = menuRect();
    drawTMap(mMenu, r);
    Rectangle tr{r.x + r.width / 2 - 200, r.y - 46, 400, 80};
    drawTMap(mTitle, {tr.x, tr.y, tr.width, tr.height});
    DrawText("BASE WARS", int(tr.x + tr.width / 2) - MeasureText("BASE WARS", 34) / 2 + 2, int(tr.y) + 22, 34, rgb(20, 40, 60));
    DrawText("BASE WARS", int(tr.x + tr.width / 2) - MeasureText("BASE WARS", 34) / 2, int(tr.y) + 20, 34, rgb(255, 245, 210));
    static const char* tabs[3] = {"Internet", "Friends", "Direct"};
    for (int i = 0; i < 3; i++) drawButton(menuTabBtn(i), tabs[i], true, false, CheckCollisionPointRec(m, menuTabBtn(i)), menuTab == i, 13);
    menuTextBox(menuNameBox(), myName, "Your name", menuFocus == 1);
    DrawText("Name:", int(menuNameBox().x) - 46, int(menuNameBox().y) + 6, 13, rgb(70, 45, 30));
    Rectangle la = menuListArea();
    if (menuTab == 2) {
        menuTextBox(menuAddrBox(), directAddr, "address:port, e.g. 127.0.0.1:7777", menuFocus == 2);
        la.y += 32;
        la.height -= 32;
    }
    // header
    DrawText("Server", int(la.x) + 8, int(la.y) - 18, 12, rgb(110, 80, 50));
    DrawText("Players", int(la.x + la.width) - 200, int(la.y) - 18, 12, rgb(110, 80, 50));
    DrawText("Ping", int(la.x + la.width) - 110, int(la.y) - 18, 12, rgb(110, 80, 50));
    DrawText("Via", int(la.x + la.width) - 54, int(la.y) - 18, 12, rgb(110, 80, 50));
    int rows = int(la.height / 28.f);
    if (menuList.empty() || (menuTab < 2 && !steam.ok && menuList.size() <= 1)) {
        std::string hint = menuTab == 2 ? "Type an address above and press Connect." : (steam.ok ? (browser.loading() ? "Searching for servers..." : "No servers found. Press Refresh.")
                                                                                            : "Steam is not running or not built in: use the local server or the Direct tab.");
        DrawText(hint.c_str(), int(la.x) + 10, int(la.y) + 6 + (menuList.empty() ? 0 : 28), 14, rgb(120, 90, 60));
    }
    for (int i = 0; i < rows && i < int(menuList.size()); i++) {
        const ServerEntry& e = menuList[size_t(i)];
        Rectangle rr{la.x, la.y + float(i) * 28.f, la.width, 28};
        bool sel = i == menuSel, hov = CheckCollisionPointRec(m, rr);
        DrawRectangleRec(rr, sel ? rgb(120, 180, 240, 160) : (hov ? rgb(255, 255, 255, 90) : (i % 2 ? rgb(0, 0, 0, 14) : rgb(255, 255, 255, 40))));
        DrawText(e.name.c_str(), int(rr.x) + 8, int(rr.y) + 6, 15, rgb(60, 40, 28));
        if (!e.local) {
            DrawText(TextFormat("%d / %d", e.players, e.maxPlayers), int(rr.x + rr.width) - 200, int(rr.y) + 7, 14, rgb(60, 40, 28));
            DrawText(TextFormat("%d ms", e.ping), int(rr.x + rr.width) - 110, int(rr.y) + 7, 14, e.ping < 80 ? rgb(30, 120, 40) : (e.ping < 160 ? rgb(170, 120, 20) : rgb(170, 40, 30)));
        }
        const char* via = e.friends ? "Friend" : (e.steamId ? "Steam" : "IP");
        DrawText(via, int(rr.x + rr.width) - 54, int(rr.y) + 7, 13, e.friends ? rgb(30, 130, 60) : rgb(90, 70, 50));
    }
    bool canJoin = menuTab == 2 ? !directAddr.empty() : (menuSel >= 0 && menuSel < int(menuList.size()));
    drawButton(menuBtn(0), menuTab == 2 ? "Connect" : "Join", canJoin, false, CheckCollisionPointRec(m, menuBtn(0)), false, 14);
    if (menuTab < 2) drawButton(menuBtn(1), browser.loading() ? "Searching..." : "Refresh", steam.ok, false, CheckCollisionPointRec(m, menuBtn(1)), false, 14);
    drawButton(menuQuitBtn(), "Quit", true, true, CheckCollisionPointRec(m, menuQuitBtn()), false, 14);
    std::string st = steam.ok ? "Steam: " + steam.name : "Steam: offline";
    DrawText(st.c_str(), int(r.x) + int(mMenu.padding) + 120, int(r.y + r.height) - int(mMenu.padding) - 34, 12, steam.ok ? rgb(30, 110, 50) : rgb(130, 90, 60));
    if (!menuStatus.empty()) DrawText(menuStatus.c_str(), int(r.x) + int(mMenu.padding) + 120, int(r.y + r.height) - int(mMenu.padding) - 18, 13, rgb(170, 50, 30));
    if (screen == Screen::Connecting) {
        DrawRectangle(0, 0, SW(), SH(), rgb(0, 0, 0, 150));
        std::string s = "Connecting to " + connectingTo.name + std::string(int(GetTime() * 3) % 4, '.');
        DrawText(s.c_str(), SW() / 2 - MeasureText(s.c_str(), 24) / 2, SH() / 2 - 20, 24, WHITE);
        drawButton(menuBtn(0), "Cancel", true, true, CheckCollisionPointRec(m, menuBtn(0)), false, 14);
    }
}
