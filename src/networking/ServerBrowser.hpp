#pragma once
// Internet / Friends server lists through Steam matchmaking.
#include "networking/NetTypes.hpp"

#include <steam/steam_api.h>

#include <vector>

namespace net
{

class ServerBrowser : public ISteamMatchmakingServerListResponse
{
  public:
    ~ServerBrowser() { cancel(); }
    void refresh(bool friendsOnly);
    bool loading() const { return busy; }
    const std::vector<ServerEntry> &entries() const { return servers; }
    void ServerResponded(HServerListRequest r, int i) override;
    void ServerFailedToRespond(HServerListRequest, int) override {}
    void RefreshComplete(HServerListRequest, EMatchMakingServerResponse) override { busy = false; }

  private:
    void cancel();
    HServerListRequest req = nullptr;
    MatchMakingKeyValuePair_t filter{};
    MatchMakingKeyValuePair_t *filters = nullptr;
    std::vector<ServerEntry> servers;
    bool busy = false, asFriends = false;
};

} // namespace net
