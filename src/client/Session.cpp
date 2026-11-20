/**
 * @file Session.cpp
 * @brief Connection lifecycle: profile, Steam login, transport, hello handshake and the network event pump.
 */
#include "client/Session.hpp"

#include "client/App.hpp"

#include "common/NameToken.hpp"

#include <cstdio>
#include <cstdlib>

void Session::initIdentity()
{
    if (const char *d = std::getenv("BW_DEV_ID"))
        devId = std::strtoull(d, nullptr, 10);
    if (devId >= net::DEV_ID_LIMIT)
        devId = 0; // dev ids must never look like a real SteamID
}

std::string Session::resolveNames(const std::string &text) const
{
    return expandNameTokens(text, [&](uint64_t id) { return steam.nameOf(id); });
}

void Session::refreshNames()
{
    if (GetTime() - lastNameRefresh < 1.0)
        return;
    lastNameRefresh = GetTime();
    for (Peer &p : app.world.roster)
        p.name = resolveNames(p.raw);
    if (!app.world.myRaw.empty())
        app.world.myName = resolveNames(app.world.myRaw);
}

bool Session::parseAddr(const std::string &s, std::string &host, uint16_t &port)
{
    auto c = s.rfind(':');
    host = c == std::string::npos ? s : s.substr(0, c);
    int p = c == std::string::npos ? DEFAULT_PORT : std::atoi(s.c_str() + c + 1);
    port = uint16_t(p > 0 && p < 65536 ? p : DEFAULT_PORT);
    return !host.empty();
}

void Session::joinServer(const ServerEntry &e)
{
    app.resetWorld();
    connectingTo = e;
    serverNote.clear();
    if (!myId())
    {
        app.mainMenu.status =
            "Steam is not running: it is needed to prove who you are. Start Steam and restart the game.";
        return;
    }
    bool ok;
    if (e.steamId && steam.ok)
        ok = cnet.connectSteam(e.steamId);

    else
    {
        std::string host = e.address;
        uint16_t pt = e.port;
        if (e.local && e.address.find(':') != std::string::npos)
            parseAddr(e.address, host, pt);
        ok = cnet.connectIP(host, pt);
        app.settings.lastServer = host + ":" + std::to_string(pt);
    }
    if (!ok)
    {
        app.mainMenu.status = "Could not start a connection to " + e.name;
        return;
    }

    screen = Screen::Connecting;
    connectStart = GetTime();
    app.mainMenu.status.clear();
}

void Session::backToMenu(const std::string &why)
{
    cnet.close();
    steam.cancelTicket();
    app.resetWorld();
    screen = Screen::Menu;
    app.mainMenu.status = why;
    SetWindowTitle("Base Wars");
    app.mainMenu.refresh();
}

void Session::sendHello()
{
    Writer w;
    w.put<uint8_t>(C_Hello);
    // Dev ids use the dev ticket (accepted by `--local` servers only); otherwise Steam signs our real SteamID.
    bool dev = devId != 0 || !steam.requestTicket();
    w.put<uint64_t>(myId());
    if (dev)
    {
        w.put<uint16_t>(uint16_t(sizeof net::DEV_TICKET - 1));
        w.bytes(net::DEV_TICKET, sizeof net::DEV_TICKET - 1);
    }
    else
    {
        w.put<uint16_t>(uint16_t(steam.ticket.size()));
        w.bytes(steam.ticket.data(), steam.ticket.size());
    }
    w.put<uint16_t>(uint16_t(steam.friends.size()));
    for (uint64_t f : steam.friends)
        w.put<uint64_t>(f);
    send(w);
}

void Session::pump()
{
    NetEvent ev;
    while (cnet.poll(ev))
    {
        switch (ev.type)
        {
        case NetEvent::Connect:
            connected = true;
            if (!helloSent)
            {
                sendHello();
                helloSent = true;
            }
            break;
        case NetEvent::Receive:
            app.packets.handle(ev.data.data(), ev.data.size());
            break;
        case NetEvent::Disconnect:
            connected = false;
            if (screen != Screen::Menu)
                backToMenu(welcomed ? "Disconnected from the server."
                                    : (serverNote.empty() ? "The server closed the connection." : serverNote));
            break;
        }
    }
}
