/**
 * @file Pacing.hpp
 * @brief Pacing Director rules shared by server, client and selftests: phases and raid sizing.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Calm -> Warning -> Siege -> Aftermath -> Calm, per player.
enum PacingPhase : uint8_t
{
    PH_Calm,
    PH_Warning,
    PH_Siege,
    PH_Aftermath,
    PH_Count
};

inline const char *pacingName(int phase)
{
    static const char *n[PH_Count] = {"Calm", "Warning", "Siege", "Aftermath"};
    return phase >= 0 && phase < PH_Count ? n[phase] : "?";
}

// Defense value of a base: Town Hall level, finished buildings and army units (garrison-less troops).
inline float defenseScore(int hallLevel, int buildings, int troops)
{
    return 2.f * float(hallLevel) + .5f * float(buildings) + float(troops);
}

// Raiders for a base: grows with the defense score, scaled by the adaptive factor, never above the hard cap or the
// configured share of the base's assets (buildings + troops), so one raid cannot wipe out a small base.
inline int raidSize(float score, float factor, int assets, int maxRaiders, float maxShare)
{
    int n = int(std::lround((2.f + .35f * score) * factor));
    n = std::min(n, maxRaiders);
    n = std::min(n, std::max(2, int(maxShare * float(assets))));
    return std::max(2, n);
}
