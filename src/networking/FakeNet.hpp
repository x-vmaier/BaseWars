/**
 * @file FakeNet.hpp
 * @brief Test hook: BW_NETLAG=<ms> and BW_NETLOSS=<percent> make Steam Networking Sockets delay / drop sent packets.
 */
#pragma once
#include <steam/isteamnetworkingutils.h>

#include <cstdlib>

namespace net
{

// Applies to everything this process sends (set it on both client and server for a symmetric link).
inline void applyFakeNetworkEnv()
{
    ISteamNetworkingUtils *u = SteamNetworkingUtils();
    if (!u)
        return;
    if (const char *lag = std::getenv("BW_NETLAG"))
        u->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketLag_Send, std::atoi(lag));
    if (const char *loss = std::getenv("BW_NETLOSS"))
        u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, float(std::atof(loss)));
}

} // namespace net
