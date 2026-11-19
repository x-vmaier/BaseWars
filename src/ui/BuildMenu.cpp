#include "ui/BuildMenu.hpp"

#include "client/App.hpp"
#include "rendering/Palette.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/Text.hpp"
#include "rendering/UnitAnims.hpp"
#include "ui/GameText.hpp"
#include "ui/UiScale.hpp"

void BuildMenu::load() { mBuild = loadTMap("build_menu"); }

Rectangle BuildMenu::rect() const { Vector2 s = mBuild.size(); return {10 - (1 - slide) * (s.x + 20), 150, s.x, s.y}; }
Rectangle BuildMenu::tab() const { return {std::max(0.f, rect().x + rect().width - 6), 150, 40, 56}; }
Rectangle BuildMenu::item(int i) const { Rectangle b = rect(); float pad = float(mBuild.padding); return {b.x + pad + 2, b.y + pad + 34 + float(i) * 47.f, b.width - 2 * pad - 4, 44}; }
bool BuildMenu::over(Vector2 m) const { return CheckCollisionPointRec(m, tab()) || (slide > .02f && CheckCollisionPointRec(m, rect())); }

void BuildMenu::hotkeys() {
    for (int i = 0; i < COUNT; i++) if (IsKeyPressed(KEY_ONE + i)) app.placement.selectBuildKind(LIST[i]);
}

bool BuildMenu::click(Vector2 m) {
    if (CheckCollisionPointRec(m, tab())) { isOpen = !isOpen; return true; }
    if (slide > .9f && CheckCollisionPointRec(m, rect())) {
        for (int i = 0; i < COUNT; i++) if (CheckCollisionPointRec(m, item(i))) { app.placement.selectBuildKind(LIST[i]); return true; }
        return true;
    }
    return false;
}

void BuildMenu::draw(float dt) {
    const ClientWorld& W = app.world;
    Vector2 m = ui::mouse();
    slide += ((isOpen ? 1.f : 0.f) - slide) * std::min(1.f, 12.f * dt);
    Rectangle br = rect();
    Rectangle tb = tab();
    DrawRectangleRounded(tb, .3f, 6, rgb(70, 50, 34, 235));
    DrawRectangleRoundedLinesEx(tb, .3f, 6, 2, rgb(30, 20, 14));
    Texture2D hm = tex("UI Elements/UI Elements/Icons/Icon_01.png");
    DrawTexturePro(hm, {0, 0, 64, 64}, {tb.x + 4, tb.y + 8, 40, 40}, {0, 0}, 0, WHITE);
    if (slide <= .01f) return;
    drawTMap(mBuild, br);
    textShadow("BUILD", int(br.x + br.width / 2) - 28, int(br.y) + mBuild.padding + 8, 20, rgb(255, 240, 200));
    for (int i = 0; i < COUNT; i++) {
        Kind k = LIST[i];
        Rectangle it = item(i);
        bool aff = W.affordable(KINFO[k].cost), act = app.placement.buildKind == int(k), hov = CheckCollisionPointRec(m, it) && slide > .9f;
        DrawRectangleRounded(it, .2f, 5, act ? rgb(80, 150, 80, 210) : (hov ? rgb(255, 255, 255, 70) : rgb(0, 0, 0, 60)));
        BSpr s = buildingSprite(k, 1);
        if (s.file[0]) {
            Texture2D bt = tex(std::string("Buildings/") + COLOR_NAME[colorIdx(W.me)] + " Buildings/" + s.file + ".png");
            float sc = 38.f / s.artH * 1.f;
            DrawTexturePro(bt, {0, 0, s.artW, s.artH}, {it.x + 4, it.y + 3, s.artW * sc, s.artH * sc}, {0, 0}, 0, WHITE);
        } else if (k == K_Campfire) { Anim f = mkAnim("Particle FX/Fire_03.png", 64, 64, 12, 32, 62); drawAnim(f, frameFor(f, GetTime()), it.x + 24, it.y + 40, false, .8f); }
        else { Texture2D g = tex("Terrain/Resources/Gold/Gold Stones/Gold Stone 2.png"); DrawTexturePro(g, {0, 0, 128, 128}, {it.x + 2, it.y - 2, 48, 48}, {0, 0}, 0, WHITE); }
        textShadow(TextFormat("[%d] %s", i + 1, KINFO[k].name), int(it.x) + 54, int(it.y) + 6, 16, WHITE);
        std::string c = costText(KINFO[k].cost);
        DrawText(c.c_str(), int(it.x) + 54, int(it.y) + 26, 14, aff ? rgb(255, 225, 120) : rgb(255, 120, 110));
    }
    DrawText("Shift: keep placing", int(br.x) + mBuild.padding + 4, int(br.y + br.height) - mBuild.padding - 14, 12, rgb(255, 235, 200));
}
