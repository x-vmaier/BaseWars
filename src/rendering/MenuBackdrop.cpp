#include "rendering/MenuBackdrop.hpp"

#include "client/App.hpp"
#include "rendering/Sprites.hpp"

#include <cmath>
#include <string>

void MenuBackdrop::draw(float t)
{
    Camera2D &cam = app.cam;
    cam.target = {std::sin(t * .05f) * 200.f + 640.f, std::cos(t * .04f) * 120.f + 360.f};
    cam.offset = {float(::GetScreenWidth()) / 2, float(::GetScreenHeight()) / 2};
    cam.zoom = 1.f;
    app.water.draw(t);
    BeginMode2D(cam);
    Texture2D grass = tex("Terrain/Tileset/Tilemap_color1.png"), foam = tex("Terrain/Tileset/Water Foam.png");
    auto patch = [&](int x0, int y0, int w, int h)
    {
        for (int y = y0 - 1; y <= y0 + h; y++)
            for (int x = x0 - 1; x <= x0 + w; x++)
            {
                int fr = int(t * 10.f + float(hash2(x, y, 4) % 16)) % 16;
                DrawTexturePro(foam, {float(fr * 192), 0, 192, 192},
                               {float(x) * TILE - 64, float(y) * TILE - 64, 192, 192}, {0, 0}, 0, WHITE);
            }
        for (int y = y0; y < y0 + h; y++)
            for (int x = x0; x < x0 + w; x++)
            {
                int c = x == x0 ? 0 : (x == x0 + w - 1 ? 2 : 1), r = y == y0 ? 0 : (y == y0 + h - 1 ? 2 : 1);
                DrawTexturePro(grass, {float(c * 64), float(r * 64), 64, 64},
                               {float(x) * TILE, float(y) * TILE, TILE, TILE}, {0, 0}, 0, WHITE);
            }
    };
    patch(-2, 7, 8, 5);
    patch(15, 0, 7, 5);
    patch(-3, -2, 5, 4);
    patch(15, 8, 6, 4);
    auto sprite = [&](const std::string &file, float cx, float bottom, float w, float h, float scale, float ab)
    {
        DrawTexturePro(tex(file), {0, 0, w, h}, {cx - w * scale / 2, bottom - ab * scale, w * scale, h * scale}, {0, 0},
                       0, WHITE);
    };
    sprite("Buildings/Blue Buildings/Castle.png", 1.5f * TILE, 11.6f * TILE, 320, 256, .8f, 249);
    sprite("Buildings/Red Buildings/Archery.png", 18.5f * TILE, 4.6f * TILE, 192, 256, 1.f, 240);
    sprite("Buildings/Yellow Buildings/House1.png", -.5f * TILE, 1.4f * TILE, 128, 192, .9f, 173);
    sprite("Buildings/Purple Buildings/Tower.png", 18.f * TILE, 11.4f * TILE, 128, 256, .8f, 230);
    auto unit = [&](const std::string &rel, int fw, int frames, float fps, float x, float y, bool flip, float phase)
    {
        Anim a = mkAnim(rel, fw, fw, fps, float(fw) / 2, fw == 320 ? 198.f : 135.f);
        drawAnim(a, int(std::fmod(t * fps + phase, float(frames))) % frames, x, y, flip, 1.f);
    };
    unit("Units/Blue Units/Warrior/Warrior_Idle.png", 192, 8, 7, 0.2f * TILE, 11.9f * TILE, false, 0);
    unit("Units/Blue Units/Archer/Archer_Idle.png", 192, 6, 7, 3.4f * TILE, 11.7f * TILE, true, 2);
    unit("Units/Blue Units/Pawn/Pawn_Idle.png", 192, 8, 7, 2.2f * TILE, 12.1f * TILE, false, 3);
    unit("Units/Red Units/Lancer/Lancer_Idle.png", 320, 12, 9, 16.6f * TILE, 4.9f * TILE, false, 1);
    unit("Units/Red Units/Monk/Idle.png", 192, 6, 7, 20.3f * TILE, 4.7f * TILE, true, 4);
    unit("Units/Purple Units/Warrior/Warrior_Idle.png", 192, 8, 7, 16.f * TILE, 11.6f * TILE, false, 5);
    for (int i = 0; i < 4; i++)
    { // sheep wandering on the top-left islet
        Anim a = mkAnim("Terrain/Resources/Meat/Sheep/Sheep_Grass.png", 128, 128, 7, 64, 84);
        drawAnim(a, int(t * 7.f + float(i) * 3) % a.n, (-2.4f + float(i) * .9f) * TILE,
                 (0.2f + float(i % 2) * .6f) * TILE, i % 2, 1.f);
    }
    EndMode2D();
    for (int i = 0; i < 7; i++)
    { // drifting clouds
        Texture2D c = tex(std::string("Terrain/Decorations/Clouds/Clouds_0") + char('1' + i % 8) + ".png");
        float x = std::fmod(float(i) * 311.f + t * (14.f + float(i) * 3.f), float(::GetScreenWidth()) + 700.f) - 600.f;
        DrawTexturePro(c, {0, 0, 576, 256}, {x, 30.f + float((i * 97) % 520), 576, 256}, {0, 0}, 0,
                       rgb(255, 255, 255, 120));
    }
    DrawRectangle(0, 0, ::GetScreenWidth(), ::GetScreenHeight(), rgb(10, 20, 40, 60));
}
