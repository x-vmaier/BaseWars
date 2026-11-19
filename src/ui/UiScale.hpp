#pragma once
// UI scale. The whole UI layer (HUD, panels, menus) is drawn under rlScalef(scale) and works in "logical" pixels:
// screen size and mouse position divided by the scale. World code (camera, picking, dragging, render targets) uses the
// real values from raylib (::GetScreenWidth() / ::GetMousePosition()). At scale 1.0 both are identical.
//
// (The old monolithic client shadowed GetScreenWidth/GetScreenHeight/GetMousePosition inside `struct App`; these
// helpers make that explicit.)
#include <raylib.h>

namespace ui {

void bindScale(const float* setting);   // the settings value (Settings::uiScale) the UI scale follows
float scale();                          // clamped to 0.6 .. 2.0
int screenW();                          // logical screen width  (real width / scale)
int screenH();                          // logical screen height (real height / scale)
Vector2 mouse();                        // logical mouse position (real position / scale)
void begin();                           // push the UI scale matrix
void end();                             // pop it

}  // namespace ui
