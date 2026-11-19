#pragma once
// Plain data shared by the client and server network layers.
#include "common/Shared.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace net
{

    struct NetEvent
    {
        enum Type
        {
            Connect,
            Disconnect,
            Receive
        } type = Receive;
        uint64_t conn = 0; // Steam Networking Sockets connection handle (server side), 0 on the client
        std::vector<uint8_t> data;
    };

    // One row of the main-menu server browser.
    struct ServerEntry
    {
        std::string name, map, tags, address;
        int ping = 0, players = 0, maxPlayers = 0;
        uint16_t port = DEFAULT_PORT;
        uint64_t steamId = 0; // non-zero: reachable through Steam Datagram Relay
        bool friends = false, password = false, local = false;
    };

} // namespace net

using net::NetEvent;
using net::ServerEntry;
