#include "networking/ServerBrowser.hpp"

#include <cstring>
#include <string>

namespace net {

void ServerBrowser::refresh(bool friendsOnly) {
    cancel();
    servers.clear();
    asFriends = friendsOnly;
    std::strncpy(filter.m_szKey, "gametagsand", sizeof(filter.m_szKey));
    std::strncpy(filter.m_szValue, "basewars", sizeof(filter.m_szValue));
    filters = &filter;
    AppId_t app = SteamUtils()->GetAppID();
    req = friendsOnly ? SteamMatchmakingServers()->RequestFriendsServerList(app, &filters, 1, this)
                      : SteamMatchmakingServers()->RequestInternetServerList(app, &filters, 1, this);
    busy = req != nullptr;
}

void ServerBrowser::ServerResponded(HServerListRequest r, int i) {
    gameserveritem_t* s = SteamMatchmakingServers()->GetServerDetails(r, i);
    if (!s || s->m_bDoNotRefresh) return;
    uint64_t sid = s->m_steamID.ConvertToUint64();
    for (auto& e : servers) if (e.steamId == sid) return;
    ServerEntry e;
    e.name = s->GetName();
    e.map = s->m_szMap;
    e.tags = s->m_szGameTags;
    e.ping = s->m_nPing;
    e.players = s->m_nPlayers;
    e.maxPlayers = s->m_nMaxPlayers;
    e.port = s->m_NetAdr.GetConnectionPort();
    uint32 ip = s->m_NetAdr.GetIP();
    e.address = std::to_string((ip >> 24) & 255) + "." + std::to_string((ip >> 16) & 255) + "." + std::to_string((ip >> 8) & 255) + "." + std::to_string(ip & 255);
    e.steamId = sid;
    e.password = s->m_bPassword;
    e.friends = asFriends;
    servers.push_back(e);
}

void ServerBrowser::cancel() {
    if (req) { if (auto* m = SteamMatchmakingServers()) m->ReleaseRequest(req); req = nullptr; }     // null after SteamAPI_Shutdown
    busy = false;
}

}  // namespace net
