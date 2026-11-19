/**
 * @file SteamClientNet.hpp
 * @brief Client transport: Steam Networking Sockets by IP or by server SteamID (Datagram Relay).
 */
#pragma once
#include "networking/NetTypes.hpp"

#include <deque>
#include <string>

namespace net
{

class SteamClientNet
{
  public:
    ~SteamClientNet() { close(); }
    bool connectIP(const std::string &ip, uint16_t port);
    bool connectSteam(uint64_t serverSteamId);
    bool active() const { return conn != 0; }
    bool poll(NetEvent &out);
    void send(const Writer &w);
    void close();

  private:
    void pump();
    std::deque<NetEvent> queue;
    uint32_t conn = 0;
};

} // namespace net
