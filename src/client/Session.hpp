/**
 * @file Session.hpp
 * @brief Connection lifecycle: profile, Steam login, transport, hello handshake and the network event pump.
 */
#pragma once
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
    std::string ip;         // configured server (config.txt)
    uint64_t devId = 0;     // BW_DEV_ID: identity for `--local` servers (no Steam login); 0 = use the Steam account
    std::string serverNote; // last text the server sent before the welcome (why a login was refused)
    int port = DEFAULT_PORT;
    double connectStart = 0, lastNameRefresh = 0;
    ServerEntry connectingTo;
    net::SteamClient steam;
    net::ServerBrowser browser;
    net::SteamClientNet cnet;

    bool playing() const { return screen == Screen::Playing && welcomed && !lost; }
    void initIdentity(); // reads BW_DEV_ID
    uint64_t myId() const { return devId ? devId : (steam.ok ? steam.id : 0); }
    std::string resolveNames(const std::string &text) const; // replaces server name tokens with Steam persona names
    void refreshNames();        // re-resolves roster names once per second (async Steam lookups)
    void send(const Writer &w); // stamps every command except C_Hello with the next sequence number
    uint32_t cmdSeq = 0;        // last sequence number sent on this connection
    void sendHello();
    void joinServer(const ServerEntry &e);
    void backToMenu(const std::string &why);
    void pump(); // drain transport events
    void resetFlags()
    {
        welcomed = lost = connected = helloSent = false;
        cmdSeq = 0;
    }
    static bool parseAddr(const std::string &s, std::string &host, uint16_t &port);

  private:
    App &app;
};
