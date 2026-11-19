#include "client/TestHooks.hpp"

#include "client/App.hpp"
#include "ui/UiScale.hpp"

#include <rlgl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

TestHooks::TestHooks(App &app)
    : app(app), shotPath(std::getenv("BW_SHOT")),
      shotAfter(std::getenv("BW_SHOT_FRAMES") ? std::atoi(std::getenv("BW_SHOT_FRAMES")) : 400)
{
}

void TestHooks::update()
{
    if (!shotPath || !app.session.welcomed)
        return;

    ClientWorld &W = app.world;

    if (shotFrames == 25)
        if (const char *c = std::getenv("BW_CMD"))
        {
            std::string cm = c;
            if (cm.rfind("chat:", 0) == 0)
            {
                app.chat.lines.push_back({0, "Tester", cm.substr(5), GetTime()});
                app.chat.lines.push_back({1, "Friend", "clan chat line", GetTime()});
            }
            if (cm.rfind("clan:", 0) == 0)
                app.cmd.clanCreate(
                    cm.substr(5, cm.find(';') == std::string::npos ? std::string::npos : cm.find(';') - 5));
        }

    if (shotFrames == 300 || shotFrames == 360)
        if (const char *c = std::getenv("BW_CMD"))
        {
            std::string cm = c;
            auto p = cm.find("xfer:");
            if (p != std::string::npos)
                app.cmd.transfer(std::atoi(cm.c_str() + p + 5), R_Wood, 60.f);
        }

    siteCommands();
    if (shotFrames < 20)
        return;

    if (const char *mp = std::getenv("BW_MOUSE"))
    {
        float dx = 0, dy = 0;
        if (std::sscanf(mp, "%f,%f", &dx, &dy) == 2)
            SetMousePosition(::GetScreenWidth() / 2 + int(dx), ::GetScreenHeight() / 2 + int(dy));
    }

    const char *u = std::getenv("BW_UI");
    if (!u)
        return;

    std::string ui = u;
    if (ui.find("build") != std::string::npos)
        app.buildMenu.isOpen = true;
    if (ui.find("place") != std::string::npos)
        app.placement.buildKind = K_Lumber;
    if (ui.find("social") != std::string::npos && shotFrames == 20)
    {
        app.social.isOpen = true;
        if (const char *tb = std::getenv("BW_TAB"))
            app.social.tab = std::atoi(tb);
        if (const char *se = std::getenv("BW_SEARCH"))
            app.social.searchText = se;
    }
    if (ui.find("map") != std::string::npos && shotFrames == 20)
    {
        app.bigMap.open();
        if (const char *z = std::getenv("BW_MAPZ"))
            app.bigMap.zoom = std::atoi(z);
    }
    if (ui.find("pause") != std::string::npos)
        app.pause.isOpen = true;
    if (ui.find("chatbox") != std::string::npos && shotFrames == 20)
        app.chat.open();

    TrainingPanel &tp = app.training;
    ContextPanel &cp = app.context;

    if (ui.find("train") != std::string::npos && !tp.isOpen)
        W.reg.view<NetId, Info>().each([&](const NetId &n, const Info &i)
        {
            if (!tp.isOpen && i.owner == W.me && i.kind == K_Archery)
            {
                tp.isOpen = true;
                tp.target = n.id;
                if (const char *ts = std::getenv("BW_TSEL"))
                    tp.sel = std::atoi(ts);
            }
        });
    if (ui.find("townhall") != std::string::npos)
        townHallHook();

    float sw = float(ui::screenW()), sh = float(ui::screenH());
    if (ui.find("ctxop") != std::string::npos && !cp.isOpen)
        W.reg.view<NetId, Info>().each([&](const NetId &n, const Info &i)
        {
            if (!cp.isOpen && i.owner == W.me && i.kind == K_Outpost)
                cp.open(n.id, {sw * .5f, sh * .15f});
        });
    if (ui.find("ctxfac") != std::string::npos && !cp.isOpen)
        W.reg.view<NetId, Info>().each([&](const NetId &n, const Info &i)
        {
            if (!cp.isOpen && isFactionOwner(i.owner) && i.kind == K_TownHall)
                cp.open(n.id, {sw * .55f, sh * .2f});
        });
    else if (ui.find("ctx") != std::string::npos && ui.find("ctxfac") == std::string::npos && !cp.isOpen)
        W.reg.view<NetId, Info>().each([&](const NetId &n, const Info &i)
        {
            if (i.owner == W.me && i.kind == K_TownHall)
                cp.open(n.id, {sw * .6f, sh * .25f});
        });
}

// BW_UI=townhall opens the own Town Hall window; BW_THTAB=0|1|2 picks the tab; BW_THHOVER=<control> moves the mouse
// onto a control for hover states (train, upgrade, tab<i>, slider<r>, dispose<r>, arrow<t>l / arrow<t>r); BW_THCLICK=1
// with dispose<r> shows its confirm state.
void TestHooks::townHallHook()
{
    ClientWorld &W = app.world;
    TownHallWindow &th = app.townHall;
    if (!th.isOpen)
    {
        W.reg.view<NetId, Info>().each([&](const NetId &n, const Info &i)
        {
            if (!th.isOpen && i.owner == W.me && i.kind == K_TownHall)
                th.openFor(n.id);
        });
        if (const char *t = std::getenv("BW_THTAB"))
            th.tab = std::clamp(std::atoi(t), 0, int(TownHallWindow::T_Count) - 1);
    }
    const char *hv = std::getenv("BW_THHOVER");
    if (!th.isOpen || !hv)
        return;

    std::string h = hv;
    Rectangle r{0, 0, 0, 0};

    if (h == "train")
        r = th.trainBtn();
    else if (h == "upgrade")
        r = th.upgradeBtn();
    else if (h.rfind("slider", 0) == 0)
        r = th.allocSlider(std::atoi(h.c_str() + 6));
    else if (h.rfind("dispose", 0) == 0)
        r = th.disposeBtn(std::atoi(h.c_str() + 7));
    else if (h.rfind("arrow", 0) == 0 && h.size() >= 7)
        r = th.quotaArrow(h[5] - '0', h[6] == 'l' ? -1 : 1);
    else if (h.rfind("tab", 0) == 0)
        r = th.tabRect(std::atoi(h.c_str() + 3));

    Vector2 p = th.toScreen({r.x + r.width / 2, r.y + r.height / 2});
    SetMousePosition(int(p.x * ui::scale()), int(p.y * ui::scale()));
    if (std::getenv("BW_THCLICK") && h.rfind("dispose", 0) == 0)
        th.armDisposeForTest(std::atoi(h.c_str() + 7));
}

void TestHooks::siteCommands()
{
    const char *env = std::getenv("BW_SITE");
    if (!env)
        return;

    ClientWorld &W = app.world;
    auto firstOf = [&](int k)
    {
        uint32_t id = 0;
        W.reg.view<NetId, Info>().each([&](const NetId &n, const Info &i)
        {
            if (!id && i.owner == W.me && i.kind == k)
                id = n.id;
        });
        return id;
    };

    std::string all = env;
    size_t p = 0;
    while (p < all.size())
    {
        size_t e = all.find(';', p);
        std::string tok = all.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e == std::string::npos ? all.size() : e + 1;
        size_t at = tok.find('@'), c1 = tok.find(':');
        if (at == std::string::npos || c1 == std::string::npos || std::atoi(tok.c_str() + at + 1) != shotFrames)
            continue;

        std::string verb = tok.substr(0, c1), args = tok.substr(c1 + 1, at - c1 - 1);
        int kind = std::atoi(args.c_str()), dx = 0, dy = 0;

        if (verb == "build" && std::sscanf(args.c_str(), "%d:%d,%d", &kind, &dx, &dy) == 3)
        {
            Pos th = W.myTH(app.cam.target);
            app.cmd.build(Kind(kind), {int(std::floor(th.x)) + dx, int(std::floor(th.y)) + dy});
        }
        else if (verb == "alloc" && std::sscanf(args.c_str(), "%d:%d", &dx, &dy) == 2)
        {
            app.cmd.setAlloc(dx, dy);
        }
        else if (verb == "dispose")
        {
            app.cmd.dispose(kind);
        }
        else if (verb == "quota")
        {
            int q[QUOTA_TYPES]{};
            if (std::sscanf(args.c_str(), "%d,%d,%d,%d", &q[0], &q[1], &q[2], &q[3]) == 4)
                app.cmd.quotas(q);
        }
        else if (verb == "donate" && std::sscanf(args.c_str(), "%d:%d", &dx, &dy) == 2)
        {
            app.cmd.donate(dx, dy, 50.f);
        }
        else if (uint32_t id = firstOf(kind); id)
        {
            if (verb == "upgrade")
                app.cmd.upgrade(id);
            else if (verb == "train" && std::sscanf(args.c_str(), "%d:%d", &kind, &dx) == 2)
                app.cmd.train(id, Kind(dx));
            else if (verb == "repair")
                app.cmd.repair(id, 0);
            else if (verb == "cancel")
                app.cmd.repair(id, 1);
            else if (verb == "ctx")
                app.context.open(id, {float(ui::screenW()) * .55f, float(ui::screenH()) * .12f});
        }
    }
}

bool TestHooks::captureIfDue()
{
    if (!shotPath || ++shotFrames != shotAfter)
        return false;

    rlDrawRenderBatchActive(); // flush batched UI draws before reading pixels
    Image img = LoadImageFromScreen();
    ExportImage(img, shotPath);
    UnloadImage(img);
    return true;
}
