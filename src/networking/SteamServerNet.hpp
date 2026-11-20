/**
 * @file SteamServerNet.hpp
 * @brief Server transport: Steam GameServer + Steam Networking Sockets.
 */
#pragma once
//  * one IP listen socket   - direct connections (LAN / localhost testing, port forwarded hosting)
//  * one P2P listen socket  - Steam Datagram Relay, reachable by SteamID from the server browser without port
//  forwarding
// Both feed the same poll group, so the game logic sees one stream of NetEvents.
#include "networking/NetTypes.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace net
{

struct AuthCallbacks;

class SteamServerNet
{
  public:
    SteamServerNet();
    ~SteamServerNet();
    using TicketVerifier = std::function<AuthResult(uint64_t claimedId, const std::vector<uint8_t> &ticket)>;

    // localOnly: skip the Steam backend login/advertising (offline testing); direct IP still works.
    bool start(uint16_t port, const std::string &serverName, bool localOnly);
    void startOffline()
    {
        offline = true;
        devAuth = true;
    } // selftests: no Steam; sends are counted, kicks become Disconnect events
    size_t sentCount() const { return sent; }
    bool relayActive() const { return advertised; }
    void publish(int players, int maxPlayers, const std::string &map);
    bool poll(NetEvent &out);
    // reliable = false: sequenced, may be dropped (S_State chunks). Selftests observe every message through `tap`.
    void send(uint64_t conn, const Writer &w, bool reliable = true);
    std::function<void(uint64_t conn, const std::vector<uint8_t> &, bool reliable)> tap;
    void kick(uint64_t conn, bool linger);
    // Identity: checks that `ticket` proves the connection owns SteamID `claimedId`. Real servers ask Steam (verdict
    // may be Pending); `--local` and selftests accept only the dev ticket for small ids (never a real SteamID).
    AuthResult beginAuth(uint64_t conn, uint64_t claimedId, const std::vector<uint8_t> &ticket);
    void endAuth(uint64_t conn); // call when the connection ends; releases the Steam auth session
    bool devAuthMode() const { return devAuth; }
    void setTicketVerifier(TicketVerifier v) { verifier = std::move(v); } // selftests: replaces the dev rule
    void flush(); // push queued messages out now (called once per tick)

  private:
    friend struct AuthCallbacks;
    void pump();
    std::deque<NetEvent> queue;
    std::unique_ptr<AuthCallbacks> authCb;
    std::unordered_map<uint64_t, uint64_t> authBySteamId; // steamId -> conn, only while a Steam auth session is open
    TicketVerifier verifier;
    bool running = false, advertised = false, offline = false, devAuth = false;
    size_t sent = 0;
    uint32_t listenIp = 0, listenP2P = 0, group = 0;
};

} // namespace net
