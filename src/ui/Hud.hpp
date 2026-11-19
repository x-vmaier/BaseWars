#pragma once
// HUD: resource ribbon, name banner, message log, help line, command-mode banner, camera coordinates, the social
// menu tab, drag-selection rectangle, hover label and the cursor.
#include "client/ClientState.hpp"
#include "rendering/Assets.hpp"

#include <string>
#include <vector>

class App;

class Hud {
public:
    explicit Hud(App& app) : app(app) {}
    void load();
    void onLog(const LogEvent& e);      // dispatcher sink
    void clearLog() { logs.clear(); }

    Rectangle hudRect() const;
    Rectangle nameRect() const;
    Rectangle coordRect() const;
    static Rectangle socialTabBtn() { return {0, 214, 40, 56}; }
    bool over(Vector2 m) const;         // ribbon / banner / coordinates / social tab

    void drawTop();                     // resource ribbon + name banner
    void drawLog();                     // message log, help line, command-mode banner
    void drawCoords();
    void drawSocialTab();
    void drawDragRect();
    void drawHoverLabel();
    void drawCursor();

private:
    struct LogLine { std::string text; double t; };
    App& app;
    std::vector<LogLine> logs;
    TMap mHud, mRoster;
};
