/**
 * @file SteamClient.cpp
 * @brief Local Steam user: login, display name, friend list.
 */
#include "networking/SteamClient.hpp"

#include "common/Log.hpp"
#include "networking/NetTypes.hpp"

#include <steam/isteamnetworkingutils.h>
#include <steam/steam_api.h>

#include <cstdio>

namespace net
{

bool SteamClient::init()
{
    SteamErrMsg err{};

    if (SteamAPI_InitEx(&err) != k_ESteamAPIInitResult_OK)
    {
        spdlog::error("SteamAPI_Init failed: {}", static_cast<const char *>(err));
        return false;
    }

    bw::log::hookSteam(false);
    ok = true;
    id = SteamUser()->GetSteamID().ConvertToUint64();
    name = SteamFriends()->GetPersonaName();
    int n = SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
    for (int i = 0; i < n && i < 200; i++)
        friends.push_back(SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate).ConvertToUint64());

    SteamNetworkingUtils()->InitRelayNetworkAccess();
    return true;
}
bool SteamClient::requestTicket()
{
    cancelTicket();
    if (!ok)
        return false;
    ticket.assign(1024, 0);
    uint32 len = 0;
    HAuthTicket h = SteamUser()->GetAuthSessionTicket(ticket.data(), int(ticket.size()), &len, nullptr);
    if (h == k_HAuthTicketInvalid || len == 0)
    {
        ticket.clear();
        return false;
    }
    ticket.resize(len);
    ticketHandle = h;
    return true;
}
void SteamClient::cancelTicket()
{
    if (ok && ticketHandle)
        SteamUser()->CancelAuthTicket(ticketHandle);
    ticketHandle = 0;
    ticket.clear();
}
std::string SteamClient::nameOf(uint64_t sid) const
{
    std::string fallback = "Player" + std::to_string(sid % 10000);
    if (sid < DEV_ID_LIMIT)
        return "Dev" + std::to_string(sid);
    if (!ok)
        return fallback;
    CSteamID cs(sid);
    if (cs == SteamUser()->GetSteamID())
        return name;
    // Strangers' names arrive asynchronously; the caller asks again later (Session refreshes roster names).
    if (SteamFriends()->RequestUserInformation(cs, true))
        return fallback;
    const char *n = SteamFriends()->GetFriendPersonaName(cs);
    return n && *n ? n : fallback;
}
void SteamClient::run()
{
    if (ok)
        SteamAPI_RunCallbacks();
}
void SteamClient::shutdown()
{
    cancelTicket();
    if (ok)
        SteamAPI_Shutdown();
    ok = false;
}

} // namespace net
