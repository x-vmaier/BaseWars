// Pause menu (ESC), UI scale and window modes. Included inside `struct App` (client.cpp).
//
// UI scale: inside App, GetScreenWidth()/GetScreenHeight()/GetMousePosition() are SHADOWED by versions that return the
// logical (scaled) size / mouse position, and the whole UI layer is drawn under rlScalef(uiScale). World code uses the
// real values through ::GetScreenWidth() etc. At scale 1.0 both are identical.

float uiScale() const { return std::clamp(settings.uiScale, 0.6f, 2.0f); }
int GetScreenWidth() const { return int(float(::GetScreenWidth()) / uiScale()); }
int GetScreenHeight() const { return int(float(::GetScreenHeight()) / uiScale()); }
Vector2 GetMousePosition() const { Vector2 m = ::GetMousePosition(); return {m.x / uiScale(), m.y / uiScale()}; }
int SW() const { return GetScreenWidth(); }
int SH() const { return GetScreenHeight(); }
Vector2 MP() const { return GetMousePosition(); }
void beginUi() { rlPushMatrix(); rlScalef(uiScale(), uiScale(), 1.f); }
void endUi() { rlPopMatrix(); }

bool pauseOpen = false, pauseSliderDrag = false;
double pauseSavedAt = -100;
TMap mPause;

void applyWindowMode(int modeIdx) {
    bool full = IsWindowFullscreen(), border = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    if (modeIdx == 1) { if (border) ToggleBorderlessWindowed(); if (!IsWindowFullscreen()) ToggleFullscreen(); }
    else if (modeIdx == 2) { if (full) ToggleFullscreen(); if (!IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) ToggleBorderlessWindowed(); }
    else { if (full) ToggleFullscreen(); if (border) ToggleBorderlessWindowed(); }
}
void setWindowMode(int modeIdx) {
    settings.windowMode = (modeIdx % Settings::WINDOW_MODES + Settings::WINDOW_MODES) % Settings::WINDOW_MODES;
    applyWindowMode(settings.windowMode);
}

Rectangle pauseRect() { Vector2 s = mPause.size(); return {(float(SW()) - s.x) / 2, (float(SH()) - s.y) / 2 + 14, s.x, s.y}; }
Rectangle pauseRow(int i) { Rectangle r = pauseRect(); float pad = float(mPause.padding); return {r.x + pad + 10, r.y + pad + 34 + float(i) * 46.f, r.width - 2 * pad - 20, 34}; }
Rectangle pauseSlider() { Rectangle r = pauseRow(0); return {r.x + 96, r.y + 20, r.width - 96 - 56, 14}; }
Rectangle pauseArrow(int dir) { Rectangle r = pauseRow(1); return dir < 0 ? Rectangle{r.x + 96, r.y + 2, 30, 30} : Rectangle{r.x + r.width - 30, r.y + 2, 30, 30}; }

void pauseInput() {
    Vector2 m = GetMousePosition();
    bool lp = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (IsKeyPressed(KEY_ESCAPE)) { pauseOpen = false; pauseSliderDrag = false; return; }
    Rectangle sl = pauseSlider();
    if (lp && CheckCollisionPointRec(m, {sl.x - 12, sl.y - 12, sl.width + 24, sl.height + 24})) pauseSliderDrag = true;
    if (pauseSliderDrag) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {                      // note: the mouse position is already in scaled space
            float k = std::clamp((::GetMousePosition().x / uiScale() - sl.x) / sl.width, 0.f, 1.f);
            settings.uiScale = std::clamp(std::round((0.6f + 1.4f * k) * 20.f) / 20.f, 0.6f, 2.0f);
        } else pauseSliderDrag = false;
        return;
    }
    if (!lp) return;
    if (CheckCollisionPointRec(m, pauseArrow(-1))) { setWindowMode(settings.windowMode - 1); return; }
    if (CheckCollisionPointRec(m, pauseArrow(1))) { setWindowMode(settings.windowMode + 1); return; }
    if (CheckCollisionPointRec(m, pauseRow(2))) { if (settings.save(std::string(GetApplicationDirectory()))) pauseSavedAt = GetTime(); else msg("Could not write settings.txt"); return; }
    if (CheckCollisionPointRec(m, pauseRow(3))) { pauseOpen = false; return; }
    if (CheckCollisionPointRec(m, pauseRow(4))) { pauseOpen = false; backToMenu("You left the game."); return; }
    if (CheckCollisionPointRec(m, pauseRow(5))) { wantQuit = true; return; }
    if (!CheckCollisionPointRec(m, pauseRect())) pauseOpen = false;
}

void drawPause() {
    Vector2 m = GetMousePosition();
    DrawRectangle(0, 0, SW(), SH(), rgb(0, 0, 0, 150));                  // darken the game
    Rectangle r = pauseRect();
    drawTMap(mPause, r);
    Rectangle tr{r.x + r.width / 2 - mTitle.size().x / 2, r.y - mTitle.size().y * .55f, mTitle.size().x, mTitle.size().y};
    drawTMap(mTitle, tr);
    textShadow("Paused", int(tr.x + tr.width / 2) - MeasureText("Paused", 22) / 2, int(tr.y + tr.height / 2) - 13, 22, WHITE);
    Color ink = rgb(70, 45, 30);
    Rectangle row0 = pauseRow(0), sl = pauseSlider();
    DrawText("UI scale", int(row0.x), int(row0.y) + 14, 15, ink);
    float k = (settings.uiScale - 0.6f) / 1.4f;
    DrawRectangleRounded(sl, .5f, 6, rgb(60, 40, 28, 220));
    DrawRectangleRounded({sl.x, sl.y, std::max(8.f, sl.width * k), sl.height}, .5f, 6, rgb(80, 170, 220));
    DrawCircleV({sl.x + sl.width * k, sl.y + sl.height / 2}, 11, rgb(250, 235, 200));
    DrawCircleLines(int(sl.x + sl.width * k), int(sl.y + sl.height / 2), 11, rgb(60, 40, 28));
    DrawText(TextFormat("%d%%", int(std::lround(settings.uiScale * 100.f))), int(row0.x + row0.width) - 46, int(row0.y) + 14, 15, ink);
    Rectangle row1 = pauseRow(1);
    DrawText("Window", int(row1.x), int(row1.y) + 9, 15, ink);
    for (int d = -1; d <= 1; d += 2) {                                       // left / right arrows
        Rectangle a = pauseArrow(d);
        drawButton(a, "", true, false, CheckCollisionPointRec(m, a), false, 14);
        float cx = a.x + a.width / 2, cy = a.y + a.height / 2;
        if (d < 0) DrawTriangle({cx + 5, cy - 8}, {cx - 7, cy}, {cx + 5, cy + 8}, rgb(255, 245, 210));
        else DrawTriangle({cx - 5, cy - 8}, {cx - 5, cy + 8}, {cx + 7, cy}, rgb(255, 245, 210));
    }
    const char* mn = Settings::modeName(settings.windowMode);
    DrawText(mn, int(row1.x + 96 + (row1.width - 96 - 0) / 2) - 15 - MeasureText(mn, 14) / 2 + 0, int(row1.y) + 10, 14, ink);
    bool saved = GetTime() - pauseSavedAt < 2.0;
    drawButton(pauseRow(2), saved ? "Saved!" : "Save settings", true, false, CheckCollisionPointRec(m, pauseRow(2)), false, 15);
    drawButton(pauseRow(3), "Resume", true, false, CheckCollisionPointRec(m, pauseRow(3)), false, 15);
    drawButton(pauseRow(4), "Leave to main menu", true, false, CheckCollisionPointRec(m, pauseRow(4)), false, 15);
    drawButton(pauseRow(5), "Quit game", true, true, CheckCollisionPointRec(m, pauseRow(5)), false, 15);
}
