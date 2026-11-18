#pragma once
// Steamworks integration (compiled only with -DBW_STEAM, i.e. when the SDK is found in third_party/).
//   * steamlayer::Client   - Steam login, display name, friend list
//   * steamlayer::Browser  - Internet / Friends server lists through Steam matchmaking
//   * server*()            - dedicated-server registration (shows up in the browser) and a Steam Networking Sockets listen socket
//   * client*()            - P2P relay connection to a server by its SteamID
// Without the SDK every call is a harmless no-op so the game still builds and uses plain ENet.
// NOTE: included from net.hpp, after NetEvent / ServerEntry / STEAM_BIT are defined.
#include <deque>
#include <functional>
#include <string>
#include <vector>

#ifdef BW_STEAM
#include <steam/isteamnetworkingutils.h>
#include <steam/steam_api.h>
#include <steam/steam_gameserver.h>

#include <cstring>

namespace steamlayer {

inline std::deque<NetEvent>* g_q = nullptr;       // Steam callbacks run on the polling thread and feed this queue
inline HSteamNetPollGroup g_group = k_HSteamNetPollGroup_Invalid;
inline HSteamListenSocket g_listen = k_HSteamListenSocket_Invalid;
inline bool g_serverSide = false;

inline ISteamNetworkingSockets* sockets() { return g_serverSide ? SteamGameServerNetworkingSockets() : SteamNetworkingSockets(); }

inline void onStatus(SteamNetConnectionStatusChangedCallback_t* info) {
    ISteamNetworkingSockets* s = sockets();
    HSteamNetConnection h = info->m_hConn;
    uint64_t id = g_serverSide ? (STEAM_BIT | uint64_t(h)) : 0;
    switch (info->m_info.m_eState) {
    case k_ESteamNetworkingConnectionState_Connecting:
        if (g_serverSide && info->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid) {
            if (s->AcceptConnection(h) != k_EResultOK) { s->CloseConnection(h, 0, nullptr, false); break; }
            s->SetConnectionPollGroup(h, g_group);
        }
        break;
    case k_ESteamNetworkingConnectionState_Connected:
        if (g_q) g_q->push_back({NetEvent::Connect, id, {}});
        break;
    case k_ESteamNetworkingConnectionState_ClosedByPeer:
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        s->CloseConnection(h, 0, nullptr, false);
        if (g_q) g_q->push_back({NetEvent::Disconnect, id, {}});
        break;
    default: break;
    }
}

inline void drain(const std::function<int(SteamNetworkingMessage_t**, int)>& receive, bool serverSide) {
    SteamNetworkingMessage_t* msgs[32];
    for (int n = receive(msgs, 32); n > 0; n = receive(msgs, 32)) {
        for (int i = 0; i < n; i++) {
            const uint8_t* d = static_cast<const uint8_t*>(msgs[i]->m_pData);
            if (g_q) g_q->push_back({NetEvent::Receive, serverSide ? (STEAM_BIT | uint64_t(msgs[i]->m_conn)) : 0, std::vector<uint8_t>(d, d + msgs[i]->m_cbSize)});
            msgs[i]->Release();
        }
    }
}

// ---- dedicated server -------------------------------------------------------------------------------------------
inline bool serverInit(uint16_t port, const std::string& name, std::deque<NetEvent>& q) {
    // The query port answers the server browser; the game itself talks over ENet (direct) and Steam P2P (relay).
    if (!SteamGameServer_Init(0, port, uint16_t(port + 1), eServerModeNoAuthentication, "0.1.0")) return false;
    ISteamGameServer* gs = SteamGameServer();
    gs->SetModDir("basewars");
    gs->SetProduct("BaseWars");
    gs->SetGameDescription("Base Wars");
    gs->SetDedicatedServer(true);
    gs->SetServerName(name.c_str());
    gs->SetMaxPlayerCount(MAX_PLAYERS);
    gs->SetPasswordProtected(false);
    gs->SetGameTags("basewars");
    gs->LogOnAnonymous();
    gs->SetAdvertiseServerActive(true);
    SteamGameServerNetworkingUtils()->InitRelayNetworkAccess();
    g_serverSide = true;
    g_q = &q;
    SteamNetworkingConfigValue_t opt;
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(&onStatus));
    g_listen = SteamGameServerNetworkingSockets()->CreateListenSocketP2P(0, 1, &opt);
    g_group = SteamGameServerNetworkingSockets()->CreatePollGroup();
    return g_listen != k_HSteamListenSocket_Invalid;
}
inline void serverPublish(int players, int maxPlayers, const std::string& map) {
    ISteamGameServer* gs = SteamGameServer();
    gs->SetMaxPlayerCount(maxPlayers);
    gs->SetBotPlayerCount(players);                    // connected players are reported as the player count in the browser
    gs->SetMapName(map.c_str());
}
inline void serverRun() {
    SteamGameServer_RunCallbacks();
    SteamGameServerNetworkingSockets()->RunCallbacks();
    drain([](SteamNetworkingMessage_t** m, int n) { return SteamGameServerNetworkingSockets()->ReceiveMessagesOnPollGroup(g_group, m, n); }, true);
}
inline void serverSend(uint32_t conn, const void* data, uint32_t size) { SteamGameServerNetworkingSockets()->SendMessageToConnection(conn, data, size, k_nSteamNetworkingSend_Reliable, nullptr); }
inline void serverClose(uint32_t conn, bool linger) { SteamGameServerNetworkingSockets()->CloseConnection(conn, 0, nullptr, linger); }
inline void serverShutdown() {
    if (g_listen != k_HSteamListenSocket_Invalid) SteamGameServerNetworkingSockets()->CloseListenSocket(g_listen);
    if (g_group != k_HSteamNetPollGroup_Invalid) SteamGameServerNetworkingSockets()->DestroyPollGroup(g_group);
    SteamGameServer()->LogOff();
    SteamGameServer_Shutdown();
}

// ---- client connection -------------------------------------------------------------------------------------------
inline uint32_t clientConnect(uint64_t steamId, std::deque<NetEvent>& q) {
    g_serverSide = false;
    g_q = &q;
    SteamNetworkingIdentity ident;
    ident.Clear();
    ident.SetSteamID64(steamId);
    SteamNetworkingConfigValue_t opt;
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(&onStatus));
    return SteamNetworkingSockets()->ConnectP2P(ident, 0, 1, &opt);
}
inline void clientRun(uint32_t conn) {
    SteamNetworkingSockets()->RunCallbacks();
    drain([conn](SteamNetworkingMessage_t** m, int n) { return SteamNetworkingSockets()->ReceiveMessagesOnConnection(conn, m, n); }, false);
}
inline void clientSend(uint32_t conn, const void* data, uint32_t size) { SteamNetworkingSockets()->SendMessageToConnection(conn, data, size, k_nSteamNetworkingSend_Reliable, nullptr); }
inline void clientClose(uint32_t conn) { SteamNetworkingSockets()->CloseConnection(conn, 0, nullptr, true); }

// ---- local player ------------------------------------------------------------------------------------------------
struct Client {
    bool ok = false;
    uint64_t id = 0;
    std::string name;
    std::vector<uint64_t> friends;                    // SteamIDs of friends, sent to servers so new bases spawn near them
    bool init() {
        if (!SteamAPI_Init()) return false;
        ok = true;
        id = SteamUser()->GetSteamID().ConvertToUint64();
        name = SteamFriends()->GetPersonaName();
        int n = SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
        for (int i = 0; i < n && i < 200; i++) friends.push_back(SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate).ConvertToUint64());
        SteamNetworkingUtils()->InitRelayNetworkAccess();
        return true;
    }
    void run() { if (ok) SteamAPI_RunCallbacks(); }
    void shutdown() { if (ok) SteamAPI_Shutdown(); ok = false; }
};

// ---- server browser ----------------------------------------------------------------------------------------------
class Browser : public ISteamMatchmakingServerListResponse {
public:
    ~Browser() { cancel(); }
    void refresh(bool friendsOnly) {
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
    bool loading() const { return busy; }
    const std::vector<ServerEntry>& entries() const { return servers; }
    void ServerResponded(HServerListRequest r, int i) override {
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
    void ServerFailedToRespond(HServerListRequest, int) override {}
    void RefreshComplete(HServerListRequest, EMatchMakingServerResponse) override { busy = false; }

private:
    void cancel() {
        if (req) { if (auto* m = SteamMatchmakingServers()) m->ReleaseRequest(req); req = nullptr; }     // null after SteamAPI_Shutdown
        busy = false;
    }
    HServerListRequest req = nullptr;
    MatchMakingKeyValuePair_t filter{};
    MatchMakingKeyValuePair_t* filters = nullptr;
    std::vector<ServerEntry> servers;
    bool busy = false, asFriends = false;
};

}  // namespace steamlayer

#else  // ---------------------------------------------------------------- no SDK: inert stand-ins

namespace steamlayer {
struct Client {
    bool ok = false;
    uint64_t id = 0;
    std::string name;
    std::vector<uint64_t> friends;
    bool init() { return false; }
    void run() {}
    void shutdown() {}
};
class Browser {
public:
    void refresh(bool) {}
    bool loading() const { return false; }
    const std::vector<ServerEntry>& entries() const { return none; }
private:
    std::vector<ServerEntry> none;
};
}  // namespace steamlayer

#endif
