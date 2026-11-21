/**
 * @file Text.cpp
 * @brief Game fonts (Noto Sans body + bold, Press Start 2P for titles; SIL OFL, assets/fonts) and text helpers.
 */
#include "rendering/Text.hpp"

#include "rendering/Assets.hpp"
#include "ui/UiScale.hpp"

#include <algorithm>
#include <map>
#include <spdlog/spdlog.h>
#include <sstream>
#include <vector>

namespace
{
constexpr int MIN_SIZE = 16; // below this glyphs turn to mush
constexpr int MAX_SIZE = 160;
constexpr float PIXEL_SHRINK = .42f;
constexpr float SIZE_BOOST = 1.3f; // layouts were written for a 10 px bitmap font; real fonts read smaller

// drawText, measureText and textHeight share this, so every layout that measures stays consistent.
// The pixel face is about as wide as it is tall, so it is drawn smaller to take the same room as body text.
int effectiveSize(int size, TextFace face)
{
    if (face == TextFace::Pixel)
        return std::clamp(int(float(size) * SIZE_BOOST * PIXEL_SHRINK + .5f), 11, MAX_SIZE);
    return std::clamp(int(float(size) * SIZE_BOOST + .5f), MIN_SIZE, MAX_SIZE);
}

struct FaceFile
{
    const char *path;
    float spacing; // extra letter spacing in em (the pixel font breathes better with a little)
};
const FaceFile FACES[] = {
    {"fonts/NotoSans-Regular.ttf", 0.f},
    {"fonts/NotoSans-Bold.ttf", 0.f},
    {"fonts/PressStart2P.ttf", 0.f},
};

// Glyphs are rasterised at the size they are drawn on screen (logical size x UI scale), one atlas per face and pixel
// size, drawn 1:1 with bilinear filtering: a big atlas scaled down (even with mipmaps) looks blurry.
struct FontCache
{
    std::map<int, Font> font; // key = face * 1000 + pixel size; an empty Font = file missing
    bool warned[3]{};

    static std::vector<int> glyphs()
    {
        std::vector<int> g;
        for (int c = 32; c <= 126; c++)
            g.push_back(c);
        for (int c = 160; c <= 255; c++)
            g.push_back(c); // Latin-1: names typed by players
        for (int c : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2192})
            g.push_back(c);
        return g;
    }

    bool available(TextFace f, int px) { return get(f, px).texture.id != fallback().texture.id; }

    // The face at `px` pixels, loaded on first use; the raylib default font when its file is missing.
    const Font &get(TextFace f, int px)
    {
        int key = int(f) * 1000 + px;
        auto it = font.find(key);
        if (it == font.end())
        {
            if (font.size() > 64)
                unload(); // sizes pile up when the UI scale is dragged
            auto g = glyphs();
            Font ft = LoadFontEx(asset(FACES[int(f)].path).c_str(), px, g.data(), int(g.size()));
            if (ft.texture.id == GetFontDefault().texture.id)
                ft = Font{}; // LoadFontEx hands back the default font on failure
            if (ft.texture.id != 0)
                SetTextureFilter(ft.texture, TEXTURE_FILTER_BILINEAR);
            else if (!warned[int(f)])
            {
                warned[int(f)] = true;
                spdlog::warn("game font {} missing: falling back to the raylib default font", FACES[int(f)].path);
            }
            it = font.emplace(key, ft).first;
        }
        return it->second.texture.id != 0 ? it->second : fallback();
    }

    static const Font &fallback()
    {
        static Font f;
        f = GetFontDefault();
        return f;
    }

    void unload()
    {
        for (auto &[k, ft] : font)
            if (ft.texture.id != 0)
                UnloadFont(ft);
        font.clear();
    }
};

FontCache &fonts()
{
    static FontCache c;
    return c;
}

// Pixel size the glyphs are rasterised at for a logical text size.
int pixelSize(int size)
{
    return std::clamp(int(float(size) * ui::scale() + .5f), 8, 320);
}

float spacingFor(TextFace face, int size)
{
    return fonts().available(face, pixelSize(size)) ? FACES[int(face)].spacing * float(size) : float(size) / 10.f;
}
} // namespace

void drawText(const char *s, int x, int y, int size, Color c, TextFace face)
{
    size = effectiveSize(size, face);
    const Font &f = fonts().get(face, pixelSize(size));
    DrawTextEx(f, s, {float(x), float(y)}, float(size), spacingFor(face, size), c);
}

int measureText(const char *s, int size, TextFace face)
{
    size = effectiveSize(size, face);
    const Font &f = fonts().get(face, pixelSize(size));
    return int(MeasureTextEx(f, s, float(size), spacingFor(face, size)).x + .5f);
}

int textHeight(int size, TextFace face)
{
    size = effectiveSize(size, face);
    const Font &f = fonts().get(face, pixelSize(size));
    return int(MeasureTextEx(f, "Ag", float(size), 0).y + .5f);
}

void unloadFonts()
{
    fonts().unload();
}

void textShadow(const std::string &s, int x, int y, int sz, Color c, TextFace face)
{
    drawText(s, x + 1, y + 1, sz, rgb(0, 0, 0, c.a / 2 + 40), face);
    drawText(s, x, y, sz, c, face);
}

std::vector<std::string> wrapText(const std::string &s, int maxW, int fs)
{
    std::vector<std::string> out;
    std::string cur, word;
    std::istringstream ss(s);
    while (ss >> word)
    {
        std::string t = cur.empty() ? word : cur + " " + word;
        if (measureText(t, fs) > maxW && !cur.empty())
        {
            out.push_back(cur);
            cur = word;
        }
        else
            cur = t;
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}
