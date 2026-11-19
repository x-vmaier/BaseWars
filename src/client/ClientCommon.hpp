/**
 * @file ClientCommon.hpp
 * @brief Client-wide constants.
 */
#pragma once
#include "common/Progression.hpp"
#include "common/Shared.hpp"

#ifdef near
#undef near
#endif

inline constexpr float TILE = 64.f;     // world pixels per tile
inline constexpr int MAX_QUOTA_UI = 20; // upper end of the defense-quota slider and of the support-patrol size
