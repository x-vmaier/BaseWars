/**
 * @file NetGarbage.cpp
 * @brief Selftest: malformed, oversized and out-of-range packets never crash or corrupt the server.
 */
#include "server/SelfTests.hpp"

#include <climits>
#include <cmath>
#include <random>

namespace selftest
{

namespace
{
std::vector<std::vector<uint8_t>> payloads(std::mt19937 &rng)
{
    std::vector<std::vector<uint8_t>> v;
    v.push_back({});
    v.push_back({0xFF});
    v.push_back(std::vector<uint8_t>(3, 0xFF));
    v.push_back(std::vector<uint8_t>(64, 0xFF));
    v.push_back(std::vector<uint8_t>(64, 0x00));
    v.push_back(std::vector<uint8_t>(70000, 0x7F)); // oversized
    for (int n : {1, 2, 5, 9, 17, 40, 200})
    {
        std::vector<uint8_t> d;
        d.resize(size_t(n));
        for (auto &b : d)
            b = uint8_t(rng());
        v.push_back(std::move(d));
    }
    return v;
}

int usedProfiles(World &w)
{
    int n = 0;
    for (int i = 1; i <= MAX_PLAYERS; i++)
        n += w.pl[i].used ? 1 : 0;
    return n;
}
} // namespace

void netGarbageTest(Ctx &t)
{
    Fixture f;
    std::mt19937 rng(7);
    int id = f.join(1, 500);
    f.run(5);
    auto pay = payloads(rng);

    // 1. A connection that never said hello cannot change anything, whatever it sends.
    uint64_t before = stateHash(f);
    for (int type = 0; type < 256; type++)
        for (auto &p : pay)
        {
            if (type == C_Hello)
                continue; // a valid hello legitimately creates a profile; malformed ones are covered below
            std::vector<uint8_t> d{uint8_t(type)};
            d.insert(d.end(), p.begin(), p.end());
            f.sys.packets.onPacket(777, d);
        }
    t.check(stateHash(f) == before, "packets from an unauthenticated connection change nothing");

    // 2. Malformed hellos are dropped (no profile created).
    int usedBefore = usedProfiles(f.world);
    for (auto &p : pay)
    {
        std::vector<uint8_t> d{C_Hello};
        d.insert(d.end(), p.begin(), p.end());
        f.sys.packets.onPacket(900, d);
    }
    t.check(usedProfiles(f.world) == usedBefore, "garbage hellos create no profile");

    // 3. A joined player sending garbage of every type: no crash, state stays consistent.
    for (int type = 0; type < 256; type++)
        for (auto &p : pay)
        {
            std::vector<uint8_t> d{uint8_t(type)};
            d.insert(d.end(), p.begin(), p.end());
            f.sys.packets.onPacket(500, d);
        }
    f.run(10);
    t.invariants(f, "after garbage from a joined player");
    t.check(f.hallOf(id) != entt::null, "player's Town Hall still exists");

    // 4. Targeted out-of-range values.
    auto send = [&](const Writer &w) { f.sys.packets.onPacket(500, w.b); };
    for (int32_t bad : {INT32_MIN, -1, 1 << 30, INT32_MAX})
    {
        Writer b;
        b.put<uint8_t>(C_Build).put<uint8_t>(K_Lumber).put<int32_t>(bad).put<int32_t>(bad);
        send(b);
        Writer c;
        c.put<uint8_t>(C_Cam).put<float>(float(bad)).put<float>(float(bad));
        send(c);
    }
    Writer kind;
    kind.put<uint8_t>(C_Build).put<uint8_t>(200).put<int32_t>(0).put<int32_t>(0);
    send(kind);
    Writer mv;
    mv.put<uint8_t>(C_Move).put<uint16_t>(65535).put<float>(NAN).put<float>(INFINITY);
    send(mv);
    Writer tf;
    tf.put<uint8_t>(C_Transfer).put<uint8_t>(2).put<uint8_t>(0).put<float>(-1e9f);
    send(tf);
    f.run(5);
    t.invariants(f, "after targeted packets");
}

} // namespace selftest
