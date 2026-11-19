#pragma once
// Cost row widget: every resource requirement drawn as [resource icon][numeric value], with a fixed icon size, padding
// and margin. Several costs flow left to right and wrap onto further lines inside a maximum width. Values the player
// cannot pay are drawn in the "missing" colour. Buttons carry only a label; the cost row sits beside or below them.
#include "common/Shared.hpp"

#include <raylib.h>

namespace ui {

struct CostStyle {
    float icon = 18;                    // icon edge (square)
    float gap = 3;                      // icon -> value
    float margin = 10;                  // between two cost items
    float lineGap = 3;                  // between wrapped lines
    int fs = 14;                        // value font size
    Color ok = {70, 45, 30, 255};       // affordable value (default: ink on paper)
    Color bad = {200, 40, 30, 255};     // missing value
    bool shadow = false;                // drop shadow behind values (dark / world backgrounds)
};
CostStyle paperCost(float s = 1.f);     // ink on the parchment panels, scaled by s
CostStyle darkCost(float s = 1.f);      // light text on dark wood / tooltips / the world, scaled by s

// Draws the resource icon r centred in the square `dst` (wood, gold, stone, meat).
void resourceIcon(int r, Rectangle dst, Color tint = WHITE);
// Size of the laid-out row (0 x line height when there is nothing to pay: "Free").
Vector2 measureCosts(const float* cost, float maxW, const CostStyle& st);
// Draws the costs at (x, y) wrapping inside maxW. `have` (may be null) colours missing amounts; returns the used size.
Vector2 drawCosts(const float* cost, float x, float y, float maxW, const float* have, const CostStyle& st);
// Convenience: a single resource amount.
Vector2 drawCost(int r, float amount, float x, float y, const float* have, const CostStyle& st);

}  // namespace ui
