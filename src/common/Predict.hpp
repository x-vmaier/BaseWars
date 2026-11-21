/**
 * @file Predict.hpp
 * @brief Client-side command prediction: queue of unacknowledged move orders and the local movement-start offset.
 * Header-only and free of raylib so the `net-predict` selftest can drive it headless.
 */
#pragma once
#include "common/Shared.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

// A move / attack-move order sent but not yet covered by a snapshot's ack seq.
struct PendingCmd
{
    uint32_t seq = 0;
    double sentAt = 0;
    float tx = 0, ty = 0; // target, tiles
    std::vector<uint32_t> units;
};

struct PendingQueue
{
    static constexpr size_t CAP = 64;   // a flooding client never grows the queue
    static constexpr double LIFE = 3.0; // s; a command nobody acked (lost link) is forgotten, never leaks

    std::deque<PendingCmd> q;

    void push(PendingCmd c)
    {
        if (q.size() == CAP)
            q.pop_front();
        q.push_back(std::move(c));
    }
    // Snapshot echoed `ackSeq`: everything up to it was processed by the server.
    void ack(uint32_t ackSeq)
    {
        while (!q.empty() && q.front().seq <= ackSeq)
            q.pop_front();
    }
    // The server refused command `seq`; false when it was not pending (already acked or expired).
    bool reject(uint32_t seq, PendingCmd *out = nullptr)
    {
        for (auto it = q.begin(); it != q.end(); ++it)
            if (it->seq == seq)
            {
                if (out)
                    *out = *it;
                q.erase(it);
                return true;
            }
        return false;
    }
    void expire(double now)
    {
        std::erase_if(q, [&](const PendingCmd &c) { return now - c.sentAt > LIFE; });
    }
    // Target of the newest pending order that includes unit `uid`; false if none.
    bool targetOf(uint32_t uid, float &tx, float &ty) const
    {
        for (auto it = q.rbegin(); it != q.rend(); ++it)
            if (std::find(it->units.begin(), it->units.end(), uid) != it->units.end())
            {
                tx = it->tx, ty = it->ty;
                return true;
            }
        return false;
    }
};

// Rendered offset on top of the interpolated server position: grows towards the order target while the order is
// pending (units start moving at once), then fades out smoothly as the interpolated position takes over, so an
// ack or a rejection never snaps anything.
struct PredOffset
{
    static constexpr float SPEED = 2.f;     // tiles/s of the local movement start
    static constexpr float MAX_TILES = .6f; // never drifts further from the authoritative position
    static constexpr float FADE = 6.f;      // 1/s decay once nothing is pending

    float x = 0, y = 0;

    // `px,py`: interpolated position; `pending` + target: the unit has an unacknowledged order.
    void step(float dt, bool pending, float px, float py, float tx, float ty)
    {
        if (pending)
        {
            float dx = tx - (px + x), dy = ty - (py + y), d = std::hypot(dx, dy);
            if (d > .15f)
            {
                x += dx / d * SPEED * dt;
                y += dy / d * SPEED * dt;
                float len = std::hypot(x, y);
                if (len > MAX_TILES)
                    x *= MAX_TILES / len, y *= MAX_TILES / len;
                return;
            }
        }
        float k = std::exp(-FADE * dt);
        x *= k, y *= k;
        if (std::abs(x) + std::abs(y) < 1e-3f)
            x = y = 0;
    }
};
