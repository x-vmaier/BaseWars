/**
 * @file Hud.hpp
 * @brief HUD: resource ribbon, banners, message log, help line, drag selection and the cursor.
 */
#pragma once
#include "client/ClientState.hpp"
#include "rendering/Assets.hpp"

#include <string>
#include <vector>

class App;

inline constexpr float SIDE_Y = 232; // top of the left side tabs, below the resource ribbon and name banner

class Hud
{
  public:
    explicit Hud(App &app) : app(app) {}
    void load();
    void onLog(const LogEvent &e); // dispatcher sink
    void clearLog() { logs.clear(); }

    Rectangle hudRect() const;
    Rectangle nameRect() const;
    Rectangle coordRect() const;
    static Rectangle socialTabBtn() { return {0, SIDE_Y + 64, 40, 56}; }
    bool over(Vector2 m) const; // ribbon / banner / coordinates / social tab

    void drawTop(); // resource ribbon + name banner
    void drawLog(); // message log, help line, command-mode banner
    void drawCoords();
    void drawBiome(); // hovered biome name, bottom right
    void drawSocialTab();
    void drawDragRect();
    void drawHoverLabel();
    void drawCursor();

  private:
    struct LogLine
    {
        std::string text;
        double t;
    };
    void drawResources();
    void drawNameBanner();
    void drawRaidArrow(); // screen-edge arrow towards the announced raid
    App &app;
    std::vector<LogLine> logs;
    TMap mHud, mRoster;
};
