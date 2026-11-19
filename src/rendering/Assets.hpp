/**
 * @file Assets.hpp
 * @brief Asset loading: asset directory lookup, texture/animation cache, a tiny JSON parser and Tiled (.tmj) UI maps.
 */
#pragma once
#include <raylib.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

inline Color rgb(int r, int g, int b, int a = 255)
{
    return Color{(unsigned char)std::clamp(r, 0, 255), (unsigned char)std::clamp(g, 0, 255),
                 (unsigned char)std::clamp(b, 0, 255), (unsigned char)std::clamp(a, 0, 255)};
}

// assets
void findAssets();                         // locates the assets/ directory next to the executable or in a parent
std::string asset(const std::string &rel); // absolute path of an asset
Texture2D tex(const std::string &rel);     // cached texture (point filtered); id 0 when missing

struct Anim
{
    Texture2D t;
    int fw, fh, n;
    float fps, footX, footY;
};

Anim mkAnim(const std::string &rel, int fw, int fh, float fps, float fx, float fy);
Rectangle frameRect(const Anim &a, int f);

// Tiled maps used as UI backgrounds
struct TMap
{
    bool ok = false;
    int w = 0, h = 0, tw = 64, th = 64, padding = 0;
    float scale = 1;
    std::vector<uint32_t> data;
    struct TS
    {
        int first, cols, tw, th;
        Texture2D t;
    };
    std::vector<TS> sets;
    Vector2 size() const { return {float(w * tw) * scale, float(h * th) * scale}; }
};

TMap loadTMap(const std::string &name);
// Draws the whole map stretched into `dst` (natural size when dst matches size()).
void drawTMap(const TMap &m, Rectangle dst, Color tint = WHITE);
