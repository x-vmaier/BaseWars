/**
 * @file NetSnapshot.cpp
 * @brief Selftest: S_State is split into unreliable chunks under the MTU, a lost chunk never deletes entities,
 * the snapshot echoes the last command seq, stale commands and a wrong protocol version are rejected.
 */
#include "server/SelfTests.hpp"

#include <algorithm>
#include <set>

namespace selftest
{

namespace
{
struct Chunk
{
    SnapHeader h;
    std::vector<uint32_t> uids;
};

constexpr size_t RECORD_BYTES = 24; // see Replication::writeStateEntity

// Chunks of the snapshots sent to `conn`, in send order; also checks the size cap and the reliability flag.
struct Capture
{
    std::vector<Chunk> chunks;
    size_t biggest = 0, wrongFlag = 0;
    bool sawText = false;

    void onSend(const std::vector<uint8_t> &d, bool reliable)
    {
        if (d.empty())
            return;
        if (d[0] == S_Text)
            sawText = true;
        if (d[0] != S_State)
            return;
        biggest = std::max(biggest, d.size());
        wrongFlag += reliable ? 1 : 0;
        Reader r{d.data(), d.data() + d.size()};
        r.get<uint8_t>();
        Chunk c;
        c.h = getSnapHeader(r);
        if (c.h.index == 0)
            r.bytes(R_Count * 4 + FACTION_COUNT + (K_Monk - K_Pawn + 1));
        for (int n = r.get<uint16_t>(); n > 0 && r.ok; n--)
        {
            const uint8_t *rec = r.bytes(RECORD_BYTES);
            uint32_t uid = 0;
            if (rec)
                std::memcpy(&uid, rec, 4);
            c.uids.push_back(uid);
        }
        chunks.push_back(std::move(c));
    }
};

// The client's rule in miniature: apply every chunk, drop unseen entities only when the tick is complete.
struct Mirror
{
    SnapshotTracker tracker;
    std::set<uint32_t> known, seen;
    void apply(const Chunk &c)
    {
        bool fresh = false;
        auto res = tracker.add(c.h, fresh);
        if (res == SnapshotTracker::Result::Reject)
            return;
        if (fresh)
            seen.clear();
        seen.insert(c.uids.begin(), c.uids.end());
        known.insert(c.uids.begin(), c.uids.end());
        if (res == SnapshotTracker::Result::Complete)
            known = seen;
    }
};
} // namespace

void netSnapshotTest(Ctx &t)
{
    Fixture f;
    Capture cap;
    f.world.net.tap = [&](uint64_t, const std::vector<uint8_t> &d, bool reliable) { cap.onSend(d, reliable); };
    int id = f.join(1, 1000);
    auto hall = f.hallOf(id);
    Pos at = f.world.posOf(hall);
    for (int i = 0; i < 150; i++)
        f.world.makeUnit(K_Warrior, uint8_t(id), at.x + float(i % 15) * .5f, at.y + 3.f + float(i / 15) * .5f);
    f.run(1);
    cap.chunks.clear();
    f.run(1);

    t.check(cap.biggest > 0 && cap.biggest <= SNAPSHOT_MTU, "every snapshot chunk stays under the MTU");
    t.check(cap.wrongFlag == 0, "snapshots are sent unreliably");
    t.check(!cap.chunks.empty() && cap.chunks[0].h.count > 3, "a large world needs several chunks per snapshot");

    // Group the captured chunks by tick.
    std::vector<std::vector<Chunk>> ticks;
    for (auto &c : cap.chunks)
    {
        if (ticks.empty() || ticks.back()[0].h.tick != c.h.tick)
            ticks.emplace_back();
        ticks.back().push_back(c);
    }
    t.check(ticks.size() >= 2, "two snapshots per simulated second at the default interval");
    if (ticks.size() < 2)
        return;
    t.check(ticks[0].size() == ticks[0][0].h.count, "all chunks of a tick are sent");

    // A lost chunk: tick A arrives without one chunk, tick B whole. The mirror must end up with exactly B.
    Mirror m;
    for (size_t i = 0; i < ticks[0].size(); i++)
        if (i != 1)
            m.apply(ticks[0][i]);
    size_t afterPartial = m.known.size();
    t.check(afterPartial > 0 && afterPartial < ticks[0][0].h.count * 1000, "partial tick applied");
    for (auto &c : ticks[1])
        m.apply(c);
    std::set<uint32_t> expect;
    for (auto &c : ticks[1])
        expect.insert(c.uids.begin(), c.uids.end());
    t.check(m.known == expect && expect.size() >= 150, "the next whole snapshot restores the exact state");
    m.apply(ticks[0][0]);
    t.check(m.known == expect, "a late chunk of an older tick is ignored");

    // Command envelope: the newest accepted seq is echoed, stale / replayed seqs change nothing.
    Writer cam;
    cam.put<uint8_t>(C_Cam).put<float>(12.f).put<float>(13.f);
    f.send(1000, cam);
    uint32_t seq = f.seqOf[1000];
    cap.chunks.clear();
    f.run(.5);
    t.check(!cap.chunks.empty() && cap.chunks.back().h.ackSeq == seq, "snapshot echoes the last processed seq");
    Writer stale;
    stale.put<uint8_t>(C_Cam).put<float>(99.f).put<float>(99.f);
    f.sys.packets.onPacket(1000, withSeq(stale.b, seq));
    t.check(f.world.pl[id].cam.x == 12.f, "a replayed seq is dropped");
    f.sys.packets.onPacket(1000, stale.b);
    t.check(f.world.pl[id].cam.x == 12.f, "a command without envelope is dropped");

    // Protocol version: a client of another version is refused with a text and gets no profile.
    Writer hello;
    hello.put<uint8_t>(C_Hello).put<uint16_t>(PROTOCOL_VERSION + 1).put<uint64_t>(2);
    hello.put<uint16_t>(uint16_t(sizeof DEV_TICKET - 1)).bytes(DEV_TICKET, sizeof DEV_TICKET - 1);
    hello.put<uint16_t>(0);
    cap.sawText = false;
    f.send(1001, hello);
    t.check(f.world.connPlayer.count(1001) == 0 && cap.sawText, "version mismatch is rejected with a message");
    t.invariants(f, "after snapshots");
}

} // namespace selftest
