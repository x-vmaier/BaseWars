#include "networking/SteamClient.hpp"

#include <steam/isteamnetworkingutils.h>
#include <steam/steam_api.h>

namespace net {

bool SteamClient::init() {
    if (!SteamAPI_Init()) return false;
    ok = true;
    id = SteamUser()->GetSteamID().ConvertToUint64();
    name = SteamFriends()->GetPersonaName();
    int n = SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
    for (int i = 0; i < n && i < 200; i++) friends.push_back(SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate).ConvertToUint64());
    SteamNetworkingUtils()->InitRelayNetworkAccess();
    return true;
}
void SteamClient::run() { if (ok) SteamAPI_RunCallbacks(); }
void SteamClient::shutdown() { if (ok) SteamAPI_Shutdown(); ok = false; }

}  // namespace net
