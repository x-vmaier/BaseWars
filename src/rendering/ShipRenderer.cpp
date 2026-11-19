#include "rendering/ShipRenderer.hpp"

#include "client/App.hpp"
#include "rendering/Bars.hpp"
#include "rendering/Palette.hpp"
#include "rendering/Sprites.hpp"
#include "rendering/Text.hpp"

#include <cmath>
#include <string>

// Path lines: thin, light gray, translucent; only what the server sent us.
void ShipRenderer::queuePathLines(DrawList& items) {
    ClientWorld& W = app.world;
    std::erase_if(W.pathLines, [&](auto& kv) { return W.ents.find(kv.first) == W.ents.end(); });
    if (W.pathLines.empty()) return;
    items.push_back({-1e8f, [this]() {
        const ClientWorld& cw = app.world;
        float th = std::max(1.6f, 2.2f / std::max(.4f, app.cam.zoom));
        for (auto& [id, pts] : cw.pathLines) {
            auto it = cw.ents.find(id);
            if (it == cw.ents.end() || pts.empty()) continue;
            const RPos& rp = cw.reg.get<RPos>(it->second);
            Vector2 prev{rp.x * TILE, rp.y * TILE};
            for (auto& q : pts) {
                Vector2 nx{(float(q.x) + .5f) * TILE, (float(q.y) + .5f) * TILE};
                DrawLineEx(prev, nx, th, rgb(215, 215, 220, 130));
                prev = nx;
            }
            DrawCircleV(prev, th * 1.6f, rgb(225, 225, 230, 150));
        }
    }});
}

void ShipRenderer::drawShip(float px, float py, int owner, bool flip, bool hover, double t, uint32_t id, int tier, int troops) {
    tier = std::clamp(tier, 1, 3);
    float s = tier == 1 ? 1.f : (tier == 2 ? 1.3f : 1.6f);
    float bob = std::sin(float(t) * 2.f + float(id)) * 2.5f;
    py += bob;
    float d = flip ? -1.f : 1.f;
    Color hullA = tier == 3 ? rgb(92, 62, 42) : (tier == 2 ? rgb(112, 78, 50) : rgb(122, 84, 48));
    Color hullB = tier == 3 ? rgb(108, 72, 48) : (tier == 2 ? rgb(128, 90, 56) : rgb(138, 96, 56));
    Color edge = rgb(70, 44, 26);
    DrawEllipse(int(px), int(py + 6 * s), int(46 * s), int(12 * s), rgb(0, 40, 80, 70));
    Vector2 a{px - 44 * s * d, py - 10 * s}, b{px + 44 * s * d, py - 10 * s}, c2{px + 30 * s * d, py + 8 * s}, e{px - 30 * s * d, py + 8 * s};
    DrawTriangle(a, e, c2, hullA);
    DrawTriangle(a, c2, b, hullB);
    DrawLineEx(a, b, 3, tier == 3 ? rgb(214, 170, 60) : rgb(88, 58, 32));       // gold rail on the galleon
    DrawLineEx(a, e, 2, edge);
    DrawLineEx(e, c2, 2, edge);
    DrawLineEx(c2, b, 2, edge);
    if (tier >= 2)                                                             // shield row along the rail
        for (int k = 0; k < 5; k++) {
            float u = -.7f + float(k) * .35f;
            DrawCircleV({px + 40 * s * u * d, py - 6 * s}, 3.6f * s, k % 2 ? teamColor(owner) : rgb(235, 230, 210));
        }
    if (tier == 3) {                                                           // figurehead + stern cabin
        DrawTriangle({px + 44 * s * d, py - 10 * s}, {px + 56 * s * d, py - 16 * s}, {px + 40 * s * d, py - 2 * s}, rgb(214, 170, 60));
        DrawRectangle(int(px - 40 * s * d - (flip ? 0 : 0) - 12), int(py - 26 * s), 24, int(14 * s), rgb(96, 66, 38));
        DrawRectangleLines(int(px - 40 * s * d - 12), int(py - 26 * s), 24, int(14 * s), edge);
    }
    int masts = tier;
    for (int m = 0; m < masts; m++) {
        float mx = px + (masts == 1 ? 0.f : (float(m) - float(masts - 1) * .5f) * 30.f * s) * d;
        float h = (m == 0 || masts == 1 ? 56.f : 46.f) * s * (masts == 1 ? 1.f : .85f);
        DrawRectangle(int(mx - 2), int(py - 8 * s - h), 4, int(h), rgb(96, 66, 38));
        Vector2 s1{mx + 2, py - 6 * s - h}, s2{mx + 2 + 34 * s * d * (masts == 1 ? 1.f : .8f), py - 8 * s - h * .5f}, s3{mx + 2, py - 12 * s};
        Color sail = tier == 3 ? rgb(248, 242, 224) : rgb(240, 236, 220);
        if (d > 0) DrawTriangle(s1, s3, s2, sail); else DrawTriangle(s1, s2, s3, sail);
        if (tier == 3) DrawLineEx({s1.x, s1.y + 14 * s}, {s2.x, s2.y - 8 * s}, 2, teamColor(owner));
        if (m == 0) {
            Vector2 f1{mx - 2, py - 6 * s - h}, f2{mx - 2 - 18 * s * d, py - h + 2 * s}, f3{mx - 2, py - h + 14 * s};
            if (d > 0) DrawTriangle(f1, f3, f2, teamColor(owner)); else DrawTriangle(f1, f2, f3, teamColor(owner));
        }
    }
    int show = std::min(troops, 4);                                            // a few crew figures stand on deck while troops are aboard
    if (show > 0 && troops != UNIT_ABOARD_PROG) {
        Anim w = mkAnim(std::string("Units/") + COLOR_NAME[colorIdx(owner)] + " Units/Warrior/Warrior_Idle.png", 192, 192, 7, 96, 135);
        for (int k = 0; k < show; k++) drawAnim(w, frameFor(w, t, float(id % 5 + k)), px + (float(k) - float(show - 1) * .5f) * 15.f * s * d, py - 6 * s, flip, .42f * (tier == 1 ? 1.f : 1.1f), WHITE, hover ? 1 : 0, rgb(255, 255, 255));
    }
    if (hover) DrawEllipseLines(int(px), int(py - 4), int(50 * s), int(16 * s), WHITE);
}

// Overlay above an own/allied ship: tier name and load.
void ShipRenderer::drawLabel(float px, float py, const Info& i, bool selected, bool hover) {
    const ClientWorld& W = app.world;
    if (!(i.owner == W.me || W.alliedWith(i.owner)) || (!selected && !hover && i.prog == 0)) return;
    int tier = shipTier(i.aux ? i.aux : 1);
    float s = tier == 1 ? 1.f : (tier == 2 ? 1.3f : 1.6f);
    std::string t = TextFormat("%d/%d", int(i.prog), shipTroopCap(tier));
    textShadow(t, int(px) - MeasureText(t.c_str(), 14) / 2, int(py - 78 * s), 14, i.prog >= shipTroopCap(tier) ? rgb(255, 170, 120) : rgb(235, 245, 255));
    float y = py - 78 * s + 18;                                                 // troops aboard, then cargo (when carrying any)
    drawBar({px, y}, 56, float(i.prog) / float(std::max(1, shipTroopCap(tier))), BAR_TROOPS);
    if (i.stock > 0) drawBar({px, y + 10}, 56, float(i.stock) / float(std::max(1, shipResCap(tier))), BAR_CARGO);
}
