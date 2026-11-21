/**
 * @file SteamServerNet.cpp
 * @brief Server transport: Steam GameServer + Steam Networking Sockets.
 */
#include "networking/SteamServerNet.hpp"
#include "common/Log.hpp"

#include "networking/FakeNet.hpp"

#include <steam/isteamnetworkingutils.h>
#include <steam/steam_api.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <steam/steam_gameserver.h>

namespace net
{

namespace
{
std::deque<NetEvent> *g_queue = nullptr;
SteamServerNet *g_net = nullptr;
HSteamNetPollGroup g_group = k_HSteamNetPollGroup_Invalid;

ISteamNetworkingSockets *sockets()
{
    return SteamGameServerNetworkingSockets();
}

void onStatus(SteamNetConnectionStatusChangedCallback_t *info)
{
    auto *s = sockets();
    HSteamNetConnection h = info->m_hConn;
    if (std::getenv("BW_NETLOG"))
        spdlog::info("[net] state {} -> {} ({})", int(info->m_eOldState), int(info->m_info.m_eState),
                     info->m_info.m_szEndDebug);

    switch (info->m_info.m_eState)
    {
    case k_ESteamNetworkingConnectionState_Connecting:
        if (info->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid)
        {
            if (s->AcceptConnection(h) != k_EResultOK)
            {
                s->CloseConnection(h, 0, nullptr, false);
                break;
            }
            s->SetConnectionPollGroup(h, g_group);
        }
        break;
    case k_ESteamNetworkingConnectionState_Connected:
        if (g_queue)
            g_queue->push_back({NetEvent::Connect, h, {}});
        break;
    case k_ESteamNetworkingConnectionState_ClosedByPeer:
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        s->CloseConnection(h, 0, nullptr, false);
        if (g_queue)
            g_queue->push_back({NetEvent::Disconnect, h, {}});
        break;
    default:
        break;
    }
}
} // namespace

// Steam reports the ticket verdict (and later revocations) here.
struct AuthCallbacks
{
    STEAM_GAMESERVER_CALLBACK(AuthCallbacks, onResponse, ValidateAuthTicketResponse_t);
};

void AuthCallbacks::onResponse(ValidateAuthTicketResponse_t *r)
{
    if (!g_net)
        return;
    auto it = g_net->authBySteamId.find(r->m_SteamID.ConvertToUint64());
    if (it == g_net->authBySteamId.end())
        return;
    bool ok = r->m_eAuthSessionResponse == k_EAuthSessionResponseOK;
    g_net->queue.push_back({NetEvent::Auth, it->second, {uint8_t(ok ? 1 : 0)}});
}

SteamServerNet::SteamServerNet() = default;

bool SteamServerNet::start(uint16_t port, const std::string &serverName, bool localOnly)
{
    // Game port = direct listen port, query port = port + 1 (server browser).
    SteamErrMsg err{};
    if (SteamGameServer_InitEx(0, port, uint16_t(port + 1),
                               localOnly ? eServerModeNoAuthentication : eServerModeAuthentication, "0.2.0",
                               &err) != k_ESteamAPIInitResult_OK)
    {
        spdlog::error("SteamGameServer_Init failed: {}", static_cast<const char *>(err));
        return false;
    }

    bw::log::hookSteam(true);
    ISteamGameServer *gs = SteamGameServer();
    gs->SetModDir("basewars");
    gs->SetProduct("BaseWars");
    gs->SetGameDescription("Base Wars");
    gs->SetDedicatedServer(true);
    gs->SetServerName(serverName.c_str());
    gs->SetMaxPlayerCount(MAX_PLAYERS);
    gs->SetPasswordProtected(false);
    gs->SetGameTags("basewars");

    if (!localOnly)
    {
        gs->LogOnAnonymous();
        gs->SetAdvertiseServerActive(true);
        SteamNetworkingUtils()->InitRelayNetworkAccess();
    }

    applyFakeNetworkEnv();
    g_queue = &queue;
    g_net = this;
    devAuth = localOnly;
    authCb = std::make_unique<AuthCallbacks>();
    SteamNetworkingConfigValue_t opt[2];
    opt[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void *>(&onStatus));
    opt[1].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth,
                    1); // direct IP connections need no Steam ticket (LAN / localhost testing)

    g_group = sockets()->CreatePollGroup();
    group = g_group;

    SteamNetworkingIPAddr addr;
    addr.Clear();
    addr.m_port = port;

    listenIp = sockets()->CreateListenSocketIP(addr, 2, opt);
    if (!localOnly)
        listenP2P = sockets()->CreateListenSocketP2P(0, 1, &opt[0]);

    advertised = !localOnly && listenP2P != k_HSteamListenSocket_Invalid;
    running = listenIp != k_HSteamListenSocket_Invalid;
    return running;
}

SteamServerNet::~SteamServerNet()
{
    if (!running)
        return;
    if (listenIp)
        sockets()->CloseListenSocket(listenIp);
    if (listenP2P)
        sockets()->CloseListenSocket(listenP2P);
    if (group)
        sockets()->DestroyPollGroup(group);

    authCb.reset();
    SteamGameServer()->LogOff();
    SteamGameServer_Shutdown();
    g_queue = nullptr;
    g_net = nullptr;
}

void SteamServerNet::publish(int players, int maxPlayers, const std::string &map)
{
    if (!running)
        return;

    bw::log::hookSteam(true);
    ISteamGameServer *gs = SteamGameServer();
    gs->SetMaxPlayerCount(maxPlayers);
    gs->SetBotPlayerCount(players); // connected players are reported as the player count in the browser
    gs->SetMapName(map.c_str());
}

void SteamServerNet::pump()
{
    if (!running)
        return;

    SteamGameServer_RunCallbacks();
    sockets()->RunCallbacks();
    SteamNetworkingMessage_t *msgs[32];

    for (int n = sockets()->ReceiveMessagesOnPollGroup(g_group, msgs, 32); n > 0;
         n = sockets()->ReceiveMessagesOnPollGroup(g_group, msgs, 32))
    {
        for (int i = 0; i < n; i++)
        {
            const auto *d = static_cast<const uint8_t *>(msgs[i]->m_pData);
            queue.push_back({NetEvent::Receive, msgs[i]->m_conn, std::vector<uint8_t>(d, d + msgs[i]->m_cbSize)});
            msgs[i]->Release();
        }
    }
}

bool SteamServerNet::poll(NetEvent &out)
{
    if (queue.empty())
        pump();
    if (queue.empty())
        return false;

    out = std::move(queue.front());
    queue.pop_front();
    return true;
}

void SteamServerNet::send(uint64_t conn, const Writer &w, bool reliable)
{
    sent++;
    if (tap)
        tap(conn, w.b, reliable);
    if (running)
        sockets()->SendMessageToConnection(
            HSteamNetConnection(conn), w.b.data(), uint32_t(w.b.size()),
            (reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable) |
                k_nSteamNetworkingSend_NoNagle,
            nullptr);
}

void SteamServerNet::flush()
{
    // Messages are sent with NoNagle, so they leave immediately; kept as the per-tick hook of the old transport API.
}

AuthResult SteamServerNet::beginAuth(uint64_t conn, uint64_t claimedId, const std::vector<uint8_t> &ticket)
{
    if (claimedId == 0)
        return AuthResult::Rejected;
    if (verifier)
        return verifier(claimedId, ticket);
    if (devAuth)
    {
        bool match = ticket.size() == sizeof DEV_TICKET - 1 && std::equal(ticket.begin(), ticket.end(), DEV_TICKET);
        return match && claimedId < DEV_ID_LIMIT ? AuthResult::Accepted : AuthResult::Rejected;
    }
    if (!running || ticket.empty() || ticket.size() > 2048 || !CSteamID(claimedId).BIndividualAccount() ||
        authBySteamId.count(claimedId))
        return AuthResult::Rejected;

    EBeginAuthSessionResult r =
        SteamGameServer()->BeginAuthSession(ticket.data(), int(ticket.size()), CSteamID(claimedId));
    if (r != k_EBeginAuthSessionResultOK)
        return AuthResult::Rejected;
    authBySteamId[claimedId] = conn;
    return AuthResult::Pending;
}

void SteamServerNet::endAuth(uint64_t conn)
{
    for (auto it = authBySteamId.begin(); it != authBySteamId.end(); ++it)
        if (it->second == conn)
        {
            SteamGameServer()->EndAuthSession(CSteamID(it->first));
            authBySteamId.erase(it);
            return;
        }
}

void SteamServerNet::kick(uint64_t conn, bool linger)
{
    if (offline)
        queue.push_back({NetEvent::Disconnect, conn, {}});
    if (!running)
        return;

    sockets()->CloseConnection(HSteamNetConnection(conn), 0, nullptr, linger);
    queue.push_back({NetEvent::Disconnect, conn, {}});
}

} // namespace net
