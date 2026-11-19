#pragma once
// Health / progress bars and selection brackets.
#include <raylib.h>

void drawBar(Vector2 c, float w, float frac, Color fill);   // UI-kit small bar centred at c.x, top at c.y
void brackets(Rectangle r, Color c);                         // corner brackets around a selected building
void clearBar(float cx, float by, float pct);                // clearing progress above a tree / rock
