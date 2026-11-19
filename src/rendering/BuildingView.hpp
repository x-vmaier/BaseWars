/**
 * @file BuildingView.hpp
 * @brief Per-frame snapshot of one building for the building and effect renderers.
 */
#pragma once
#include "client/ClientState.hpp"
#include "client/Components.hpp"

// Everything one building needs while it is drawn; computed once per frame and copied into the draw items.
struct BuildingView
{
    Info i;
    uint32_t id;
    int tx, ty;
    float cx, bottom, prog, hpFrac;
    bool selected, hover, remembered, moving, working, crippled, hasJob;
    Color oc, tint;
    int col;
    BJob job;
    double t;

    float sortY() const { return bottom; }
};
