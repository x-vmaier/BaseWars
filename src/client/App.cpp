/**
 * @file App.cpp
 * @brief The client application: owns shared state and every subsystem, and runs the frame loop.
 */
#include "client/App.hpp"

#include "rendering/Sprites.hpp"
#include "ui/UiScale.hpp"
#include "ui/Widgets.hpp"

App::App()
    : session(*this), packets(*this), cmd(*this), placement(*this), input(*this), hooks(*this), water(*this),
      post(*this), worldRenderer(*this), minimap(*this), mapOverlay(*this), backdrop(*this), hud(*this),
      buildMenu(*this), context(*this), training(*this), townHall(*this), social(*this), chat(*this), pause(*this),
      mainMenu(*this), bigMap(*this), shipPanel(*this), baseMenus(*this)
{
    ui::bindScale(&settings.uiScale);
}

bool App::overUI(Vector2 m)
{
    if (hud.over(m) || CheckCollisionPointRec(m, minimap.frame()))
        return true;
    if (buildMenu.over(m))
        return true;
    if (social.isOpen || bigMap.isOpen || training.isOpen || townHall.isOpen || chat.isOpen)
        return true;
    if (baseMenus.over(m))
        return true;
    if (context.over(m))
        return true;
    if (shipPanel.over(m))
        return true;
    return false;
}

void App::resetWorld()
{
    world.clear();
    input.sel.clear();
    input.hovered = 0;
    input.mode = Input::M_None;
    chat.lines.clear();
    hud.clearLog();
    townHall.close();
    context.isOpen = training.isOpen = social.isOpen = bigMap.isOpen = chat.isOpen = false;
    placement.buildKind = -1;
    session.resetFlags();
}

void App::travelTo(float tx, float ty)
{
    cam.target = {tx * TILE, ty * TILE};
    social.isOpen = false;
    bigMap.isOpen = false;
    social.textFocus = false;
}

void App::loadGfx()
{
    loadSpriteShaders();
    water.load();
    post.load();
    ui::loadWidgets();
    hud.load();
    buildMenu.load();
    minimap.load();
    context.load();
    social.load();
    baseMenus.load();
    bigMap.load();
    training.load();
    townHall.load();
    mainMenu.load();
    pause.load();
}

// Order matters: later panels draw over earlier ones.
void App::drawUI(float dt)
{
    hud.drawTop();
    if (!social.isOpen)
        buildMenu.draw(dt);

    hud.drawLog();
    minimap.draw();
    hud.drawCoords();
    hud.drawSocialTab();
    if (bigMap.isOpen)
        bigMap.draw();

    if (social.isOpen)
    {
        social.draw();
        buildMenu.draw(dt); // both can be open: the build menu stays above the dimming
    }

    if (training.isOpen)
        training.draw();

    if (townHall.isOpen)
        townHall.draw();

    chat.draw();
    hud.drawBiome();
    baseMenus.draw();
    context.draw();
    hud.drawDragRect();
    shipPanel.draw();
    hud.drawHoverLabel();
}

void App::run(const std::string &autoJoin)
{
    disp.sink<LogEvent>().connect<&Hud::onLog>(hud);
    loadGfx();
    mainMenu.refresh();

    if (!autoJoin.empty())
    {
        ServerEntry e;
        e.name = autoJoin;
        e.address = autoJoin;
        e.local = true;
        session.joinServer(e);
    }

    while (!WindowShouldClose() && !wantQuit)
    {
        float dt = GetFrameTime();
        session.steam.run();
        session.pump();
        session.refreshNames();
        disp.update();
        post.ensureTargets();

        bool playing = session.playing();
        if (playing)
        {
            input.update(dt);
            world.interpolate(dt);
            minimap.update();
        }
        else
        {
            mainMenu.update();
        }

        hooks.update();
        float t = float(GetTime());
        if (playing)
            drawScene(t);

        BeginDrawing();
        ClearBackground(rgb(20, 22, 30));
        if (playing)
            drawPlayingFrame(t, dt);
        else
            drawMenuFrame(t);

        // BW_SHOT saves a frame and quits
        if (hooks.captureIfDue())
        {
            EndDrawing();
            break;
        }
        EndDrawing();
    }

    settings.save(std::string(GetApplicationDirectory()));
    session.cnet.close();
    session.steam.shutdown();
}

void App::drawScene(float t)
{
    post.beginScene();
    water.draw(t);
    BeginMode2D(cam);
    worldRenderer.draw(t);
    EndMode2D();
    post.endScene();
}

void App::drawPlayingFrame(float t, float dt)
{
    HideCursor();
    post.present(t);
    ui::begin();
    if (context.isOpen)
        context.rebuild();
    drawUI(dt);
    if (pause.isOpen)
        pause.draw();
    hud.drawCursor();
    ui::end();
}

void App::drawMenuFrame(float t)
{
    ShowCursor();
    backdrop.draw(t);
    ui::begin();
    mainMenu.draw();
    ui::end();
}
