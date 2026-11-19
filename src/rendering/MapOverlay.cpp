#include "rendering/MapOverlay.hpp"

#include "client/App.hpp"
#include "rendering/Palette.hpp"

#include <cmath>
#include <set>

void MapOverlay::icon(const std::string &file, Vector2 p, float width, float artW, float artH, Color tint)
{
    Texture2D t = tex(file);
    float sc = width / artW;
    DrawTexturePro(t, {0, 0, artW, artH}, {p.x - width / 2, p.y - artH * sc * .8f, width, artH * sc}, {0, 0}, 0, tint);
}

void MapOverlay::draw(Rectangle in, Vector2 c, float tpp, bool mini)
{
    const ClientWorld &W = app.world;
    auto T = [&](float tx, float ty)
    { return Vector2{in.x + in.width / 2 + (tx - c.x) / tpp, in.y + in.height / 2 + (ty - c.y) / tpp}; };
    auto inside = [&](Vector2 p, float pad)
    { return p.x > in.x + pad && p.y > in.y + pad && p.x < in.x + in.width - pad && p.y < in.y + in.height - pad; };
    float castle = mini ? 15.f : 28.f, mycastle = mini ? 19.f : 34.f;
    W.reg.view<Info, Goal>().each([&](const Info &i, const Goal &g) { // territory: clans share one colour
        if (!isBuilding(i.kind) || i.prog < 100 || (i.state & BLD_DISABLED) || ((i.state & BLD_MOVING) && i.kind != K_TownHall) || isFactionOwner(i.owner) || (i.kind != K_TownHall && i.kind != K_Outpost))
            return;
        auto p = W.peerOf(i.owner);
        int clan = i.owner == W.me ? W.myClan() : (p ? p->clan : 0);
        Color col = clan ? clanColor(clan) : teamColor(i.owner);
        float r = float(anchorRadius(i.kind)) / tpp;
        Vector2 sp = T(g.x, g.y);
        float x0 = std::max(sp.x - r, in.x), y0 = std::max(sp.y - r, in.y), x1 = std::min(sp.x + r, in.x + in.width), y1 = std::min(sp.y + r, in.y + in.height);
        if (x1 <= x0 || y1 <= y0)
            return;
        DrawRectangleRec({x0, y0, x1 - x0, y1 - y0}, Color{col.r, col.g, col.b, 55});
        DrawRectangleLinesEx({x0, y0, x1 - x0, y1 - y0}, mini ? 1.f : 2.f, Color{col.r, col.g, col.b, 190});
    });
    for (auto &t : W.facTowns)
    { // discovered faction towns
        Vector2 sp = T(t.x, t.y);
        if (!inside(sp, mini ? 6.f : 10.f))
            continue;
        int rp = W.facRep[t.faction];
        Color ring = rp <= HOSTILE_REP ? rgb(240, 70, 60) : (rp >= FRIENDLY_REP ? rgb(90, 220, 110) : rgb(235, 220, 120));
        DrawCircleV({sp.x, sp.y - castle * .25f}, castle * .52f, Color{ring.r, ring.g, ring.b, 90});
        icon(std::string("Buildings/") + COLOR_NAME[FACTION_COLOR[t.faction]] + " Buildings/Castle.png", sp, castle - 2, 320, 256);
        if (!mini)
        {
            std::string nm = FACTION_NAME[t.faction];
            DrawText(nm.c_str(), int(sp.x) - MeasureText(nm.c_str(), 10) / 2, int(sp.y) + 6, 10, WHITE);
        }
    }
    std::set<int> drawnTH;
    W.reg.view<Info, Goal>().each([&](const Info &i, const Goal &g) { // player towns and outposts we know about
        if (!isBuilding(i.kind) || isFactionOwner(i.owner) || (i.kind != K_TownHall && i.kind != K_Outpost))
            return;
        Vector2 sp = T(g.x, g.y);
        if (i.kind == K_TownHall)
            drawnTH.insert(i.owner);
        if (!inside(sp, mini ? 4.f : 8.f))
            return;
        bool mine = i.owner == W.me;
        if (i.kind == K_TownHall)
        {
            if (mine)
                DrawCircleV({sp.x, sp.y - mycastle * .25f}, mycastle * .5f, rgb(255, 215, 80, 110));
            icon(std::string("Buildings/") + COLOR_NAME[colorIdx(i.owner)] + " Buildings/Castle.png", sp, mine ? mycastle : castle, 320, 256);
            int hallOf = W.leaderClanOf(i.owner);
            if (hallOf)
            { // clan hall: a banner in the clan colour
                Color cc = clanColor(hallOf);
                float bx = sp.x, by = sp.y - (mine ? mycastle : castle) * .85f;
                DrawRectangle(int(bx) - 1, int(by) - (mini ? 6 : 10), 2, mini ? 8 : 12, BLACK);
                DrawTriangle({bx + 1, by - (mini ? 6.f : 10.f)}, {bx + 1, by - (mini ? 1.f : 3.f)}, {bx + (mini ? 6.f : 10.f), by - (mini ? 3.5f : 6.5f)}, cc);
            }
            if (!mini)
            {
                std::string nm = W.nameOf(i.owner);
                DrawText(nm.c_str(), int(sp.x) - MeasureText(nm.c_str(), 11) / 2, int(sp.y) + 6, 11, mine ? rgb(255, 235, 140) : (W.alliedWith(i.owner) ? rgb(150, 255, 170) : WHITE));
            }
        }
        else
            icon(std::string("Buildings/") + COLOR_NAME[colorIdx(i.owner)] + " Buildings/Tower.png", sp, mini ? 6.f : 12.f, 128, 256);
    });
    for (auto &p : W.roster)
    { // towns whose location we know but cannot currently see
        if (p.id == W.me || !p.hasLoc || drawnTH.count(p.id))
            continue;
        Vector2 sp = T(p.lx, p.ly);
        if (!inside(sp, mini ? 4.f : 8.f))
            continue;
        icon(std::string("Buildings/") + COLOR_NAME[colorIdx(p.id)] + " Buildings/Castle.png", sp, castle - 2, 320, 256, rgb(255, 255, 255, 200));
        if (!mini)
            DrawText(p.name.c_str(), int(sp.x) - MeasureText(p.name.c_str(), 11) / 2, int(sp.y) + 6, 11, WHITE);
    }
    double now = GetTime();
    W.reg.view<Info, RPos>().each([&](const Info &i, const RPos &p) { // travelling convoys and hauling parties
        if (!(i.state & UNIT_CONVOY_FLAG))
            return;
        Vector2 sp = T(p.x, p.y);
        if (!inside(sp, mini ? 3.f : 6.f))
            return;
        bool friendly = W.alliedWith(i.owner);
        Color col = friendly ? rgb(100, 230, 255) : rgb(255, 70, 60);
        float pulse = friendly ? 0.f : 3.f * float(std::sin(now * 6) * .5 + .5), rad = (mini ? 4.f : 8.f) + pulse;
        DrawCircleV(sp, rad, Color{col.r, col.g, col.b, 110});
        DrawCircleLines(int(sp.x), int(sp.y), rad, col);
        if (mini)
        {
            DrawCircleV(sp, 2, col);
            return;
        }
        Anim a = mkAnim(std::string("Units/") + COLOR_NAME[colorIdx(i.owner)] + " Units/Pawn/Pawn_Run Gold.png", 192, 192, 10, 96, 135);
        DrawTexturePro(a.t, frameRect(a, 0), {sp.x - 12, sp.y - 16, 24, 24}, {0, 0}, 0, WHITE);
        if (!friendly)
        {
            DrawText("!", int(sp.x) + 9, int(sp.y) - 16, 14, rgb(255, 90, 70));
            DrawText((W.nameOf(i.owner) + " convoy").c_str(), int(sp.x) + 14, int(sp.y) - 4, 10, rgb(255, 170, 150));
        }
    });
    supportIcons(in, c, tpp, mini);
}

// support patrols are drawn on both maps with a shield-and-sword icon (orange: ours / allies, red: hostile)
void MapOverlay::supportIcons(Rectangle in, Vector2 c, float tpp, bool mini)
{
    const ClientWorld &W = app.world;
    double now = GetTime();
    W.reg.view<Info, RPos>().each([&](const Info &i, const RPos &p)
                                  {
        if (isBuilding(i.kind) || !(i.state & UNIT_SUPPORT_FLAG)) return;
        Vector2 sp{in.x + in.width / 2 + (p.x - c.x) / tpp, in.y + in.height / 2 + (p.y - c.y) / tpp};
        if (sp.x < in.x + 3 || sp.y < in.y + 3 || sp.x > in.x + in.width - 3 || sp.y > in.y + in.height - 3) return;
        bool friendly = W.alliedWith(i.owner);
        Color col = friendly ? rgb(255, 170, 40) : rgb(255, 70, 60);
        float s = mini ? 4.f : 9.f + (friendly ? 0.f : 2.f * float(std::sin(now * 6) * .5 + .5));
        Vector2 a{sp.x, sp.y - s}, b{sp.x + s, sp.y}, d{sp.x, sp.y + s}, e{sp.x - s, sp.y};
        DrawTriangle(a, e, b, Color{col.r, col.g, col.b, 230});
        DrawTriangle(e, d, b, Color{col.r, col.g, col.b, 230});
        DrawTriangleLines(a, e, b, BLACK);
        DrawLineV(e, d, BLACK);
        DrawLineV(d, b, BLACK);
        if (mini) return;
        DrawLineEx({sp.x - 4, sp.y + 4}, {sp.x + 4, sp.y - 4}, 2, WHITE);          // crossed sword
        DrawLineEx({sp.x - 4, sp.y - 4}, {sp.x + 4, sp.y + 4}, 1, rgb(60, 40, 20));
        if (tpp <= 2.f) DrawText(friendly ? "Patrol" : "Hostile patrol", int(sp.x) + 12, int(sp.y) - 5, 10, friendly ? rgb(255, 215, 130) : rgb(255, 150, 140)); });
}
