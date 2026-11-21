/**
 * @file SelfTests.cpp
 * @brief Selftest registry, runner and the shared Fixture / Ctx helpers.
 */
#include "server/SelfTests.hpp"

#include "common/Placement.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace selftest
{

namespace
{
const Scenario SCENARIOS[] = {
    {"boot", "world generation, one player, 5 simulated minutes", bootTest},
    {"saveload", "save, load into a fresh world, state hash identical", saveloadTest},
    {"economy", "build, produce, train with expected resource changes", economyTest},
    {"raid", "forced raid: raiders arrive, hall survives or raid expires", raidTest},
    {"net-garbage", "malformed / oversized / out-of-range packets", netGarbageTest},
    {"identity", "SteamID-only ownership: separate towns, reconnects, forged / unanswered logins", identityTest},
    {"net-snapshot", "chunked unreliable snapshots, lost chunks, command seq ack, protocol version", netSnapshotTest},
    {"pacing", "pacing director: phase cycle, grace / aftermath, strength cap, adaptive factor, save, offline pause",
     pacingTest},
    {"defeat", "defeat: burning ruin, resettle offers, blocked commands, timeouts", defeatTest},
    {"net-predict", "command prediction: reject message, pending queue ack / rollback / expiry, bounded local offset",
     netPredictTest},
    {"interp", "client interpolation: no snaps, low jitter under latency + loss, stops, teleports", interpTest},
    {"log-rotate", "log files: previous run archived into logs/, archive capped", logRotateTest},
    {"invariants", "invariant checker catches an orphaned job", invariantsTest},
};

std::string tempSave(uint32_t seed)
{
    static int n = 0;
    auto dir = std::filesystem::temp_directory_path();
    return (dir / ("bw_selftest_" + std::to_string(seed) + "_" + std::to_string(n++) + ".bin")).string();
}
} // namespace

Fixture::Fixture(uint32_t seed) : sim(seed, tempSave(seed)), savePath(sim.world.savePath)
{
    cfg() = ServerConfig{};          // every scenario starts from the documented defaults ...
    cfg().hostileFactionPercent = 0; // ... except that nobody raids unless the scenario forces it
    world.net.startOffline();
    std::error_code ec;
    std::filesystem::remove(savePath, ec);
}

Fixture::~Fixture()
{
    std::error_code ec;
    std::filesystem::remove(savePath, ec);
}

void Fixture::send(uint64_t conn, const Writer &w)
{
    bool hello = !w.b.empty() && w.b[0] == C_Hello;
    sys.packets.onPacket(conn, hello ? w.b : withSeq(w.b, ++seqOf[conn])); // like Session::send
}

int Fixture::join(uint64_t steamId, uint64_t conn)
{
    return joinWithTicket(steamId, std::vector<uint8_t>(DEV_TICKET, DEV_TICKET + sizeof DEV_TICKET - 1), conn);
}

int Fixture::joinWithTicket(uint64_t steamId, const std::vector<uint8_t> &ticket, uint64_t conn)
{
    if (!conn)
        conn = nextConn++;
    Writer w;
    w.put<uint8_t>(C_Hello).put<uint16_t>(PROTOCOL_VERSION);
    w.put<uint64_t>(steamId);
    w.put<uint16_t>(uint16_t(ticket.size()));
    w.bytes(ticket.data(), ticket.size());
    w.put<uint16_t>(0);
    send(conn, w);
    auto it = world.connPlayer.find(conn);
    return it == world.connPlayer.end() ? 0 : it->second;
}

void Fixture::leave(uint64_t conn)
{
    sys.session.onDisconnect(conn);
}

bool Fixture::findSite(int id, Kind k, int &tx, int &ty)
{
    entt::entity th = hallOf(id);
    if (th == entt::null)
        return false;
    Pos c = world.posOf(th);
    PlaceWorld w{world, id};
    for (int r = 4; r < 14; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
            {
                if (std::max(std::abs(dx), std::abs(dy)) != r)
                    continue;
                int x = int(c.x) + dx, y = int(c.y) + dy;
                if (checkPlace(w, k, x, y) == P_OK && sys.clearing.blockedTiles(k, x, y).empty())
                {
                    tx = x;
                    ty = y;
                    return true;
                }
            }
    return false;
}

void Ctx::check(bool ok, const std::string &what)
{
    if (ok)
        return;
    failures++;
    spdlog::error("  [{}] FAIL: {}", name, what);
}

void Ctx::invariants(Fixture &f, const char *when)
{
    for (const std::string &v : f.world.checkInvariants())
        check(false, std::string("invariant (") + when + "): " + v);
}

uint64_t stateHash(Fixture &f)
{
    f.sys.save.saveGame();
    std::vector<uint8_t> bytes;
    if (FILE *fp = std::fopen(f.savePath.c_str(), "rb"))
    {
        uint8_t buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, fp)) > 0)
            bytes.insert(bytes.end(), buf, buf + n);
        std::fclose(fp);
    }
    uint64_t h = 1469598103934665603ull; // FNV-1a
    for (uint8_t b : bytes)
        h = (h ^ b) * 1099511628211ull;
    return h ^ uint64_t(bytes.size());
}

namespace
{
bool runOne(const Scenario &s)
{
    spdlog::info("[ RUN  ] {} - {}", s.name, s.about);
    auto t0 = std::chrono::steady_clock::now();
    Ctx ctx(s.name);
    s.run(ctx);
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    spdlog::info("[ {} ] {} ({:.1f}s)", ctx.failures ? "FAIL" : " OK ", s.name, sec);
    return ctx.failures == 0;
}
} // namespace

int runCli(const std::string &what)
{
    if (what == "list")
    {
        for (const Scenario &s : SCENARIOS)
            std::printf("%s\t%s\n", s.name, s.about);
        return 0;
    }
    int failed = 0, ran = 0;
    for (const Scenario &s : SCENARIOS)
        if (what == "all" || what == s.name)
        {
            ran++;
            failed += runOne(s) ? 0 : 1;
        }
    if (!ran)
    {
        spdlog::error("Unknown selftest '{}' (try: --selftest list)", what);
        return 1;
    }
    spdlog::info("{}/{} selftests passed", ran - failed, ran);
    return failed ? 1 : 0;
}

} // namespace selftest
