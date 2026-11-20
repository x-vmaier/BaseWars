/**
 * @file PauseMenu.cpp
 * @brief Pause menu (Esc): UI scale slider, window mode, save settings, resume, leave, quit.
 */
#include "ui/PauseMenu.hpp"

#include "client/App.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

#include <cmath>

void PauseMenu::load()
{
    mPause = loadTMap("pause_menu");
}

void PauseMenu::applyWindowMode(int modeIdx)
{
    bool full = IsWindowFullscreen(), border = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    if (modeIdx == 1)
    {
        if (border)
            ToggleBorderlessWindowed();
        if (!IsWindowFullscreen())
            ToggleFullscreen();
    }
    else if (modeIdx == 2)
    {
        if (full)
            ToggleFullscreen();
        if (!IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE))
            ToggleBorderlessWindowed();
    }
    else
    {
        if (full)
            ToggleFullscreen();
        if (border)
            ToggleBorderlessWindowed();
    }
}
void PauseMenu::setWindowMode(int modeIdx)
{
    Settings &s = app.settings;
    s.windowMode = (modeIdx % Settings::WINDOW_MODES + Settings::WINDOW_MODES) % Settings::WINDOW_MODES;
    applyWindowMode(s.windowMode);
}

Rectangle PauseMenu::rect() const
{
    Vector2 s = mPause.size();
    return {(float(ui::screenW()) - s.x) / 2, (float(ui::screenH()) - s.y) / 2 + 14, s.x, s.y};
}
Rectangle PauseMenu::row(int i) const
{
    Rectangle r = rect();
    float pad = float(mPause.padding);
    return {r.x + pad + 10, r.y + pad + 34 + float(i) * 46.f, r.width - 2 * pad - 20, 34};
}
Rectangle PauseMenu::slider() const
{
    Rectangle r = row(0);
    return {r.x + 96, r.y + 20, r.width - 96 - 56, 14};
}
Rectangle PauseMenu::arrow(int dir) const
{
    Rectangle r = row(1);
    return dir < 0 ? Rectangle{r.x + 96, r.y + 2, 30, 30} : Rectangle{r.x + r.width - 30, r.y + 2, 30, 30};
}

void PauseMenu::input()
{
    Vector2 m = ui::mouse();
    bool lp = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (IsKeyPressed(KEY_ESCAPE))
    {
        isOpen = false;
        sliderDrag = false;
        return;
    }

    Rectangle sl = slider();
    if (lp && CheckCollisionPointRec(m, {sl.x - 12, sl.y - 12, sl.width + 24, sl.height + 24}))
        sliderDrag = true;
    if (sliderDrag)
    {
        dragSlider(sl);
        return;
    }
    if (lp)
        onClick(m);
}

void PauseMenu::dragSlider(const Rectangle &sl)
{
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        sliderDrag = false;
        return;
    }
    // The slider rectangle is in scaled UI space, the raw mouse position is not.
    float k = std::clamp((::GetMousePosition().x / ui::scale() - sl.x) / sl.width, 0.f, 1.f);
    app.settings.uiScale = std::clamp(std::round((0.6f + 1.4f * k) * 20.f) / 20.f, 0.6f, 2.0f);
}

void PauseMenu::onClick(Vector2 m)
{
    Settings &s = app.settings;
    if (CheckCollisionPointRec(m, arrow(-1)))
    {
        setWindowMode(s.windowMode - 1);
        return;
    }
    if (CheckCollisionPointRec(m, arrow(1)))
    {
        setWindowMode(s.windowMode + 1);
        return;
    }
    if (CheckCollisionPointRec(m, row(2)))
    {
        if (s.save(std::string(GetApplicationDirectory())))
            savedAt = GetTime();
        else
            app.msg("Could not write settings.txt");
        return;
    }
    if (CheckCollisionPointRec(m, row(3)))
    {
        isOpen = false;
        return;
    }
    if (CheckCollisionPointRec(m, row(4)))
    {
        isOpen = false;
        app.session.backToMenu("You left the game.");
        return;
    }
    if (CheckCollisionPointRec(m, row(5)))
    {
        app.wantQuit = true;
        return;
    }
    if (!CheckCollisionPointRec(m, rect()))
        isOpen = false;
}

void PauseMenu::draw()
{
    Vector2 m = ui::mouse();
    DrawRectangle(0, 0, ui::screenW(), ui::screenH(), rgb(0, 0, 0, 150));
    Rectangle r = rect();
    drawTMap(mPause, r);
    ui::panelTitle(r, "Paused", 22, .55f, 13);

    drawScaleRow();
    drawWindowRow(m);
    drawButtons(m);
}

void PauseMenu::drawScaleRow()
{
    const Settings &s = app.settings;
    Color ink = rgb(70, 45, 30);
    Rectangle row0 = row(0), sl = slider();
    DrawText("UI scale", int(row0.x), int(row0.y) + 14, 15, ink);

    float k = (s.uiScale - 0.6f) / 1.4f;
    DrawRectangleRounded(sl, .5f, 6, rgb(60, 40, 28, 220));
    DrawRectangleRounded({sl.x, sl.y, std::max(8.f, sl.width * k), sl.height}, .5f, 6, rgb(80, 170, 220));
    DrawCircleV({sl.x + sl.width * k, sl.y + sl.height / 2}, 11, rgb(250, 235, 200));
    DrawCircleLines(int(sl.x + sl.width * k), int(sl.y + sl.height / 2), 11, rgb(60, 40, 28));
    DrawText(TextFormat("%d%%", int(std::lround(s.uiScale * 100.f))), int(row0.x + row0.width) - 46, int(row0.y) + 14,
             15, ink);
}

void PauseMenu::drawWindowRow(Vector2 m)
{
    const Settings &s = app.settings;
    Color ink = rgb(70, 45, 30);
    Rectangle row1 = row(1);
    DrawText("Window", int(row1.x), int(row1.y) + 9, 15, ink);

    for (int d = -1; d <= 1; d += 2)
    {
        Rectangle a = arrow(d);
        ui::drawButton(a, "", true, false, CheckCollisionPointRec(m, a), false, 14);
        float cx = a.x + a.width / 2, cy = a.y + a.height / 2;
        if (d < 0)
            DrawTriangle({cx + 5, cy - 8}, {cx - 7, cy}, {cx + 5, cy + 8}, rgb(255, 245, 210));
        else
            DrawTriangle({cx - 5, cy - 8}, {cx - 5, cy + 8}, {cx + 7, cy}, rgb(255, 245, 210));
    }

    const char *mn = Settings::modeName(s.windowMode);
    DrawText(mn, int(row1.x + 96 + (row1.width - 96 - 0) / 2) - 15 - MeasureText(mn, 14) / 2 + 0, int(row1.y) + 10, 14,
             ink);
}

void PauseMenu::drawButtons(Vector2 m)
{
    bool saved = GetTime() - savedAt < 2.0;
    ui::drawButton(row(2), saved ? "Saved!" : "Save settings", true, false, CheckCollisionPointRec(m, row(2)), false,
                   15);
    ui::drawButton(row(3), "Resume", true, false, CheckCollisionPointRec(m, row(3)), false, 15);
    ui::drawButton(row(4), "Leave to main menu", true, false, CheckCollisionPointRec(m, row(4)), false, 15);
    ui::drawButton(row(5), "Quit game", true, true, CheckCollisionPointRec(m, row(5)), false, 15);
}
