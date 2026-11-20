/**
 * @file SteamClient.hpp
 * @brief Local Steam user: login, display name, friend list.
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace net
{

struct SteamClient
{
    bool ok = false;
    uint64_t id = 0;
    std::string name;
    std::vector<uint64_t> friends; // SteamIDs of friends, sent to servers so new bases spawn near them
    std::vector<uint8_t> ticket;   // auth ticket of the current connection attempt (proves `id` to the server)
    uint32_t ticketHandle = 0;
    bool init();
    bool requestTicket();                  // fresh ticket for one server login; false without Steam
    void cancelTicket();                   // call when the connection ends
    std::string nameOf(uint64_t id) const; // persona name (Steam is the only source of names); "DevN" for dev ids
    void run();
    void shutdown();
};

} // namespace net
