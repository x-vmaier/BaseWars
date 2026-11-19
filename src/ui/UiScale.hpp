/**
 * @file UiScale.hpp
 * @brief UI scale.
 */
#pragma once
#include <raylib.h>

namespace ui
{

void bindScale(const float *setting); // the settings value (Settings::uiScale) the UI scale follows
float scale();                        // clamped to 0.6 .. 2.0
int screenW();                        // logical screen width  (real width / scale)
int screenH();                        // logical screen height (real height / scale)
Vector2 mouse();                      // logical mouse position (real position / scale)
void begin();                         // push the UI scale matrix
void end();                           // pop it

} // namespace ui
