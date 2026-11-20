/**
 * @file Interp.hpp
 * @brief Client-side entity interpolation: timestamped position buffer, render clock, jitter metric.
 * Header-only and free of raylib so the `interp` selftest can drive it headless.
 */
#pragma once
#include "common/Shared.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Last few authoritative positions of one entity, stamped with the server tick of the snapshot.
struct PosBuffer
{
    static constexpr int CAP = 6;
    static constexpr float TELEPORT_TILES = 6.f; // a bigger step between samples is a jump, never smoothed
    static constexpr float EXTRAP_TICKS = 5.f;   // 250 ms of dead reckoning at most when snapshots stop
    static constexpr float EXTRAP_MAX_TILES = 1.f;

    struct Sample
    {
        uint32_t tick;
        float x, y;
    };
    Sample s[CAP]{};
    int n = 0;

    // Older or duplicate ticks are ignored (chunks of one tick may repeat an entity).
    void push(uint32_t tick, float x, float y)
    {
        if (n > 0 && tick <= s[n - 1].tick)
            return;
        if (n == CAP)
        {
            std::copy(s + 1, s + CAP, s);
            --n;
        }
        s[n++] = {tick, x, y};
    }

    // Position at render time `rt` (in ticks): interpolates between the bracketing samples, holds before the
    // oldest one, extrapolates with the last velocity (clamped) after the newest. Never freezes while data flows.
    Pos at(double rt) const
    {
        if (n == 0)
            return {0, 0};
        if (n == 1 || rt <= s[0].tick)
            return {s[0].x, s[0].y};
        const Sample &last = s[n - 1];
        if (rt >= last.tick)
        {
            const Sample &prev = s[n - 2];
            float span = float(last.tick - prev.tick);
            float vx = (last.x - prev.x) / span, vy = (last.y - prev.y) / span;
            if (std::hypot(last.x - prev.x, last.y - prev.y) > TELEPORT_TILES)
                return {last.x, last.y};
            float t = std::min(float(rt - last.tick), EXTRAP_TICKS);
            float dx = vx * t, dy = vy * t, len = std::hypot(dx, dy);
            if (len > EXTRAP_MAX_TILES)
                dx *= EXTRAP_MAX_TILES / len, dy *= EXTRAP_MAX_TILES / len;
            return {last.x + dx, last.y + dy};
        }
        int i = n - 2;
        while (i > 0 && s[i].tick > rt)
            --i;
        const Sample &a = s[i], &b = s[i + 1];
        if (std::hypot(b.x - a.x, b.y - a.y) > TELEPORT_TILES)
            return rt >= b.tick ? Pos{b.x, b.y} : Pos{a.x, a.y};
        float k = float((rt - a.tick) / double(b.tick - a.tick));
        return {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k};
    }
};

// Maps local time to server ticks. The offset estimate follows late packets slowly and early ones fast (the
// fastest packet shows the true latency); the render clock then slews towards it by at most 10% speed, so a
// jitter spike never makes anything jump or stall.
struct InterpClock
{
    static constexpr double SLEW = .1;
    static constexpr double RESET_TICKS = 10; // further off than this: jump (reconnect, long stall)

    double offset = 0; // serverTick - localSeconds * TICK_HZ
    double rt = 0;     // render tick
    double lastNow = 0;
    bool haveOffset = false, haveRt = false;
    int interval = 2; // snapshot spacing in ticks (smallest seen in the last window)
    uint32_t lastTick = 0;
    int window = 0, windowMin = 99;

    void reset() { *this = InterpClock{}; }

    void onSnapshot(uint32_t tick, double now)
    {
        double sample = double(tick) - now * TICK_HZ;
        if (!haveOffset || std::abs(sample - offset) > 40)
            offset = sample, haveOffset = true;
        else
            offset += (sample - offset) * (sample > offset ? .5 : .02);

        if (lastTick != 0 && tick > lastTick)
            windowMin = std::min(windowMin, int(tick - lastTick));
        lastTick = tick;
        if (++window >= 20)
        {
            interval = std::clamp(windowMin, 1, 10);
            window = 0, windowMin = 99;
        }
    }

    // Render delay in ticks: about two snapshot intervals (one lost snapshot still leaves a bracket), >= 150 ms.
    double delay(double overrideMs = 0) const
    {
        if (overrideMs > 0)
            return overrideMs * TICK_HZ / 1000.0;
        return std::max(3.0, 2.0 * interval);
    }

    // Advance the render clock to local time `now`; returns the render tick.
    double advance(double now, double delayTicks)
    {
        if (!haveOffset)
            return rt;
        double target = now * TICK_HZ + offset - delayTicks;
        if (!haveRt || std::abs(target - rt) > RESET_TICKS)
        {
            rt = target, haveRt = true;
        }
        else
        {
            double step = (now - lastNow) * TICK_HZ;
            double err = std::clamp(target - rt - step, -SLEW * step, SLEW * step);
            rt += step + err;
        }
        lastNow = now;
        return rt;
    }
};

// `BW_STUTTER=1` metric: per rendered frame, the change of an entity's velocity (tiles/s, jitter) and big steps
// (snaps).
struct StutterStats
{
    std::vector<float> jitter;
    int snaps = 0;
    float maxJitter = 0;

    void add(float jitterTilesPerSec, float stepTiles)
    {
        jitter.push_back(jitterTilesPerSec);
        maxJitter = std::max(maxJitter, jitterTilesPerSec);
        snaps += stepTiles > 1.f;
    }
    float p99()
    {
        if (jitter.empty())
            return 0;
        std::sort(jitter.begin(), jitter.end());
        return jitter[std::min(jitter.size() - 1, size_t(double(jitter.size()) * .99))];
    }
    void reset() { jitter.clear(), snaps = 0, maxJitter = 0; }
};
