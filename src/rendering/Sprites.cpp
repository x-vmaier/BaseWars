/**
 * @file Sprites.cpp
 * @brief Sprite drawing with the optional outline (hover) and dissolve (destroyed) shaders.
 */
#include "rendering/Sprites.hpp"

#include <algorithm>
#include <cmath>

#include "rendering/Shaders.hpp"

namespace
{
Shader shOutline{}, shDissolve{};
bool okOutline = false, okDissolve = false;
int locOut[5]{-1, -1, -1, -1, -1},
    locDis[4]{-1, -1, -1,
              -1}; // outline: uvRect/texelSize/outlineColor/thickness/time; dissolve: uvRect/texSize/progress/seed
} // namespace

void loadSpriteShaders()
{
    shOutline = loadShader("outline.fs", okOutline);
    shDissolve = loadShader("dissolve.fs", okDissolve);
    if (okOutline)
    {
        const char *n[5] = {"uvRect", "texelSize", "outlineColor", "thickness", "time"};
        for (int i = 0; i < 5; i++)
            locOut[i] = GetShaderLocation(shOutline, n[i]);
    }
    if (okDissolve)
    {
        const char *n[4] = {"uvRect", "texSize", "progress", "seed"};
        for (int i = 0; i < 4; i++)
            locDis[i] = GetShaderLocation(shDissolve, n[i]);
    }
}

void drawSprite(Texture2D t, Rectangle src, Rectangle dst, Color tint, int mode, Color oc, float prog, float sd)
{
    if (t.id == 0)
        return;
    bool shaded = (mode == 1 && okOutline) || (mode == 2 && okDissolve);
    if (!shaded)
    {
        DrawTexturePro(t, src, dst, {0, 0}, 0, tint);
        return;
    }
    Shader &sh = mode == 1 ? shOutline : shDissolve;
    float u0 = std::min(src.x, src.x + src.width) / float(t.width),
          u1 = std::max(src.x, src.x + src.width) / float(t.width);
    float v0 = std::min(src.y, src.y + src.height) / float(t.height),
          v1 = std::max(src.y, src.y + src.height) / float(t.height);
    float uv[4] = {u0, v0, u1, v1}, ts[2] = {1.f / float(t.width), 1.f / float(t.height)},
          tsz[2] = {float(t.width), float(t.height)};
    float col[4] = {oc.r / 255.f, oc.g / 255.f, oc.b / 255.f, oc.a / 255.f}, th = 2.5f, tm = float(GetTime());
    if (mode == 1)
    {
        SetShaderValue(sh, locOut[0], uv, SHADER_UNIFORM_VEC4);
        SetShaderValue(sh, locOut[1], ts, SHADER_UNIFORM_VEC2);
        SetShaderValue(sh, locOut[2], col, SHADER_UNIFORM_VEC4);
        SetShaderValue(sh, locOut[3], &th, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh, locOut[4], &tm, SHADER_UNIFORM_FLOAT);
    }
    else
    {
        SetShaderValue(sh, locDis[0], uv, SHADER_UNIFORM_VEC4);
        SetShaderValue(sh, locDis[1], tsz, SHADER_UNIFORM_VEC2);
        SetShaderValue(sh, locDis[2], &prog, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh, locDis[3], &sd, SHADER_UNIFORM_FLOAT);
    }
    BeginShaderMode(sh);
    DrawTexturePro(t, src, dst, {0, 0}, 0, tint);
    EndShaderMode();
}
void drawAnim(const Anim &a, int frame, float footWX, float footWY, bool flip, float scale, Color tint, int mode,
              Color oc, float prog, float sd)
{
    Rectangle src = frameRect(a, frame);
    if (flip)
        src.width = -src.width;
    float fx = flip ? float(a.fw) - a.footX : a.footX;
    Rectangle dst{footWX - fx * scale, footWY - a.footY * scale, float(a.fw) * scale, float(a.fh) * scale};
    drawSprite(a.t, src, dst, tint, mode, oc, prog, sd);
}
int frameFor(const Anim &a, double t, float phase)
{
    return int(std::fmod(t * double(a.fps) + double(phase), double(a.n))) % a.n;
}
