/**
 * @file Wind.hpp
 * @brief The world wind (one singleton in registry.ctx()) and the wind-rose speed modifier of ships.
 */
#pragma once
#include <cmath>
#include <cstdint>

inline constexpr float WIND_TAU = 6.2831853f;

// `dir` is the direction the wind blows towards (radians, atan2(dy, dx) like every heading). It slews towards
// `target`, which is re-rolled every `shiftIn` seconds from the private generator `rng` (so the world rng stays
// untouched).
struct Wind
{
    float dir = 0, target = 0, shiftIn = 0;
    uint32_t rng = 1;
};

// Speed factor of a ship sailing along `heading`: a tailwind adds `strength`, a headwind removes it, a crosswind
// changes nothing.
inline float windSpeedMul(float heading, float windDir, float strength)
{
    return 1.f + strength * std::cos(heading - windDir);
}

// Shortest signed angle from a to b.
inline float windAngleDelta(float a, float b)
{
    float d = std::fmod(b - a, WIND_TAU);
    if (d > WIND_TAU / 2)
        d -= WIND_TAU;
    else if (d < -WIND_TAU / 2)
        d += WIND_TAU;
    return d;
}

inline uint8_t windToByte(float dir)
{
    float t = std::fmod(dir, WIND_TAU);
    if (t < 0)
        t += WIND_TAU;
    return uint8_t(int(t / WIND_TAU * 256.f) & 255);
}

inline float windFromByte(uint8_t b)
{
    return float(b) / 256.f * WIND_TAU;
}
