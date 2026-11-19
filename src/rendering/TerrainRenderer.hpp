#pragma once
// Terrain: shore foam, autotiled ground / plateaus / cliff faces, unexplored tiles, fog dimming and the clouds over
// unexplored land. Works on a TerrainView: the visible tile window plus a margin, with biome / explored / visible
// samples cached once per frame.
#include <raylib.h>

#include <cstdint>
#include <vector>

class App;

struct TerrainView {
    int x0, y0, x1, y1;                 // visible tiles
    int wx0, wy0, wx1, wy1, bw, bh;     // sampled window (visible + margin)
    std::vector<uint8_t> bio, exp, vnow;
    int B(int x, int y) const;          // biome (INVALID_BIOME outside the window)
    bool E(int x, int y) const;         // explored
};

class TerrainRenderer {
public:
    explicit TerrainRenderer(App& app) : app(app) {}
    TerrainView view(Vector2 tl, Vector2 br) const;   // world-space corners of the screen
    void drawGround(const TerrainView& v, float t);   // under all sprites
    void drawFog(const TerrainView& v, float t);      // over all sprites

private:
    App& app;
};
