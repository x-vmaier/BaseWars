/**
 * @file ClientMain.cpp
 * @brief Client entry point: window setup, config.txt / settings.txt / profile loading, optional auto-join.
 */
#include "client/App.hpp"

#include <raylib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace
{

void loadConfig(std::string &ip, int &port, std::string &name)
{
    ip = "127.0.0.1";
    port = DEFAULT_PORT;

    std::ifstream f(std::string(GetApplicationDirectory()) + "config.txt");
    if (!f)
        f.open("config.txt");
    if (!f)
    {
        TraceLog(LOG_WARNING, "config.txt not found next to the executable; using %s:%d", ip.c_str(), port);
        return;
    }

    auto trim = [](const std::string &s)
    {
        auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos)
            return std::string();
        return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
    };

    std::string ln;
    while (std::getline(f, ln))
    {
        auto p = ln.find('=');
        if (p == std::string::npos || ln[0] == '#')
            continue;

        std::string k = trim(ln.substr(0, p)), v = trim(ln.substr(p + 1));
        if (k == "server_ip" || k == "ip")
            ip = v;
        else if (k == "server_port" || k == "port")
            port = std::atoi(v.c_str());
        else if (k == "name" || k == "player_name")
            name = v;
    }
}

} // namespace

int main(int argc, char **argv)
{
    App app;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);

    std::string dir = std::string(GetApplicationDirectory());
    loadConfig(app.session.ip, app.session.port, app.world.myName);
    app.settings.load(dir);

    if (app.world.myName.empty())
        app.world.myName = app.settings.name;

    app.session.steam.init();

    if (app.world.myName.empty() && app.session.steam.ok)
        app.world.myName = app.session.steam.name;

    app.session.loadProfile();
    app.mainMenu.directAddr = app.settings.lastServer;
    if (const char *sc = std::getenv("BW_SCALE"))
        app.settings.uiScale = std::clamp(float(std::atof(sc)), 0.6f, 2.0f);

    InitWindow(1280, 720, "Base Wars");

    if (const char *ws = std::getenv("BW_WIN"))
    {
        int w = 0, h = 0;
        if (std::sscanf(ws, "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
            SetWindowSize(w, h);
    }

    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    PauseMenu::applyWindowMode(app.settings.windowMode);

    findAssets();

    std::string autoJoin; // "--connect host:port" joins immediately; screenshot runs join the configured server

    for (int i = 1; i + 1 < argc; i++)
        if (std::string(argv[i]) == "--connect")
            autoJoin = argv[i + 1];

    if (autoJoin.empty() && std::getenv("BW_SHOT") && !std::getenv("BW_MENU"))
        autoJoin = app.session.ip + ":" + std::to_string(app.session.port);

    app.run(autoJoin);
    CloseWindow();
}
