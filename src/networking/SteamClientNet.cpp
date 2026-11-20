/**
 * @file SteamClientNet.cpp
 * @brief Client transport: Steam Networking Sockets by IP or by server SteamID (Datagram Relay).
 */
#include "networking/SteamClientNet.hpp"

#include "networking/FakeNet.hpp"

#include <steam/isteamnetworkingutils.h>
#include <steam/steam_api.h>

#include <cstdio>
#include <cstdlib>

namespace net
{

namespace
{
std::deque<NetEvent> *g_queue = nullptr;

void onStatus(SteamNetConnectionStatusChangedCallback_t *info)
{
    auto *s = SteamNetworkingSockets();
    if (std::getenv("BW_NETLOG"))
        std::fprintf(stderr, "[net] state %d -> %d (%s)\n", int(info->m_eOldState), int(info->m_info.m_eState),
                     info->m_info.m_szEndDebug);

    switch (info->m_info.m_eState)
    {
    case k_ESteamNetworkingConnectionState_Connected:
        if (g_queue)
            g_queue->push_back({NetEvent::Connect, 0, {}});
        break;
    case k_ESteamNetworkingConnectionState_ClosedByPeer:
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        s->CloseConnection(info->m_hConn, 0, nullptr, false);
        if (g_queue)
            g_queue->push_back({NetEvent::Disconnect, 0, {}});
        break;
    default:
        break;
    }
}
SteamNetworkingConfigValue_t statusOption()
{
    SteamNetworkingConfigValue_t opt;
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void *>(&onStatus));
    return opt;
}
} // namespace

bool SteamClientNet::connectIP(const std::string &ip, uint16_t port)
{
    close();
    if (!SteamNetworkingSockets())
        return false;

    SteamNetworkingIPAddr addr;
    addr.Clear();
    if (!addr.ParseString(ip.c_str()))
        return false; // also accepts "host:port"; the explicit port below is the fallback

    if (addr.m_port == 0)
        addr.m_port = port;

    applyFakeNetworkEnv();
    g_queue = &queue;
    SteamNetworkingConfigValue_t opt[2] = {statusOption(), {}};
    opt[1].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);
    conn = SteamNetworkingSockets()->ConnectByIPAddress(addr, 2, opt);
    return conn != 0;
}

bool SteamClientNet::connectSteam(uint64_t serverSteamId)
{
    close();
    if (!SteamNetworkingSockets())
        return false;

    SteamNetworkingIdentity ident;
    ident.Clear();
    ident.SetSteamID64(serverSteamId);
    applyFakeNetworkEnv();
    g_queue = &queue;
    auto opt = statusOption();
    conn = SteamNetworkingSockets()->ConnectP2P(ident, 0, 1, &opt); // routed through Steam Datagram Relay
    return conn != 0;
}

void SteamClientNet::pump()
{
    if (!conn || !SteamNetworkingSockets())
        return;

    SteamNetworkingSockets()->RunCallbacks();
    SteamNetworkingMessage_t *msgs[32];
    for (int n = SteamNetworkingSockets()->ReceiveMessagesOnConnection(conn, msgs, 32); n > 0;
         n = SteamNetworkingSockets()->ReceiveMessagesOnConnection(conn, msgs, 32))
    {
        for (int i = 0; i < n; i++)
        {
            const auto *d = static_cast<const uint8_t *>(msgs[i]->m_pData);
            queue.push_back({NetEvent::Receive, 0, std::vector<uint8_t>(d, d + msgs[i]->m_cbSize)});
            msgs[i]->Release();
        }
    }
}

bool SteamClientNet::poll(NetEvent &out)
{
    if (queue.empty())
        pump();
    if (queue.empty())
        return false;

    out = std::move(queue.front());
    queue.pop_front();
    return true;
}

void SteamClientNet::send(const Writer &w)
{
    if (conn)
        SteamNetworkingSockets()->SendMessageToConnection(conn, w.b.data(), uint32_t(w.b.size()),
                                                          k_nSteamNetworkingSend_Reliable, nullptr);
}

void SteamClientNet::close()
{
    if (conn && SteamNetworkingSockets())
        SteamNetworkingSockets()->CloseConnection(conn, 0, nullptr, true);
    conn = 0;
    queue.clear();
}

} // namespace net
