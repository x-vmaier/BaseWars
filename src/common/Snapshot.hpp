/**
 * @file Snapshot.hpp
 * @brief Wire envelope shared by client and server: command sequence numbers and chunked, tick-numbered S_State.
 */
#pragma once
#include "common/Shared.hpp"

#include <bitset>

// Bumped on every incompatible protocol change; checked in C_Hello and S_Welcome.
inline constexpr uint16_t PROTOCOL_VERSION = 2;

// An unreliable message that fragments is lost if any fragment is lost, so every S_State chunk stays below this.
inline constexpr size_t SNAPSHOT_MTU = 1100;
inline constexpr int SNAPSHOT_MAX_CHUNKS = 255;

// S_State chunk header (after the type byte): the server tick, the last client command seq it has processed,
// chunk index and chunk count of this tick's snapshot.
struct SnapHeader
{
    uint32_t tick = 0, ackSeq = 0;
    uint8_t index = 0, count = 0;
};
inline constexpr size_t SNAP_HEADER_BYTES = 1 + 4 + 4 + 1 + 1;

inline void putSnapHeader(Writer &w, const SnapHeader &h)
{
    w.put<uint8_t>(S_State).put<uint32_t>(h.tick).put<uint32_t>(h.ackSeq).put<uint8_t>(h.index).put<uint8_t>(h.count);
}

inline SnapHeader getSnapHeader(Reader &r)
{
    SnapHeader h;
    h.tick = r.get<uint32_t>();
    h.ackSeq = r.get<uint32_t>();
    h.index = r.get<uint8_t>();
    h.count = r.get<uint8_t>();
    if (h.count == 0 || h.index >= h.count)
        r.ok = false;
    return h;
}

// Every client command except C_Hello carries a u32 sequence number right after the type byte.
inline std::vector<uint8_t> withSeq(const std::vector<uint8_t> &msg, uint32_t seq)
{
    std::vector<uint8_t> out(msg);
    uint8_t b[4];
    std::memcpy(b, &seq, 4);
    out.insert(out.begin() + std::min<size_t>(1, out.size()), b, b + 4);
    return out;
}

// Client-side bookkeeping of which chunks of the newest tick arrived. A newer tick abandons an older partial one,
// older or duplicate chunks are rejected. Chunks are independent: each can be applied the moment it arrives.
class SnapshotTracker
{
  public:
    enum class Result
    {
        Reject,
        Partial, // applied, the tick is still missing chunks
        Complete // applied, the tick is whole: entities not seen in it are gone
    };
    Result add(const SnapHeader &h, bool &startsTick)
    {
        startsTick = false;
        if (h.count == 0 || h.index >= h.count || h.tick < tick)
            return Result::Reject;
        if (h.tick > tick || tick == 0)
        {
            tick = h.tick;
            count = h.count;
            got.reset();
            startsTick = true;
        }
        else if (h.count != count || got[h.index])
            return Result::Reject;
        got.set(h.index);
        return got.count() == count ? Result::Complete : Result::Partial;
    }
    void reset() { *this = SnapshotTracker{}; }
    uint32_t tick = 0;

  private:
    uint8_t count = 0;
    std::bitset<256> got;
};
