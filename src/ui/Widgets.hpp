#pragma once
// Shared UI widgets: Tiled-skinned buttons, title ribbon, text boxes, resource icons, stat bars.
#include "rendering/Assets.hpp"

#include <string>

namespace ui {

void loadWidgets();                     // button skins + the title ribbon map
const TMap& titleMap();                 // "title_ribbon": panel titles and the HUD name banner

void drawButton(Rectangle r, const std::string& label, bool enabled, bool red, bool hover, bool pressed, int fs = 14);
void drawResourceIcon(int r, float x, float y, float s);
void statBar(float x, float y, float w, float frac, Color c, const std::string& label, const std::string& value, const std::string& bonus);
// Single-line text field: appends typed printable characters / handles backspace; true when the text changed.
bool textInput(std::string& target, size_t maxLen);
void textBox(Rectangle r, const std::string& text, const std::string& hint, bool focus);
// Draws a title ribbon centred above a panel with `title` in it (height factor `rise` of the ribbon above r.y).
void panelTitle(Rectangle panel, const std::string& title, int fs, float rise, int textDy);

}  // namespace ui
