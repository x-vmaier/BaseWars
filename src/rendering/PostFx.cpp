#include "rendering/PostFx.hpp"

#include "client/App.hpp"
#include "rendering/Shaders.hpp"

#include <cmath>
#include <ctime>
#include <vector>

void PostFx::load()
{
    shPost = loadShader("post.fs", okPost);
    bool b1 = false, b2 = false;
    shBright = loadShader("bloom_bright.fs", b1);
    shBlur = loadShader("bloom_blur.fs", b2);
    okBloom = b1 && b2 && okPost;
}

void PostFx::ensureTargets()
{
    int w = ::GetScreenWidth(), h = ::GetScreenHeight();
    if (rt.id == 0 || rt.texture.width != w || rt.texture.height != h)
    {
        if (rt.id)
            UnloadRenderTexture(rt);
        rt = LoadRenderTexture(w, h);
        SetTextureFilter(rt.texture, TEXTURE_FILTER_POINT);
    }
    int bw = std::max(2, w / 2), bh = std::max(2, h / 2);
    if (bloomA.id == 0 || bloomA.texture.width != bw || bloomA.texture.height != bh)
    {
        if (bloomA.id)
        {
            UnloadRenderTexture(bloomA);
            UnloadRenderTexture(bloomB);
        }
        bloomA = LoadRenderTexture(bw, bh);
        bloomB = LoadRenderTexture(bw, bh);
        SetTextureFilter(bloomA.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(bloomB.texture, TEXTURE_FILTER_BILINEAR);
    }
}

void PostFx::beginScene()
{
    BeginTextureMode(rt);
    ClearBackground(rgb(20, 22, 30));
}
void PostFx::endScene()
{
    EndTextureMode();
    runBloom();
}
void PostFx::present(float t)
{
    if (okPost)
    {
        uniforms(t);
        BeginShaderMode(shPost);
    }
    DrawTextureRec(rt.texture, {0, 0, float(rt.texture.width), -float(rt.texture.height)}, {0, 0}, WHITE);
    if (okPost)
        EndShaderMode();
}

// Bloom: bright-pass into a half-res target, then a separable gaussian (H then V).
void PostFx::runBloom()
{
    if (!okBloom)
        return;
    float bw = float(bloomA.texture.width), bh = float(bloomA.texture.height);
    float th = .78f;
    setU(shBright, "threshold", &th, SHADER_UNIFORM_FLOAT);
    BeginTextureMode(bloomA);
    ClearBackground(BLACK);
    BeginShaderMode(shBright);
    DrawTexturePro(rt.texture, {0, 0, float(rt.texture.width), -float(rt.texture.height)}, {0, 0, bw, bh}, {0, 0}, 0, WHITE);
    EndShaderMode();
    EndTextureMode();
    float dh[2] = {1.f / bw, 0}, dv[2] = {0, 1.f / bh};
    setU(shBlur, "direction", dh, SHADER_UNIFORM_VEC2);
    BeginTextureMode(bloomB);
    ClearBackground(BLACK);
    BeginShaderMode(shBlur);
    DrawTexturePro(bloomA.texture, {0, 0, bw, -bh}, {0, 0, bw, bh}, {0, 0}, 0, WHITE);
    EndShaderMode();
    EndTextureMode();
    setU(shBlur, "direction", dv, SHADER_UNIFORM_VEC2);
    BeginTextureMode(bloomA);
    ClearBackground(BLACK);
    BeginShaderMode(shBlur);
    DrawTexturePro(bloomB.texture, {0, 0, bw, -bh}, {0, 0, bw, bh}, {0, 0}, 0, WHITE);
    EndShaderMode();
    EndTextureMode();
}

void PostFx::uniforms(float t)
{
    const Camera2D &cam = app.cam;
    const ClientWorld &W = app.world;
    float sw = float(::GetScreenWidth()), sh = float(::GetScreenHeight());
    int ctx2 = int(std::floor(cam.target.x / TILE)), cty = int(std::floor(cam.target.y / TILE));
    bool snow = W.biomeAt(ctx2, cty) == B_Snow;
    bool nearFire = false;
    std::vector<float> lights;
    int lc = 0;
    W.reg.view<Info, Goal>().each([&](const Info &i, const Goal &g)
                                  {
        if (!isBuilding(i.kind) || i.prog < 100 || !W.alliedWith(i.owner)) return;
        bool fire = i.kind == K_Campfire;
        if (!fire && i.kind != K_TownHall && i.kind != K_Outpost) return;
        if (fire && std::hypot(g.x - cam.target.x / TILE, g.y - cam.target.y / TILE) < WARMTH_RADIUS) nearFire = true;
        Vector2 s = GetWorldToScreen2D({g.x * TILE, g.y * TILE - (fire ? 20.f : 60.f)}, cam);
        if (lc < 16 && s.x > -300 && s.y > -300 && s.x < sw + 300 && s.y < sh + 300) { lights.push_back(s.x); lights.push_back(s.y); lights.push_back((fire ? 190.f : 150.f) * cam.zoom); lc++; } });
    float dt = GetFrameTime();
    float targetCold = snow && !nearFire ? 1.f : 0.f, targetWarm = nearFire && snow ? 1.f : (nearFire ? .45f : 0.f);
    cold += (targetCold - cold) * std::min(1.f, 1.5f * dt);
    warm += (targetWarm - warm) * std::min(1.f, 2.f * dt);
    shake = std::max(0.f, shake - 28.f * dt);
    damage = std::max(0.f, damage - .6f * dt);
    double ph = std::fmod(double(std::time(nullptr)), 480.0) / 480.0;
    float daylight = std::clamp(.5f + .75f * std::sin(float(ph * 6.28319 - 1.5708)), 0.f, 1.f);
    float blizz = cold * (.75f + .25f * std::sin(t * .7f));
    float res2[2] = {sw, sh};
    float lt[48] = {};
    for (size_t i = 0; i < lights.size(); i++)
        lt[i] = lights[i];
    setU(shPost, "resolution", res2, SHADER_UNIFORM_VEC2);
    setU(shPost, "time", &t, SHADER_UNIFORM_FLOAT);
    setU(shPost, "shake", &shake, SHADER_UNIFORM_FLOAT);
    setU(shPost, "damage", &damage, SHADER_UNIFORM_FLOAT);
    setU(shPost, "daylight", &daylight, SHADER_UNIFORM_FLOAT);
    setU(shPost, "cold", &cold, SHADER_UNIFORM_FLOAT);
    setU(shPost, "warmth", &warm, SHADER_UNIFORM_FLOAT);
    setU(shPost, "blizzard", &blizz, SHADER_UNIFORM_FLOAT);
    setU(shPost, "lights", lt, SHADER_UNIFORM_VEC3, 16);
    setU(shPost, "lightCount", &lc, SHADER_UNIFORM_INT);
    float bloomK = okBloom ? .55f : 0.f, expo = 1.05f;
    setU(shPost, "bloomStrength", &bloomK, SHADER_UNIFORM_FLOAT);
    setU(shPost, "exposure", &expo, SHADER_UNIFORM_FLOAT);
    int bl = GetShaderLocation(shPost, "bloomTex");
    if (bl >= 0 && okBloom)
        SetShaderValueTexture(shPost, bl, bloomA.texture);
}
