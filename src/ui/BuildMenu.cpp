#include "ui/BuildMenu.hpp"

#include "client/App.hpp"
#include "rendering/Palette.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/Text.hpp"
#include "rendering/UnitAnims.hpp"
#include "ui/CostRow.hpp"
#include "ui/GameText.hpp"
#include "ui/UiScale.hpp"

void BuildMenu::load()
{
    mBuild = loadTMap("build_menu");
}

// The open menu sits right of the social tab button (x 0..40) so both stay usable.
Rectangle BuildMenu::rect() const
{
    Vector2 s = mBuild.size();
    return {LEFT - (1 - slide) * (s.x + LEFT + 12), 150, s.x, s.y};
}
Rectangle BuildMenu::tab() const
{
    return {std::max(0.f, rect().x + rect().width - 6), 150, 40, 56};
}
Rectangle BuildMenu::item(int i) const
{
    Rectangle b = rect();
    float pad = float(mBuild.padding);
    return {b.x + pad + 2, b.y + pad + 34 + float(i) * 47.f, b.width - 2 * pad - 4, 44};
}
bool BuildMenu::over(Vector2 m) const
{
    return CheckCollisionPointRec(m, tab()) || (slide > .02f && CheckCollisionPointRec(m, rect()));
}

void BuildMenu::hotkeys()
{
    for (int i = 0; i < COUNT; i++)
        if (IsKeyPressed(KEY_ONE + i))
            app.placement.selectBuildKind(LIST[i]);
}

bool BuildMenu::click(Vector2 m)
{
    if (CheckCollisionPointRec(m, tab()))
    {
        isOpen = !isOpen;
        return true;
    }
    if (slide > .9f && CheckCollisionPointRec(m, rect()))
    {
        for (int i = 0; i < COUNT; i++)
            if (CheckCollisionPointRec(m, item(i)))
            {
                app.placement.selectBuildKind(LIST[i]);
                return true;
            }
        return true;
    }
    return false;
}

void BuildMenu::draw(float dt)
{
    const ClientWorld &W = app.world;
    Vector2 m = ui::mouse();
    slide += ((isOpen ? 1.f : 0.f) - slide) * std::min(1.f, 12.f * dt);
    Rectangle br = rect();
    Rectangle tb = tab();
    DrawRectangleRounded(tb, .3f, 6, rgb(70, 50, 34, 235));
    DrawRectangleRoundedLinesEx(tb, .3f, 6, 2, rgb(30, 20, 14));
    Texture2D hm = tex("UI Elements/UI Elements/Icons/Icon_01.png");
    DrawTexturePro(hm, {0, 0, 64, 64}, {tb.x + 4, tb.y + 8, 40, 40}, {0, 0}, 0, WHITE);
    if (slide <= .01f)
        return;
    int hoverItem = -1, hoverGate = 0;
    drawTMap(mBuild, br);
    textShadow("BUILD", int(br.x + br.width / 2) - 28, int(br.y) + mBuild.padding + 8, 20, rgb(255, 240, 200));
    for (int i = 0; i < COUNT; i++)
    {
        Kind k = LIST[i];
        Rectangle it = item(i);
        float cost[R_Count];
        buildCost(k, cost);
        int gate = W.buildGateErr(k);
        bool act = app.placement.buildKind == int(k), hov = CheckCollisionPointRec(m, it) && slide > .9f;
        DrawRectangleRounded(it, .2f, 5,
                             act ? rgb(80, 150, 80, 210)
                                 : (gate ? rgb(20, 20, 30, 120) : (hov ? rgb(255, 255, 255, 70) : rgb(0, 0, 0, 60))));
        Color dim = gate ? rgb(120, 120, 130, 255) : WHITE;
        BSpr s = buildingSprite(k, 1);
        if (s.file[0])
        {
            Texture2D bt =
                tex(std::string("Buildings/") + COLOR_NAME[colorIdx(W.me)] + " Buildings/" + s.file + ".png");
            float sc = 38.f / s.artH * 1.f;
            DrawTexturePro(bt, {0, 0, s.artW, s.artH}, {it.x + 4, it.y + 3, s.artW * sc, s.artH * sc}, {0, 0}, 0, dim);
        }
        else if (hasTag(k, KT_Warm))
        {
            Anim f = mkAnim("Particle FX/Fire_03.png", 64, 64, 12, 32, 62);
            drawAnim(f, frameFor(f, GetTime()), it.x + 24, it.y + 40, false, .8f);
        }
        else
        {
            Texture2D g = tex("Terrain/Resources/Gold/Gold Stones/Gold Stone 2.png");
            DrawTexturePro(g, {0, 0, 128, 128}, {it.x + 2, it.y - 2, 48, 48}, {0, 0}, 0, WHITE);
        }
        int have = W.countKind(k), th = W.thLevel(), mx = maxCount(k, th);
        textShadow(TextFormat("[%d] %s", i + 1, KINFO[k].name), int(it.x) + 54, int(it.y) + 6, 16, dim);
        if (mx > 0)
        {
            std::string cnt = TextFormat("%d/%d", have, mx);
            DrawText(cnt.c_str(), int(it.x + it.width) - MeasureText(cnt.c_str(), 12) - 6, int(it.y) + 8, 12,
                     have >= mx ? rgb(255, 140, 120) : rgb(220, 220, 220));
        }
        if (gate == G_Locked)
            DrawText(TextFormat("Requires Town Hall Lv%d", thUnlock(k)), int(it.x) + 54, int(it.y) + 26, 13,
                     rgb(255, 150, 120));
        else if (gate)
            DrawText(TextFormat("Limit reached (Town Hall Lv%d)", th), int(it.x) + 54, int(it.y) + 26, 13,
                     rgb(255, 150, 120));
        else
        { // cost row: icon + value per resource, red when missing
            ui::CostStyle st = rowStyle(it.width - 58, cost);
            ui::drawCosts(cost, it.x + 54, it.y + 25, it.width - 58, W.res, st);
        }
        if (hov)
        {
            hoverItem = i;
            hoverGate = gate;
        }
    }
    DrawText("Shift: keep placing", int(br.x) + mBuild.padding + 4, int(br.y + br.height) - mBuild.padding - 20, 12,
             rgb(255, 235, 200));
    if (hoverItem >= 0)
        tooltip(LIST[hoverItem], item(hoverItem), hoverGate);
}

// compact cost style that fits one line of an entry (shrinks for long cost lists)
ui::CostStyle BuildMenu::rowStyle(float maxW, const float *cost)
{
    ui::CostStyle st = ui::darkCost();
    st.icon = 15;
    st.gap = 2;
    st.margin = 8;
    st.fs = 14;
    for (int i = 0; i < 3 && ui::measureCosts(cost, maxW, st).y > st.icon + 1; i++)
    {
        st.icon -= 1;
        st.margin -= 2;
        st.fs -= 1;
    }
    return st;
}

// hover tooltip beside the menu: name, cost row, build time, how many are allowed, and the lock reason
void BuildMenu::tooltip(Kind k, Rectangle it, int gate)
{
    const ClientWorld &W = app.world;
    float cost[R_Count];
    buildCost(k, cost);
    int th = W.thLevel();
    std::string head = KINFO[k].name, time = "Takes " + timeText(buildSeconds(k)) + " of pawn work";
    std::string cnt = maxCount(k, th) > 0 ? TextFormat("Built %d of %d allowed", W.countKind(k), maxCount(k, th)) : "";
    std::string why = gate ? gateText(gate, k, th) : "";
    ui::CostStyle st = ui::darkCost();
    float w = 250;
    std::vector<std::string> whyLines = why.empty() ? std::vector<std::string>{} : wrapText(why, int(w) - 20, 13);
    Vector2 cs = ui::measureCosts(cost, w - 20, st);
    float h = 10 + 20 + cs.y + 8 + 16 + (cnt.empty() ? 0.f : 16.f) + 15.f * float(whyLines.size()) + 8;
    float x = tab().x + tab().width + 8, y = std::clamp(it.y - 4, 8.f, float(ui::screenH()) - h - 8);
    DrawRectangleRounded({x, y, w, h}, .08f, 6, rgb(30, 22, 16, 238));
    DrawRectangleRoundedLinesEx({x, y, w, h}, .08f, 6, 2, rgb(255, 225, 160, 200));
    float ty = y + 8;
    textShadow(head, int(x) + 10, int(ty), 16, rgb(255, 230, 140));
    ty += 22;
    ui::drawCosts(cost, x + 10, ty, w - 20, W.res, st);
    ty += cs.y + 8;
    DrawText(time.c_str(), int(x) + 10, int(ty), 13, rgb(225, 225, 230));
    ty += 16;
    if (!cnt.empty())
    {
        DrawText(cnt.c_str(), int(x) + 10, int(ty), 13, rgb(225, 225, 230));
        ty += 16;
    }
    for (auto &l : whyLines)
    {
        DrawText(l.c_str(), int(x) + 10, int(ty), 13, rgb(255, 150, 120));
        ty += 15;
    }
}
