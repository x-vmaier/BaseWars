#include "rendering/WorldRenderer.hpp"

#include "client/App.hpp"

#include <algorithm>
#include <cmath>

void WorldRenderer::draw(float t)
{
    ClientWorld &W = app.world;
    const Camera2D &cam = app.cam;
    Vector2 tl = GetScreenToWorld2D({0, 0}, cam), br = GetScreenToWorld2D({float(::GetScreenWidth()), float(::GetScreenHeight())}, cam);
    TerrainView v = terrain.view(tl, br);
    terrain.drawGround(v, t);

    DrawList items;
    items.reserve(512);
    features.frame();
    features.queue(v, v.x0, v.y0 - 0, v.x1, v.y1 + 4, items, t);
    ships.queuePathLines(items);
    effects.queueMarkers(items);
    Rectangle vr{tl.x - 400, tl.y - 400, br.x - tl.x + 800, br.y - tl.y + 800};
    const auto &sel = app.input.sel;
    uint32_t hovered = app.input.hovered;
    W.reg.view<RPos, Info, NetId, Face>().each([&](entt::entity e, const RPos &p, Info &i, const NetId &n, Face &fc)
                                               {
        if (!CheckCollisionPointRec({p.x * TILE, p.y * TILE}, vr)) return;
        if (aboardUnit(i)) return;
        bool selected = sel.count(n.id) > 0, hover = hovered == n.id;
        if (isBuilding(i.kind)) buildings.queue(p, i, n.id, selected, hover, W.reg.all_of<Remembered>(e), items, t);
        else units.queue(p, i, fc, n.id, selected, hover, items, t, v.B(int(std::floor(p.x)), int(std::floor(p.y)))); });
    effects.queueGhosts(items, t);
    effects.queueFx(items, t);
    effects.queueDrops(items, t);
    buildings.queuePendingSites(items);
    buildings.queuePlacementGhost(items);
    std::sort(items.begin(), items.end(), [](const DrawItem &a, const DrawItem &b)
              { return a.y < b.y; });
    for (auto &it : items)
        it.fn();

    terrain.drawFog(v, t);
    effects.drawLandClouds(v, t); // translucent clouds over discovered land
}
