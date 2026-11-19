#include "client/TestHooks.hpp"

#include "client/App.hpp"
#include "ui/UiScale.hpp"

#include <rlgl.h>

#include <cstdio>
#include <cstdlib>
#include <string>

TestHooks::TestHooks(App& app)
    : app(app), shotPath(std::getenv("BW_SHOT")), shotAfter(std::getenv("BW_SHOT_FRAMES") ? std::atoi(std::getenv("BW_SHOT_FRAMES")) : 400) {}

void TestHooks::update() {
    if (!shotPath || !app.session.welcomed) return;
    ClientWorld& W = app.world;
    if (shotFrames == 25) if (const char* c = std::getenv("BW_CMD")) {
        std::string cm = c;
        if (cm.rfind("chat:", 0) == 0) { app.chat.lines.push_back({0, "Tester", cm.substr(5), GetTime()}); app.chat.lines.push_back({1, "Friend", "clan chat line", GetTime()}); }
        if (cm.rfind("clan:", 0) == 0) app.cmd.clanCreate(cm.substr(5, cm.find(';') == std::string::npos ? std::string::npos : cm.find(';') - 5));
    }
    if (shotFrames == 300 || shotFrames == 360) if (const char* c = std::getenv("BW_CMD")) {
        std::string cm = c;
        auto p = cm.find("xfer:");
        if (p != std::string::npos) app.cmd.transfer(std::atoi(cm.c_str() + p + 5), R_Wood, 60.f);
    }
    if (shotFrames < 20) return;
    if (const char* mp = std::getenv("BW_MOUSE")) { float dx = 0, dy = 0; if (std::sscanf(mp, "%f,%f", &dx, &dy) == 2) SetMousePosition(::GetScreenWidth() / 2 + int(dx), ::GetScreenHeight() / 2 + int(dy)); }
    const char* u = std::getenv("BW_UI");
    if (!u) return;
    std::string ui = u;
    if (ui.find("build") != std::string::npos) app.buildMenu.isOpen = true;
    if (ui.find("place") != std::string::npos) app.placement.buildKind = K_Lumber;
    if (ui.find("social") != std::string::npos && shotFrames == 20) { app.social.isOpen = true; if (const char* tb = std::getenv("BW_TAB")) app.social.tab = std::atoi(tb); if (const char* se = std::getenv("BW_SEARCH")) app.social.searchText = se; }
    if (ui.find("map") != std::string::npos && shotFrames == 20) { app.bigMap.open(); if (const char* z = std::getenv("BW_MAPZ")) app.bigMap.zoom = std::atoi(z); }
    if (ui.find("pause") != std::string::npos) app.pause.isOpen = true;
    TrainingPanel& tp = app.training;
    ContextPanel& cp = app.context;
    if (ui.find("train") != std::string::npos && !tp.isOpen)
        W.reg.view<NetId, Info>().each([&](const NetId& n, const Info& i) { if (!tp.isOpen && i.owner == W.me && i.kind == K_Archery) { tp.isOpen = true; tp.target = n.id; if (const char* ts = std::getenv("BW_TSEL")) tp.sel = std::atoi(ts); } });
    float sw = float(ui::screenW()), sh = float(ui::screenH());
    if (ui.find("ctxop") != std::string::npos && !cp.isOpen)
        W.reg.view<NetId, Info>().each([&](const NetId& n, const Info& i) { if (!cp.isOpen && i.owner == W.me && i.kind == K_Outpost) cp.open(n.id, {sw * .5f, sh * .15f}); });
    if (ui.find("ctxfac") != std::string::npos && !cp.isOpen)
        W.reg.view<NetId, Info>().each([&](const NetId& n, const Info& i) { if (!cp.isOpen && isFactionOwner(i.owner) && i.kind == K_TownHall) cp.open(n.id, {sw * .55f, sh * .2f}); });
    else if (ui.find("ctx") != std::string::npos && ui.find("ctxfac") == std::string::npos && !cp.isOpen)
        W.reg.view<NetId, Info>().each([&](const NetId& n, const Info& i) { if (i.owner == W.me && i.kind == K_TownHall) cp.open(n.id, {sw * .6f, sh * .25f}); });
}

bool TestHooks::captureIfDue() {
    if (!shotPath || ++shotFrames != shotAfter) return false;
    rlDrawRenderBatchActive();                                 // flush batched UI draws before reading pixels
    Image img = LoadImageFromScreen();
    ExportImage(img, shotPath);
    UnloadImage(img);
    return true;
}
