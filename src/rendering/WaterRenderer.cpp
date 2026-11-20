/**
 * @file WaterRenderer.cpp
 * @brief Full-screen animated water (water.fs) with click / splash ripples; plain tiled texture without the shader.
 */
#include "rendering/WaterRenderer.hpp"

#include "client/App.hpp"
#include "rendering/Shaders.hpp"

void WaterRenderer::load()
{
    sh = loadShader("water.fs", ok);
}

namespace
{

// Newest ripples first, at most 8; unused slots get age -1. Expired ripples are dropped from the world.
void packRipples(std::vector<Ripple> &ripples, float rp[8][3])
{
    for (int i = 0; i < 8; i++)
    {
        rp[i][0] = rp[i][1] = 0;
        rp[i][2] = -1;
    }

    std::erase_if(ripples, [&](const Ripple &r) { return GetTime() - r.t > 2.2; });
    for (size_t i = 0; i < ripples.size() && i < 8; i++)
    {
        auto &r = ripples[ripples.size() - 1 - i];
        rp[i][0] = r.x;
        rp[i][1] = r.y;
        rp[i][2] = float(GetTime() - r.t);
    }
}

} // namespace

void WaterRenderer::draw(float t)
{
    const Camera2D &cam = app.cam;
    float sw = float(::GetScreenWidth()), sh2 = float(::GetScreenHeight());
    Texture2D wt = tex("Terrain/Tileset/Water Background color.png");

    if (ok)
    {
        float res2[2] = {sw, sh2}, tgt[2] = {cam.target.x, cam.target.y}, z = cam.zoom;
        float rp[8][3];
        packRipples(app.world.ripples, rp);

        setU(sh, "resolution", res2, SHADER_UNIFORM_VEC2);
        setU(sh, "camTarget", tgt, SHADER_UNIFORM_VEC2);
        setU(sh, "zoom", &z, SHADER_UNIFORM_FLOAT);
        setU(sh, "time", &t, SHADER_UNIFORM_FLOAT);
        setU(sh, "ripples", rp, SHADER_UNIFORM_VEC3, 8);
        BeginShaderMode(sh);
    }
    DrawTexturePro(wt, {0, 0, 64, 64}, {0, 0, sw, sh2}, {0, 0}, 0, WHITE);
    if (ok)
        EndShaderMode();
}
