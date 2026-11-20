/**
 * @file TerrainRenderer.cpp
 * @brief Terrain: shore foam, autotiled ground, cliffs, fog dimming and clouds over unexplored land.
 */
#include "rendering/TerrainRenderer.hpp"

#include "client/App.hpp"

#include <cmath>
#include <string>

int TerrainView::B(int x, int y) const
{
    return x < wx0 || y < wy0 || x > wx1 || y > wy1 ? int(INVALID_BIOME) : int(bio[size_t((y - wy0) * bw + (x - wx0))]);
}
bool TerrainView::E(int x, int y) const
{
    return x < wx0 || y < wy0 || x > wx1 || y > wy1 ? false : exp[size_t((y - wy0) * bw + (x - wx0))] != 0;
}

namespace
{

size_t cellIndex(const TerrainView &v, int x, int y)
{
    return size_t((y - v.wy0) * v.bw + (x - v.wx0));
}

void sampleWindow(const ClientWorld &W, TerrainView &v)
{
    size_t n = size_t(v.bw) * size_t(v.bh);
    v.bio.assign(n, INVALID_BIOME);
    v.exp.assign(n, 0);
    v.vnow.assign(n, 0);

    for (int y = v.wy0; y <= v.wy1; y++)
        for (int x = v.wx0; x <= v.wx1; x++)
        {
            size_t k = cellIndex(v, x, y);
            v.bio[k] = uint8_t(W.biomeAt(x, y));
            v.exp[k] = W.exploredAt(x, y);
        }
}

// Currently visible tiles: everything within vision range of an allied, finished entity.
void markVisible(const ClientWorld &W, TerrainView &v)
{
    W.reg.view<Info, Goal>().each([&](const Info &i, const Goal &g)
    {
        if (!W.alliedWith(i.owner) || (isBuilding(i.kind) && i.prog < 100))
            return;

        int r = KINFO[i.kind].vision, cx = int(std::floor(g.x)), cy = int(std::floor(g.y));
        for (int y = std::max(cy - r, v.wy0); y <= std::min(cy + r, v.wy1); y++)
            for (int x = std::max(cx - r, v.wx0); x <= std::min(cx + r, v.wx1); x++)
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r + r)
                    v.vnow[cellIndex(v, x, y)] = 1;
    });
}

bool isLand(const TerrainView &v, int x, int y)
{
    int b = v.B(x, y);
    return b != B_Ocean && b != INVALID_BIOME;
}

bool isHigh(const TerrainView &v, int x, int y)
{
    return v.B(x, y) == B_Highlands;
}

Color biomeTint(int b)
{
    return b == B_Desert ? rgb(255, 232, 170) : (b == B_Snow ? rgb(236, 246, 255) : WHITE);
}

// Picks the 64px edge/corner/centre cell of a tileset by which of the four neighbours are members.
template <typename Member> void autoTile(Texture2D ts, int x, int y, int colOff, Member member, Color tn)
{
    bool U = member(x, y - 1), D = member(x, y + 1), L = member(x - 1, y), R = member(x + 1, y);
    int c, r;
    if (!U && !D)
    {
        r = 3;
        c = (!L && !R) ? 3 : (!L ? 0 : (!R ? 2 : 1));
    }
    else if (!L && !R)
    {
        c = 3;
        r = !U ? 0 : (!D ? 2 : 1);
    }
    else
    {
        c = !L ? 0 : (!R ? 2 : 1);
        r = !U ? 0 : (!D ? 2 : 1);
    }

    DrawTexturePro(ts, {float((c + colOff) * 64), float(r * 64), 64, 64},
                   {float(x) * TILE, float(y) * TILE, TILE, TILE}, {0, 0}, 0, tn);
}

bool touchesOcean(const TerrainView &v, int x, int y)
{
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (v.B(x + dx, y + dy) == B_Ocean)
                return true;
    return false;
}

void drawShoreFoam(const TerrainView &v, float t)
{
    Texture2D foam = tex("Terrain/Tileset/Water Foam.png");

    for (int y = v.y0; y <= v.y1; y++)
        for (int x = v.x0; x <= v.x1; x++)
        {
            if (!isLand(v, x, y) || !v.E(x, y) || !touchesOcean(v, x, y))
                continue;

            int fr = int(t * 10.f + float(hash2(x, y, 4) % 16)) % 16;
            DrawTexturePro(foam, {float(fr * 192), 0, 192, 192}, {float(x) * TILE - 64, float(y) * TILE - 64, 192, 192},
                           {0, 0}, 0, WHITE);
        }
}

// Flat grass on every explored land tile, with snow and sand overlays.
void drawFlatGround(const TerrainView &v)
{
    static const char *tileset[B_Count] = {"", "1", "3", "2", "4", "2", "5"};
    auto land = [&](int x, int y) { return isLand(v, x, y); };

    for (int y = v.y0; y <= v.y1; y++)
        for (int x = v.x0; x <= v.x1; x++)
        {
            int b = v.B(x, y);
            if (!isLand(v, x, y) || !v.E(x, y))
                continue;

            autoTile(tex(std::string("Terrain/Tileset/Tilemap_color") + tileset[b] + ".png"), x, y, 0, land,
                     biomeTint(b));
            if (b == B_Snow)
                DrawRectangle(x * 64, y * 64, 64, 64, rgb(240, 248, 255, 215));
            else if (b == B_Desert)
                DrawRectangle(x * 64, y * 64, 64, 64, rgb(232, 200, 120, 120));
        }
}

// A non-highlands tile directly below a highlands tile shows the cliff face.
bool cliffBase(const TerrainView &v, int x, int y)
{
    int b = v.B(x, y);
    return b != B_Highlands && b != INVALID_BIOME && v.B(x, y - 1) == B_Highlands;
}

void drawCliffFace(const TerrainView &v, int x, int y, int b)
{
    auto land = [&](int px, int py) { return isLand(v, px, py); };
    bool l = cliffBase(v, x - 1, y), r = cliffBase(v, x + 1, y);
    int c = (!l && !r) ? 8 : (!l ? 5 : (!r ? 7 : 6)), row = b == B_Ocean ? 5 : 4;

    if (b != B_Ocean)
        autoTile(tex("Terrain/Tileset/Tilemap_color1.png"), x, y, 0, land, WHITE);
    DrawTexturePro(tex("Terrain/Tileset/Tilemap_color2.png"), {float(c * 64), float(row * 64), 64, 64},
                   {float(x) * TILE, float(y) * TILE, TILE, TILE}, {0, 0}, 0, WHITE);
}

void drawPlateausAndCliffs(const TerrainView &v)
{
    auto high = [&](int x, int y) { return isHigh(v, x, y); };

    for (int y = v.y0; y <= v.y1; y++)
        for (int x = v.x0; x <= v.x1; x++)
        {
            if (!v.E(x, y))
                continue;

            if (high(x, y))
                autoTile(tex("Terrain/Tileset/Tilemap_color2.png"), x, y, 5, high, WHITE);

            int b = v.B(x, y);
            if (cliffBase(v, x, y))
                drawCliffFace(v, x, y, b);
        }
}

void drawUnexplored(const TerrainView &v)
{
    for (int y = v.y0; y <= v.y1; y++)
        for (int x = v.x0; x <= v.x1; x++)
            if (!v.E(x, y))
                DrawRectangle(x * 64, y * 64, 64, 64, rgb(124, 154, 182));
}

} // namespace

TerrainView TerrainRenderer::view(Vector2 tl, Vector2 br) const
{
    TerrainView v;
    v.x0 = int(std::floor(tl.x / TILE)) - 1;
    v.y0 = int(std::floor(tl.y / TILE)) - 1;
    v.x1 = int(std::floor(br.x / TILE)) + 1;
    v.y1 = int(std::floor(br.y / TILE)) + 1;
    v.wx0 = v.x0 - 2;
    v.wy0 = v.y0 - 3;
    v.wx1 = v.x1 + 2;
    v.wy1 = v.y1 + 5;
    v.bw = v.wx1 - v.wx0 + 1;
    v.bh = v.wy1 - v.wy0 + 1;

    sampleWindow(app.world, v);
    markVisible(app.world, v);
    return v;
}

void TerrainRenderer::drawGround(const TerrainView &v, float t)
{
    drawShoreFoam(v, t);
    drawFlatGround(v);
    drawPlateausAndCliffs(v);
    drawUnexplored(v);
}

// Dims remembered land, then drifts clouds over the unknown.
void TerrainRenderer::drawFog(const TerrainView &v, float t)
{
    const int x0 = v.x0, y0 = v.y0, x1 = v.x1, y1 = v.y1, wx0 = v.wx0, wy0 = v.wy0, wx1 = v.wx1, wy1 = v.wy1;
    auto E = [&](int x, int y) { return v.E(x, y); };
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
        {
            size_t k = size_t((y - wy0) * v.bw + (x - wx0));
            if (v.exp[k] && !v.vnow[k])
                DrawRectangle(x * 64, y * 64, 64, 64, rgb(10, 14, 30, 85));
        }
    for (int gy = int(std::floor(float(y0) / 5.f)) - 1; gy <= y1 / 5 + 1; gy++)
        for (int gx = int(std::floor(float(x0) / 5.f)) - 2; gx <= x1 / 5 + 2; gx++)
        {
            uint32_t h = hash2(gx, gy, 4242);
            int cx = gx * 5 + int(h % 5), cy = gy * 5 + int((h >> 3) % 5);
            if (E(cx, cy) && E(cx + 1, cy + 1))
                continue;
            if (cx >= wx0 && cx <= wx1 && cy >= wy0 && cy <= wy1 && E(cx, cy))
                continue;
            if (E(cx, cy))
                continue;
            Texture2D ct = tex(std::string("Terrain/Decorations/Clouds/Clouds_0") + char('1' + (h >> 8) % 8) + ".png");
            float drift = std::sin(t * .12f + float(h % 100)) * 48.f;
            DrawTexturePro(ct, {0, 0, 576, 256}, {float(cx) * TILE - 220 + drift, float(cy) * TILE - 110, 576, 256},
                           {0, 0}, 0, rgb(255, 255, 255, 235));
        }
}
