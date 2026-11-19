#include "ui/Widgets.hpp"

#include "common/Shared.hpp"
#include "rendering/Text.hpp"

namespace ui {
namespace {
TMap mBtn, mBtnP, mBtnR, mBtnRP, mTitle;
}

void loadWidgets() {
    mTitle = loadTMap("title_ribbon");
    mBtn = loadTMap("button_blue"); mBtnP = loadTMap("button_blue_pressed"); mBtnR = loadTMap("button_red"); mBtnRP = loadTMap("button_red_pressed");
}
const TMap& titleMap() { return mTitle; }

void drawButton(Rectangle r, const std::string& label, bool enabled, bool red, bool hover, bool pressed, int fs) {
    const TMap& m = red ? (pressed ? mBtnRP : mBtnR) : (pressed ? mBtnP : mBtn);
    drawTMap(m, r, enabled ? (hover ? rgb(255, 255, 255) : rgb(238, 238, 238)) : rgb(140, 140, 140, 220));
    int tw = MeasureText(label.c_str(), fs);
    while (tw > int(r.width) - 12 && fs > 9) { fs--; tw = MeasureText(label.c_str(), fs); }      // shrink long labels to fit
    DrawText(label.c_str(), int(r.x + r.width / 2) - tw / 2 + 1, int(r.y + r.height / 2) - fs / 2 + 1, fs, rgb(20, 20, 40, 180));
    DrawText(label.c_str(), int(r.x + r.width / 2) - tw / 2, int(r.y + r.height / 2) - fs / 2, fs, enabled ? WHITE : rgb(200, 200, 200));
}
void drawResourceIcon(int r, float x, float y, float s) {
    if (r == R_Stone) { Texture2D t = tex("Terrain/Decorations/Rocks/Rock2.png"); DrawTexturePro(t, {0, 0, 64, 64}, {x - 4, y - 10, 64 * s, 64 * s}, {0, 0}, 0, WHITE); return; }
    static const char* icon[R_Count] = {"Icon_02", "Icon_03", "", "Icon_04"};
    Texture2D t = tex(std::string("UI Elements/UI Elements/Icons/") + icon[r] + ".png");
    DrawTexturePro(t, {0, 0, 64, 64}, {x, y, 64 * s, 64 * s}, {0, 0}, 0, WHITE);
}
void statBar(float x, float y, float w, float frac, Color c, const std::string& label, const std::string& value, const std::string& bonus) {
    DrawText(label.c_str(), int(x), int(y), 12, rgb(70, 45, 30));
    DrawRectangleRounded({x + 56, y + 2, w, 10}, .5f, 4, rgb(60, 40, 28, 200));
    DrawRectangleRounded({x + 56, y + 2, std::max(4.f, w * std::clamp(frac, 0.f, 1.f)), 10}, .5f, 4, c);
    DrawText(value.c_str(), int(x + 62 + w), int(y), 12, rgb(70, 45, 30));
    if (!bonus.empty()) DrawText(bonus.c_str(), int(x + 62 + w + MeasureText(value.c_str(), 12) + 4), int(y), 11, rgb(30, 130, 50));
}
bool textInput(std::string& target, size_t maxLen) {
    bool changed = false;
    for (int ch = GetCharPressed(); ch > 0; ch = GetCharPressed()) if (ch >= 32 && ch < 127 && target.size() < maxLen) { target.push_back(char(ch)); changed = true; }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !target.empty()) { target.pop_back(); changed = true; }
    return changed;
}
void textBox(Rectangle r, const std::string& text, const std::string& hint, bool focus) {
    DrawRectangleRounded(r, .3f, 6, rgb(255, 250, 235, 235));
    DrawRectangleRoundedLinesEx(r, .3f, 6, 2, focus ? rgb(60, 120, 200) : rgb(120, 90, 60));
    std::string shown = text.empty() && !focus ? hint : text + (focus && int(GetTime() * 2) % 2 == 0 ? "|" : "");
    DrawText(shown.c_str(), int(r.x) + 8, int(r.y) + 5, 15, text.empty() && !focus ? rgb(150, 130, 110) : rgb(70, 45, 30));
}
void panelTitle(Rectangle panel, const std::string& title, int fs, float rise, int textDy) {
    Vector2 ts = mTitle.size();
    Rectangle tr{panel.x + panel.width / 2 - ts.x / 2, panel.y - ts.y * rise, ts.x, ts.y};
    drawTMap(mTitle, tr);
    textShadow(title, int(tr.x + tr.width / 2) - MeasureText(title.c_str(), fs) / 2, int(tr.y + tr.height / 2) - textDy, fs, WHITE);
}

}  // namespace ui
