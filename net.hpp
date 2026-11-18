#pragma once
// Transport abstraction used by both executables. ENet gives direct IP connections; when the game is built
// with the Steamworks SDK (BW_STEAM) players can also connect through Steam Networking Sockets (P2P relay),
// so a server is reachable "from anywhere" without port forwarding.
#include "shared.hpp"

#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

inline constexpr uint64_t STEAM_BIT = 1ull << 63;     // connection ids with this bit belong to Steam, the rest are ENet peers

struct NetEvent {
    enum Type { Connect, Disconnect, Receive } type = Receive;
    uint64_t conn = 0;
    std::vector<uint8_t> data;
};

// One row of the main-menu server browser.
struct ServerEntry {
    std::string name, map, tags, address;
    int ping = 0, players = 0, maxPlayers = 0;
    uint16_t port = DEFAULT_PORT;
    uint64_t steamId = 0;       // non-zero: reachable through Steam P2P
    bool friends = false, password = false, local = false;
};

#include "steam_layer.hpp"

// ------------------------------------------------------------------------------------------------ server side
class ServerNet {
public:
    bool start(uint16_t port, int maxPeers, const std::string& serverName) {
        ENetAddress a{};
        a.host = ENET_HOST_ANY;
        a.port = port;
        host = enet_host_create(&a, size_t(maxPeers), 2, 0, 0);
        if (!host) return false;
#ifdef BW_STEAM
        steam = steamlayer::serverInit(port, serverName, q);
#else
        (void)serverName;
#endif
        return true;
    }
    bool steamActive() const {
#ifdef BW_STEAM
        return steam;
#else
        return false;
#endif
    }
    void publish(int players, int maxPlayers, const std::string& map) {
#ifdef BW_STEAM
        if (steam) steamlayer::serverPublish(players, maxPlayers, map);
#else
        (void)players; (void)maxPlayers; (void)map;
#endif
    }
    // Next pending event, or false when the queue is empty.
    bool poll(NetEvent& out) {
        if (q.empty()) pump();
        if (q.empty()) return false;
        out = std::move(q.front());
        q.pop_front();
        return true;
    }
    void send(uint64_t conn, const Writer& w) {
        if (conn & STEAM_BIT) {
#ifdef BW_STEAM
            steamlayer::serverSend(uint32_t(conn & 0xffffffffu), w.b.data(), uint32_t(w.b.size()));
#endif
            return;
        }
        auto* peer = reinterpret_cast<ENetPeer*>(uintptr_t(conn));
        if (live.count(peer)) sendPacket(peer, w);
    }
    void kick(uint64_t conn, bool linger) {
        if (conn & STEAM_BIT) {
#ifdef BW_STEAM
            steamlayer::serverClose(uint32_t(conn & 0xffffffffu), linger);
            q.push_back({NetEvent::Disconnect, conn, {}});
#endif
            return;
        }
        auto* peer = reinterpret_cast<ENetPeer*>(uintptr_t(conn));
        if (!live.count(peer)) return;
        if (linger) enet_peer_disconnect_later(peer, 0); else enet_peer_disconnect_now(peer, 0);
        if (!linger) { live.erase(peer); q.push_back({NetEvent::Disconnect, conn, {}}); }
    }
    void flush() { if (host) enet_host_flush(host); }
    ~ServerNet() {
#ifdef BW_STEAM
        if (steam) steamlayer::serverShutdown();
#endif
        if (host) enet_host_destroy(host);
    }

private:
    void pump() {
        ENetEvent ev;
        while (host && enet_host_service(host, &ev, 0) > 0) {
            uint64_t id = uint64_t(reinterpret_cast<uintptr_t>(ev.peer));
            switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT: live.insert(ev.peer); q.push_back({NetEvent::Connect, id, {}}); break;
            case ENET_EVENT_TYPE_RECEIVE:
                if (live.count(ev.peer)) q.push_back({NetEvent::Receive, id, std::vector<uint8_t>(ev.packet->data, ev.packet->data + ev.packet->dataLength)});
                enet_packet_destroy(ev.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT: if (live.erase(ev.peer)) q.push_back({NetEvent::Disconnect, id, {}}); break;
            default: break;
            }
        }
#ifdef BW_STEAM
        if (steam) steamlayer::serverRun();
#endif
    }
    ENetHost* host = nullptr;
    std::unordered_set<ENetPeer*> live;
    std::deque<NetEvent> q;
#ifdef BW_STEAM
    bool steam = false;
#endif
};

// ------------------------------------------------------------------------------------------------ client side
class ClientNet {
public:
    ~ClientNet() { close(); }
    bool connectIP(const std::string& ip, uint16_t port) {
        close();
        host = enet_host_create(nullptr, 1, 2, 0, 0);
        ENetAddress a{};
        if (!host || enet_address_set_host(&a, ip.c_str()) != 0) { close(); return false; }
        a.port = port;
        peer = enet_host_connect(host, &a, 2, 0);
        return peer != nullptr;
    }
    bool connectSteam(uint64_t steamId) {
#ifdef BW_STEAM
        close();
        steamConn = steamlayer::clientConnect(steamId, q);
        return steamConn != 0;
#else
        (void)steamId;
        return false;
#endif
    }
    bool active() const {
#ifdef BW_STEAM
        if (steamConn) return true;
#endif
        return peer != nullptr;
    }
    bool poll(NetEvent& out) {
        if (q.empty()) pump();
        if (q.empty()) return false;
        out = std::move(q.front());
        q.pop_front();
        return true;
    }
    void send(const Writer& w) {
#ifdef BW_STEAM
        if (steamConn) { steamlayer::clientSend(steamConn, w.b.data(), uint32_t(w.b.size())); return; }
#endif
        if (peer && connectedEnet) sendPacket(peer, w);
    }
    void close() {
#ifdef BW_STEAM
        if (steamConn) { steamlayer::clientClose(steamConn); steamConn = 0; }
#endif
        if (peer && connectedEnet) { enet_peer_disconnect(peer, 0); enet_host_flush(host); }
        if (host) enet_host_destroy(host);
        host = nullptr;
        peer = nullptr;
        connectedEnet = false;
        q.clear();
    }

private:
    void pump() {
        ENetEvent ev;
        while (host && enet_host_service(host, &ev, 0) > 0) {
            switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT: connectedEnet = true; q.push_back({NetEvent::Connect, 0, {}}); break;
            case ENET_EVENT_TYPE_RECEIVE: q.push_back({NetEvent::Receive, 0, std::vector<uint8_t>(ev.packet->data, ev.packet->data + ev.packet->dataLength)}); enet_packet_destroy(ev.packet); break;
            case ENET_EVENT_TYPE_DISCONNECT: connectedEnet = false; q.push_back({NetEvent::Disconnect, 0, {}}); break;
            default: break;
            }
        }
#ifdef BW_STEAM
        if (steamConn) steamlayer::clientRun(steamConn);
#endif
    }
    ENetHost* host = nullptr;
    ENetPeer* peer = nullptr;
    bool connectedEnet = false;
    std::deque<NetEvent> q;
#ifdef BW_STEAM
    uint32_t steamConn = 0;
#endif
};
