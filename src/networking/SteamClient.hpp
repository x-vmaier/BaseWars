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
    bool init();
    void run();
    void shutdown();
};

} // namespace net
