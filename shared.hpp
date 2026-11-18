#pragma once
// Shared constants, protocol and tiny serialization helpers for server + client.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI   // keep windows.h from clashing with raylib (Rectangle, DrawText...)
#define NOUSER  // (CloseWindow, ShowCursor...)
#endif
#include <enet/enet.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

inline constexpr int MAP_W = 96, MAP_H = 96, MAX_PLAYERS = 8, TICK_HZ = 20, DEFAULT_PORT = 7777, BUILD_RADIUS = 12, UNIT_CAP = 40;

enum Terrain : uint8_t { T_Clearing, T_Forest, T_Mountain, T_Sea };
enum Kind : uint8_t { K_TownHall, K_GoldMine, K_Lumber, K_Scout, K_Soldier, K_Knight, K_Count };
enum Msg : uint8_t { C_Build, C_Train, C_Move, C_Attack, C_Ally, S_Welcome, S_State, S_Fog, S_Text };

struct Pos { float x, y; };
struct IV { int x, y; };

struct KindInfo { const char* name; int gold, wood; float hp, speed, dps; int vision; bool building; };
// Town Hall "dps" is its defensive fire. vision = reveal radius in tiles (square).
inline constexpr KindInfo KINFO[K_Count] = {
    {"Town Hall", 0, 0, 600, 0, 15, 3, true},
    {"Gold Mine", 50, 40, 150, 0, 0, 1, true},
    {"Lumber Camp", 40, 30, 150, 0, 0, 1, true},
    {"Scout", 30, 0, 20, 5.5f, 0, 4, false},
    {"Soldier", 40, 20, 60, 2.4f, 9, 1, false},
    {"Knight", 100, 60, 160, 3.2f, 24, 1, false}};

inline bool inb(int x, int y) { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H; }
inline int ix(int x, int y) { return y * MAP_W + x; }
inline bool walkableT(uint8_t t) { return t == T_Clearing || t == T_Forest; }
inline uint32_t hash2(int x, int y, uint32_t s) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

struct Writer {
    std::vector<uint8_t> b;
    template <class T> Writer& put(T v) {
        static_assert(std::is_trivially_copyable_v<T>);
        auto p = reinterpret_cast<const uint8_t*>(&v);
        b.insert(b.end(), p, p + sizeof(T));
        return *this;
    }
    void bytes(const void* d, size_t n) { auto p = static_cast<const uint8_t*>(d); b.insert(b.end(), p, p + n); }
    void str(const std::string& s) { put<uint16_t>(uint16_t(s.size())); bytes(s.data(), s.size()); }
};

struct Reader {
    const uint8_t* p;
    const uint8_t* e;
    bool ok = true;
    template <class T> T get() {
        T v{};
        if (size_t(e - p) < sizeof(T)) { ok = false; return v; }
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        return v;
    }
    const uint8_t* bytes(size_t n) {
        if (size_t(e - p) < n) { ok = false; return nullptr; }
        auto r = p;
        p += n;
        return r;
    }
    std::string str() {
        size_t n = get<uint16_t>();
        auto d = bytes(n);
        return d ? std::string(reinterpret_cast<const char*>(d), n) : std::string();
    }
};

inline void sendPacket(ENetPeer* peer, const Writer& w) {
    enet_peer_send(peer, 0, enet_packet_create(w.b.data(), w.b.size(), ENET_PACKET_FLAG_RELIABLE));
}
