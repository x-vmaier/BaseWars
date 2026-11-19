/**
 * @file FxDrawers.hpp
 * @brief Table of one-shot effect drawers (explosion, dust, arrows...) keyed by EffectType.
 */
#pragma once
#include "client/ClientCommon.hpp"
#include "client/ClientState.hpp"

// An effect type is one entry in fxDrawer(): where it sorts, how long it is visible and how it is drawn.
struct FxDrawer
{
    float sortDy;                            // added to the sort y (in pixels)
    bool sortAtTarget;                       // sort by the target (y2) instead of the origin
    bool (*visible)(const Fx &, double age); // null = always (the caller drops old effects)
    void (*draw)(const Fx &, double age, double t);
};

// Null for event types that are drawn elsewhere (buildings burning) or not at all.
const FxDrawer *fxDrawer(uint8_t type);
