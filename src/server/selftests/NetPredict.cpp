/**
 * @file NetPredict.cpp
 * @brief Selftest: command prediction. The server refuses an impossible move with S_CmdReject (and a valid one
 * without), the pending queue drains on ack, a rejected order is rolled back, nothing leaks, and the local
 * movement start stays bounded and fades without a snap (150 ms latency model).
 */
#include "common/Predict.hpp"
#include "server/SelfTests.hpp"

namespace selftest
{

void netPredictTest(Ctx &t)
{
    // Server side: which seqs get a reject.
    Fixture f;
    std::vector<uint32_t> rejected;
    f.world.net.tap = [&](uint64_t, const std::vector<uint8_t> &d, bool)
    {
        if (!d.empty() && d[0] == S_CmdReject)
        {
            Reader r{d.data() + 1, d.data() + d.size()};
            rejected.push_back(r.get<uint32_t>());
        }
    };
    int id = f.join(1, 1000);
    Pos at = f.world.posOf(f.hallOf(id));
    auto u = f.world.makeUnit(K_Warrior, uint8_t(id), at.x + 3.f, at.y + 3.f);
    uint32_t uid = f.world.reg.get<Uid>(u).id;

    auto move = [&](uint32_t unit, float x, float y)
    {
        Writer w;
        w.put<uint8_t>(C_Move).put<uint8_t>(1).put<uint32_t>(unit).put<float>(x).put<float>(y);
        f.send(1000, w);
        return f.seqOf[1000];
    };
    move(uid, at.x + 8.f, at.y + 3.f);
    uint32_t bad = move(uid + 5000, at.x + 8.f, at.y + 3.f); // not a unit of ours
    f.run(.5);
    t.check(rejected.size() == 1 && rejected[0] == bad, "only the impossible move is rejected, with its seq");

    // Client side: 150 ms round trip, snapshots every 100 ms echo the newest processed seq.
    PendingQueue q;
    PredOffset off;
    q.push({1, 0.0, 10.f, 0.f, {uid}});
    q.push({2, 0.05, 0.f, 10.f, {uid + 1}});
    float maxOff = 0, px = 0, snap = 0;
    double ackAt = .15;
    bool acked = false;
    for (double now = 0; now < 1.0; now += 1.0 / 60.0)
    {
        bool has = q.targetOf(uid, px, snap); // tx,ty of the order in flight
        float tx = px, ty = snap;
        off.step(1.f / 60.f, has, 0, 0, tx, ty);
        maxOff = std::max(maxOff, std::hypot(off.x, off.y));
        if (!acked && now >= ackAt)
        {
            q.ack(1);
            acked = true;
        }
    }
    t.check(maxOff > .1f && maxOff <= PredOffset::MAX_TILES + 1e-4f, "units start moving at once, bounded drift");
    t.check(q.q.size() == 1 && q.q[0].seq == 2, "ack drops commands up to its seq only");
    t.check(std::hypot(off.x, off.y) < .01f, "the local offset fades out after the ack");

    PendingCmd got;
    t.check(q.reject(2, &got) && got.seq == 2 && q.q.empty(), "a rejected command is rolled back");
    t.check(!q.reject(2), "rejecting an already settled command is a no-op");

    // Unacknowledged commands (link lost) expire; flooding is capped.
    q.push({3, 0.0, 1.f, 1.f, {uid}});
    q.expire(PendingQueue::LIFE + .1);
    t.check(q.q.empty(), "an unacknowledged command expires, nothing leaks");
    for (uint32_t i = 1; i <= 200; i++)
        q.push({i, 0.0, 0.f, 0.f, {}});
    t.check(q.q.size() == PendingQueue::CAP, "pending queue is capped");
    t.invariants(f, "after predicted commands");
}

} // namespace selftest
