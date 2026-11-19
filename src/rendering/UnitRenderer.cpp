#include "rendering/UnitRenderer.hpp"

#include "client/App.hpp"
#include "rendering/Bars.hpp"
#include "rendering/Palette.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/Text.hpp"
#include "rendering/UnitAnims.hpp"

#include <cmath>
#include <string>

void UnitRenderer::queue(const RPos& p, Info& i, Face& fc, uint32_t id, bool selected, bool hover, DrawList& items, double t, int biome) {
    const ClientWorld& W = app.world;
    float px = p.x * TILE, py = p.y * TILE;
    bool moving = (i.state & 7) == US_Move, convoy = (i.state & UNIT_CONVOY_FLAG) != 0;
    int carry = convoy && !i.aux ? 2 : i.aux;
    Color oc = i.owner == W.me || W.alliedWith(i.owner) ? rgb(255, 255, 255) : (W.hostileTo(i.owner) ? rgb(255, 90, 90) : rgb(255, 225, 130));
    if (i.kind == K_Sheep) oc = rgb(255, 235, 140);
    items.push_back({py, [=, this]() {
        if (i.kind == K_Ship) {
            ShipRenderer& ships = app.worldRenderer.ships;
            ships.drawShip(px, py, i.owner, fc.left, hover, t, id, i.aux, i.prog);
            ships.drawLabel(px, py, i, selected, hover);
            if (selected) DrawEllipseLines(int(px), int(py + 4), 56, 18, rgb(255, 235, 90));
            return;
        }
        Anim a = unitAnim(i.kind, i.owner, i.state & 7, carry, i.aim, moving, biome, t);
        DrawEllipse(int(px), int(py + 2), 16, 6, rgb(0, 0, 0, 60));
        if (selected) DrawEllipseLines(int(px), int(py + 2), 24, 10, rgb(255, 235, 90));
        drawAnim(a, frameFor(a, t, float(id % 7)), px, py + 6, fc.left, 1.f, WHITE, hover ? 1 : 0, oc);
        if (convoy) textShadow("Convoy", int(px) - 22, int(py) - 70, 14, rgb(120, 230, 255));
        if ((i.state & UNIT_SUPPORT_FLAG) && !convoy) textShadow("Patrol", int(px) - 20, int(py) - 70, 14, app.world.alliedWith(i.owner) ? rgb(255, 190, 80) : rgb(255, 110, 100));
        if ((i.state & 7) == US_Extinguish) extinguishSplash(px, py, fc.left, t, id);
        if (i.kind == K_Monk && (i.state & 7) == US_Heal) { Anim h = mkAnim(std::string("Units/") + COLOR_NAME[colorIdx(i.owner)] + " Units/Monk/Heal_Effect.png", 192, 192, 10, 96, 135); drawAnim(h, frameFor(h, t), px, py + 6, false, 1.f); }
        float fr = i.hp / std::max(1.f, i.maxhp);
        if ((fr < .995f || selected || hover) && i.kind != K_Sheep) drawBar({px, py - 56}, 48, fr, fr > .5f ? rgb(90, 220, 90) : (fr > .25f ? rgb(240, 190, 50) : rgb(235, 70, 60)));
    }});
    if (i.kind == K_Ship && moving)
        items.push_back({py - 1, [=]() { Anim s = mkAnim("Particle FX/Water Splash.png", 192, 192, 8, 96, 150); drawAnim(s, frameFor(s, t, float(id % 3)), px - (fc.left ? -30.f : 30.f), py + 6, false, .45f, rgb(255, 255, 255, 140)); }});
}

void UnitRenderer::extinguishSplash(float px, float py, bool left, double t, uint32_t id) {
    Anim s = mkAnim("Particle FX/Water Splash.png", 192, 192, 10, 96, 150);
    float dir = left ? -1.f : 1.f;
    drawAnim(s, frameFor(s, t, float(id % 5)), px + dir * 34.f, py + 8, false, .42f, rgb(255, 255, 255, 220));
    for (int k = 0; k < 4; k++) {                                       // droplets arcing towards the fire
        float ph = float(std::fmod(t * 2.2 + double(k) * .25 + double(id % 7) * .1, 1.0));
        DrawCircleV({px + dir * (14.f + ph * 34.f), py - 38.f - std::sin(ph * 3.14159f) * 22.f + ph * 28.f}, 3.f, rgb(120, 190, 255, int(230 * (1.f - ph * .5f))));
    }
}
