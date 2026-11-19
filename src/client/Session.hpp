#pragma once
// Connection lifecycle: profile (uuid), Steam login, server browser, transport, hello handshake, join / leave,
// and the network event pump that feeds the PacketHandler.
#include "common/Shared.hpp"
#include "networking/ServerBrowser.hpp"
#include "networking/SteamClient.hpp"
#include "networking/SteamClientNet.hpp"

#include <string>

class App;

enum class Screen
{
    Menu,
    Connecting,
    Playing
};

class Session
{
  public:
    explicit Session(App &app) : app(app) {}

    Screen screen = Screen::Menu;
    bool connected = false, welcomed = false, lost = false, helloSent = false;
    std::string ip, uuid; // configured server (config.txt) and our persistent profile id
    int port = DEFAULT_PORT;
    double connectStart = 0;
    ServerEntry connectingTo;
    net::SteamClient steam;
    net::ServerBrowser browser;
    net::SteamClientNet cnet;

    bool playing() const { return screen == Screen::Playing && welcomed && !lost; }
    void loadProfile(); // profile.txt: uuid + name (creates both when missing)
    void send(const Writer &w) { cnet.send(w); }
    void sendHello();
    void joinServer(const ServerEntry &e);
    void backToMenu(const std::string &why);
    void pump(); // drain transport events
    void resetFlags() { welcomed = lost = connected = helloSent = false; }
    static bool parseAddr(const std::string &s, std::string &host, uint16_t &port);

  private:
    App &app;
};
