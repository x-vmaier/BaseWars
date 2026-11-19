#include "rendering/Minimap.hpp"

#include "client/App.hpp"
#include "rendering/Palette.hpp"
#include "ui/UiScale.hpp"

#include <cmath>

void Minimap::load()
{
    mMini = loadTMap("minimap_frame");
    Image im = GenImageColor(MM, MM, BLACK);
    texture = LoadTextureFromImage(im);
    UnloadImage(im);
    pix.assign(size_t(MM * MM), BLACK);
}

Rectangle Minimap::frame() const
{
    Vector2 s = mMini.size();
    return {float(ui::screenW()) - s.x - 8, 8, s.x, s.y};
}
Rectangle Minimap::rect() const
{
    Rectangle f = frame();
    float in = 46.f * mMini.scale / .4f * 1.f;
    return {f.x + in, f.y + in, f.width - 2 * in, f.height - 2 * in};
}

void Minimap::update()
{
    if (GetTime() - lastMM < .3)
        return;
    lastMM = GetTime();
    const ClientWorld &W = app.world;
    int cx = int(std::floor(app.cam.target.x / TILE)) - MM / 2, cy = int(std::floor(app.cam.target.y / TILE)) - MM / 2;
    for (int j = 0; j < MM; j++)
        for (int i = 0; i < MM; i++)
        {
            int x = cx + i, y = cy + j;
            bool ex = W.exploredAt(x, y);
            Color c = ex ? biomeColor(W.biomeAt(x, y)) : rgb(16, 20, 30);
            // sparse tree dots: one sample in TREE_EVERY tiles (stable world hash), drawn before units / buildings so
            // those always paint over them
            if (ex && (uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u) % TREE_EVERY == 0)
            {
                uint8_t f = W.featVisible(x, y);
                if (f == F_Pine || f == F_Oak)
                    c = rgb(10, 40, 16);
            }
            pix[size_t(j * MM + i)] = c;
        }
    W.reg.view<Info, Goal>().each([&](const Info &in, const Goal &g)
                                  {
        int i = int(std::floor(g.x)) - cx, j = int(std::floor(g.y)) - cy;
        if (i < 1 || j < 1 || i >= MM - 1 || j >= MM - 1 || in.kind == K_Sheep) return;
        int sz = isBuilding(in.kind) ? 2 : 1;
        for (int a = 0; a < sz + 1; a++) for (int b = 0; b < sz + 1; b++) if (i + a < MM && j + b < MM) pix[size_t((j + b) * MM + i + a)] = teamColor(in.owner); });
    UpdateTexture(texture, pix.data());
}

void Minimap::draw()
{
    const Camera2D &cam = app.cam;
    Rectangle mf = frame(), mr = rect();
    drawTMap(mMini, mf);
    DrawRectangleRec(mr, rgb(18, 22, 30));
    if (texture.id)
        DrawTexturePro(texture, {0, 0, float(MM), float(MM)}, mr, {0, 0}, 0, WHITE);
    app.mapOverlay.draw(mr, {std::floor(cam.target.x / TILE) + .5f, std::floor(cam.target.y / TILE) + .5f}, float(MM) / mr.width, true);
    DrawRectangleLinesEx(mr, 2, rgb(60, 40, 26));
    Vector2 tl = GetScreenToWorld2D({0, 0}, cam), brr = GetScreenToWorld2D({float(::GetScreenWidth()), float(::GetScreenHeight())}, cam); // camera view
    float k = mr.width / float(MM) / TILE;
    Rectangle vr{mr.x + mr.width / 2 + (tl.x - cam.target.x) * k, mr.y + mr.height / 2 + (tl.y - cam.target.y) * k, (brr.x - tl.x) * k, (brr.y - tl.y) * k};
    float x0 = std::max(vr.x, mr.x), y0 = std::max(vr.y, mr.y), x1 = std::min(vr.x + vr.width, mr.x + mr.width), y1 = std::min(vr.y + vr.height, mr.y + mr.height);
    if (x1 > x0 && y1 > y0)
        DrawRectangleLinesEx({x0, y0, x1 - x0, y1 - y0}, 1, WHITE);
}
