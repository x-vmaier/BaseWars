/**
 * @file TrainingPanel.hpp
 * @brief Archery window: troop cards, troop details / training / troop upgrades, building upgrade, and the training
 * queue strip.
 */
#pragma once
#include "client/ClientState.hpp"
#include "client/Components.hpp"
#include "common/Shared.hpp"
#include "rendering/Assets.hpp"

#include <string>

class App;

class TrainingPanel
{
  public:
    explicit TrainingPanel(App &app) : app(app) {}
    void load();

    bool isOpen = false;
    int sel = 0;         // selected troop card (0 = pawn)
    uint32_t target = 0; // Archery building

    void openFor(uint32_t id)
    {
        isOpen = true;
        target = id;
        sel = 0;
    }
    void input(Vector2 mouse);
    void draw();

  private:
    struct PanelXf
    {
        float ox, oy, sc;
    };
    PanelXf xf() const;
    Vector2 toPanel(Vector2 m) const;
    int troopLevel(Kind k) const;
    Rectangle card(int i) const;
    Rectangle detail() const;
    Rectangle btnTrain() const;
    Rectangle btnTroopUp() const;
    Rectangle block() const;
    Rectangle btnBuildingUp() const;
    Rectangle closeBtn() const;
    Rectangle queueSlot(int i) const;
    bool queueClick(Vector2 m);                             // click a queued unit to cancel it (refund)
    std::string lockReason(Kind k, int archeryLevel) const; // "Town Hall Lv4" / "Archery Lv3" / "" when trainable
    void buttonInput(Vector2 m, const Info &in);
    void drawFrame(const Info &bin, Vector2 m);
    void drawCard(int i, const Info &bin, Vector2 m, double now);
    void drawCardLock(Rectangle card, Kind k);
    void drawTroopDetail(const Info &bin, Vector2 m, double now);
    void drawTroopStats(Kind k, int level, Rectangle detail);
    void drawTroopButtons(Kind k, int level, int archeryLevel, Vector2 m);
    void drawBuildingBlock(const Info &bin, Vector2 m, double now);
    float drawBuildingUpgrade(const Info &bin, Vector2 m, const std::string &why, const BJob *job);
    void drawQueueStrip(Vector2 m);
    void sideCosts(Rectangle button, const float *cost, const std::string &reason);

    App &app;
    TMap mTrain;
};
