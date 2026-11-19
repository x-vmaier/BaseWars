#pragma once
// Server transport: Steam GameServer + Steam Networking Sockets.
//  * one IP listen socket   - direct connections (LAN / localhost testing, port forwarded hosting)
//  * one P2P listen socket  - Steam Datagram Relay, reachable by SteamID from the server browser without port forwarding
// Both feed the same poll group, so the game logic sees one stream of NetEvents.
#include "networking/NetTypes.hpp"

#include <deque>
#include <string>

namespace net
{

    class SteamServerNet
    {
    public:
        ~SteamServerNet();
        // localOnly: skip the Steam backend login/advertising (offline testing); direct IP still works.
        bool start(uint16_t port, const std::string &serverName, bool localOnly);
        bool relayActive() const { return advertised; }
        void publish(int players, int maxPlayers, const std::string &map);
        bool poll(NetEvent &out);
        void send(uint64_t conn, const Writer &w);
        void kick(uint64_t conn, bool linger);
        void flush(); // push queued messages out now (called once per tick)

    private:
        void pump();
        std::deque<NetEvent> queue;
        bool running = false, advertised = false;
        uint32_t listenIp = 0, listenP2P = 0, group = 0;
    };

} // namespace net
