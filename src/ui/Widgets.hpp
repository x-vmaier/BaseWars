#pragma once
// Shared UI widgets: Tiled-skinned buttons, title ribbon, text boxes, resource icons, stat bars.
#include "rendering/Assets.hpp"

#include <string>

namespace ui
{

    void loadWidgets();     // button skins + the title ribbon map
    const TMap &titleMap(); // "title_ribbon": panel titles and the HUD name banner

    void drawButton(Rectangle r, const std::string &label, bool enabled, bool red, bool hover, bool pressed, int fs = 14);
    void drawResourceIcon(int r, float x, float y, float s);
    void statBar(float x, float y, float w, float frac, Color c, const std::string &label, const std::string &value, const std::string &bonus);
    // Single-line text field: appends typed printable characters / handles backspace; true when the text changed.
    bool textInput(std::string &target, size_t maxLen);
    void textBox(Rectangle r, const std::string &text, const std::string &hint, bool focus);
    // Left / right arrow button in the pause-menu selector style (dir -1 / +1).
    void arrowButton(Rectangle r, int dir, bool enabled, bool hover);
    // Tab strip: n buttons in a row, the active one drawn pressed; `enabled` (may be null) greys tabs out.
    void tabStrip(const Rectangle *tabs, const char *const *labels, int n, int active, const bool *enabled, Vector2 mouse, int fs = 15);
    // Horizontal slider track with a knob at frac (0..1), in the pause-menu colours.
    void sliderTrack(Rectangle r, float frac, bool hover);
    // Draws a title ribbon centred above a panel with `title` in it (height factor `rise` of the ribbon above r.y).
    void panelTitle(Rectangle panel, const std::string &title, int fs, float rise, int textDy);

} // namespace ui
