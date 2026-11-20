/**
 * @file NetTypes.hpp
 * @brief Plain data shared by the client and server network layers.
 */
#pragma once
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
        Receive,
        Auth // asynchronous ticket verdict for `conn`: data[0] = 1 accepted, 0 rejected
    } type = Receive;
    uint64_t conn = 0; // Steam Networking Sockets connection handle (server side), 0 on the client
    std::vector<uint8_t> data;
};

// Result of checking the SteamID a client claims in its hello against its auth ticket.
enum class AuthResult
{
    Accepted,
    Rejected,
    Pending // verdict arrives later as NetEvent::Auth
};

// `--local` servers accept this ticket for dev ids (< 2^32, never a real SteamID); see BW_DEV_ID.
inline constexpr char DEV_TICKET[] = "BWDEV";
inline constexpr uint64_t DEV_ID_LIMIT = 1ull << 32;

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

using net::AuthResult;
using net::DEV_ID_LIMIT;
using net::DEV_TICKET;
using net::NetEvent;
using net::ServerEntry;
