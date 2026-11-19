#include "client/Session.hpp"

#include "client/App.hpp"

#include <cstdio>
#include <fstream>
#include <random>

void Session::loadProfile() {
    std::string& myName = app.world.myName;
    std::string path = std::string(GetApplicationDirectory()) + "profile.txt";
    std::ifstream f(path);
    std::string ln;
    while (f && std::getline(f, ln)) {
        auto p = ln.find('=');
        if (p == std::string::npos) continue;
        std::string k = ln.substr(0, p), v = ln.substr(p + 1);
        while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
        if (k == "uuid") uuid = v;
        else if (k == "name" && myName.empty()) myName = v;
    }
    std::random_device rd;
    if (uuid.size() < 16) {
        char buf[40];
        uint32_t a = rd(), b = rd(), c = rd(), d = rd();
        std::snprintf(buf, sizeof buf, "%08x-%04x-4%03x-%04x-%04x%08x", a, b >> 16, b & 0xfff, 0x8000 | (c & 0x3fff), c >> 16, d);
        uuid = buf;
    }
    if (myName.empty()) myName = "Player" + std::to_string(rd() % 9000 + 1000);
    std::ofstream o(path);
    if (o) o << "uuid=" << uuid << "\nname=" << myName << "\n";
}

bool Session::parseAddr(const std::string& s, std::string& host, uint16_t& port) {
    auto c = s.rfind(':');
    host = c == std::string::npos ? s : s.substr(0, c);
    int p = c == std::string::npos ? DEFAULT_PORT : std::atoi(s.c_str() + c + 1);
    port = uint16_t(p > 0 && p < 65536 ? p : DEFAULT_PORT);
    return !host.empty();
}

void Session::joinServer(const ServerEntry& e) {
    app.resetWorld();
    connectingTo = e;
    bool ok;
    if (e.steamId && steam.ok) ok = cnet.connectSteam(e.steamId);
    else {
        std::string host = e.address;
        uint16_t pt = e.port;
        if (e.local && e.address.find(':') != std::string::npos) parseAddr(e.address, host, pt);
        ok = cnet.connectIP(host, pt);
        app.settings.lastServer = host + ":" + std::to_string(pt);
    }
    if (!ok) { app.mainMenu.status = "Could not start a connection to " + e.name; return; }
    screen = Screen::Connecting;
    connectStart = GetTime();
    app.mainMenu.status.clear();
}

void Session::backToMenu(const std::string& why) {
    cnet.close();
    app.resetWorld();
    screen = Screen::Menu;
    app.mainMenu.status = why;
    SetWindowTitle("Base Wars");
    app.mainMenu.refresh();
}

void Session::sendHello() {
    Writer w;
    w.put<uint8_t>(C_Hello);
    w.str(uuid);
    w.str(app.world.myName);
    w.put<uint64_t>(steam.ok ? steam.id : 0);
    w.put<uint16_t>(uint16_t(steam.friends.size()));
    for (uint64_t f : steam.friends) w.put<uint64_t>(f);
    send(w);
}

void Session::pump() {
    NetEvent ev;
    while (cnet.poll(ev)) {
        switch (ev.type) {
        case NetEvent::Connect:
            connected = true;
            if (!helloSent) { sendHello(); helloSent = true; }
            break;
        case NetEvent::Receive: app.packets.handle(ev.data.data(), ev.data.size()); break;
        case NetEvent::Disconnect:
            connected = false;
            if (screen != Screen::Menu) backToMenu(welcomed ? "Disconnected from the server." : "The server closed the connection.");
            break;
        }
    }
}
