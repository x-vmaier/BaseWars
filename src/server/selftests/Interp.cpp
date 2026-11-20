/**
 * @file Interp.cpp
 * @brief Selftest: client interpolation under 120 ms latency, jitter and 3% snapshot loss shows no snaps and
 * low velocity jitter; stops hold, teleports are not smeared, the clock recovers from a stall.
 */
#include "common/Interp.hpp"
#include "server/SelfTests.hpp"

#include <algorithm>
#include <deque>

namespace selftest
{

namespace
{
struct Arrival
{
    double at;
    uint32_t tick;
    float x, y;
};

// One unit walks at `speed` tiles/s, then stops at `stopAt`; snapshots every `interval` ticks travel with
// 120 ms +-30 ms latency and `lossPct` loss; the client renders 60 fps and measures like BW_STUTTER=1.
struct Run
{
    StutterStats stats;
    float restErr = 0; // distance of the rendered unit to its rest position at the end
};

Run simulate(float lossPct, double seconds, double stopAt)
{
    const int interval = 2;
    const float speed = 3.f;
    uint32_t rng = 12345;
    auto rnd = [&]() { return float((rng = rng * 1664525u + 1013904223u) >> 8) / float(1u << 24); };

    std::deque<Arrival> wire;
    PosBuffer buf;
    InterpClock clock;
    Run out;
    float px = 0, vx = 0, restX = 0;
    double next = 0.0;
    bool started = false;
    for (double now = 0; now < seconds; now += 1.0 / 60.0)
    {
        while (next <= now) // server side: sample the unit at its tick
        {
            double t = std::min(next, stopAt);
            float x = float(t * speed);
            restX = x;
            if (rnd() * 100.f >= lossPct)
                wire.push_back({next + .12 + (rnd() - .5) * .06, uint32_t(next * TICK_HZ + 100), x, 0});
            next += double(interval) / TICK_HZ;
        }
        std::stable_sort(wire.begin(), wire.end(), [](const Arrival &a, const Arrival &b) { return a.at < b.at; });
        while (!wire.empty() && wire.front().at <= now)
        {
            buf.push(wire.front().tick, wire.front().x, wire.front().y);
            clock.onSnapshot(wire.front().tick, now);
            wire.pop_front();
        }
        double rt = clock.advance(now, clock.delay());
        if (buf.n < 2)
            continue;
        float nx = buf.at(rt).x;
        if (started && now > 2.0) // skip the join transient
            out.stats.add(std::abs((nx - px) * 60.f - vx), std::abs(nx - px));
        vx = (nx - px) * 60.f;
        px = nx;
        started = true;
    }
    out.restErr = std::abs(px - restX);
    return out;
}
} // namespace

void interpTest(Ctx &t)
{
    // Walking, 120 ms latency, 3% loss: smooth.
    Run lossy = simulate(3.f, 40, 1e9);
    float p99 = lossy.stats.p99();
    std::printf("  interp: p99 jitter %.3f tiles/s, max %.3f, snaps %d\n", p99, lossy.stats.maxJitter,
                lossy.stats.snaps);
    t.check(lossy.stats.snaps == 0, "no snaps (> 1 tile per frame) under latency, jitter and loss");
    t.check(p99 < 0.6f, "p99 velocity jitter below 0.6 tiles/s");

    // The unit stops: the rendered position settles on the rest position, no overshoot beyond the clamp.
    Run stop = simulate(0.f, 20, 8.0);
    t.check(stop.restErr < .05f, "a stopped unit rests exactly at its authoritative position");

    // Buffer rules on their own.
    PosBuffer b;
    b.push(10, 0, 0);
    b.push(12, 2, 0);
    b.push(12, 99, 0); // duplicate tick ignored
    b.push(8, 50, 0);  // older tick ignored
    t.check(b.n == 2 && std::abs(b.at(11).x - 1.f) < 1e-4f, "interpolates between bracketing samples");
    t.check(b.at(1).x == 0.f, "holds the oldest sample before the buffer");
    t.check(std::abs(b.at(12.5).x - 2.5f) < 1e-4f && std::abs(b.at(100).x - 3.f) < 1e-4f,
            "extrapolation is velocity based and clamped");
    PosBuffer j;
    j.push(10, 0, 0);
    j.push(12, 30, 0);
    t.check(j.at(11).x == 0.f && j.at(12).x == 30.f, "a teleport steps, it is never smeared across the map");

    // Clock: a long stall resets the render clock instead of slewing for minutes.
    InterpClock c;
    c.onSnapshot(100, 0);
    c.advance(0, 3);
    c.onSnapshot(1000, 1);
    t.check(std::abs(c.advance(1, 3) - (1000 - 3 + 0.0)) < 40, "clock jumps after a long stall");
    t.check(c.delay() >= 3.0 && c.delay(200) == 4.0, "delay is >= 150 ms, override in ms");
}

} // namespace selftest
