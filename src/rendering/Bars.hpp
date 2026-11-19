#pragma once
// Health / progress bars and selection brackets. Every bar is high contrast and readable at any zoom: a dark opaque
// backdrop, a bright saturated fill with a light top highlight, and a 1px light outline.
#include <raylib.h>

// bar colours: health runs green -> yellow -> red; progress kinds each have their own distinct colour
Color healthColor(float frac);
inline constexpr Color BAR_BUILD = {70, 150, 255, 255};   // construction
inline constexpr Color BAR_UPGRADE = {255, 160, 30, 255}; // building upgrade
inline constexpr Color BAR_REPAIR = {40, 220, 210, 255};  // repair
inline constexpr Color BAR_CLEAR = {255, 210, 40, 255};   // obstacle clearing
inline constexpr Color BAR_TRAIN = {190, 110, 255, 255};  // training queue
inline constexpr Color BAR_CARGO = {255, 200, 70, 255};   // ship cargo
inline constexpr Color BAR_TROOPS = {90, 190, 255, 255};  // ship troops aboard
inline constexpr Color BAR_IDLE = {150, 150, 165, 255};   // waiting (no pawn at work yet)

void barRect(Rectangle r, float frac, Color fill);        // the bar itself, filling r (outline drawn just outside)
void drawBar(Vector2 c, float w, float frac, Color fill); // world / UI small bar centred at c.x, top at c.y
void brackets(Rectangle r, Color c);                      // corner brackets around a selected building
void clearBar(float cx, float by, float pct, bool working = true); // clearing progress above a tree / rock
