/**
 * @file Text.cpp
 * @brief Game fonts (Noto Sans body + bold, Press Start 2P for titles; SIL OFL, assets/fonts) and text helpers.
 */
#include "rendering/Text.hpp"

#include "rendering/Assets.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>
#include <sstream>
#include <vector>

namespace
{
constexpr int ATLAS_SIZE = 80; // glyph sampling resolution; drawing scales down from it
constexpr int MIN_SIZE = 16;   // below this glyphs turn to mush
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

struct FontCache
{
    Font font[3]{};
    bool tried[3]{};

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

    bool loaded(int i) const { return font[i].texture.id != 0; }

    // The face, loaded on first use; the raylib default font when its file is missing.
    const Font &get(TextFace f)
    {
        int i = int(f);
        if (!tried[i])
        {
            tried[i] = true;
            auto g = glyphs();
            font[i] = LoadFontEx(asset(FACES[i].path).c_str(), ATLAS_SIZE, g.data(), int(g.size()));
            if (font[i].texture.id == GetFontDefault().texture.id)
                font[i] = Font{}; // LoadFontEx hands back the default font on failure
            if (loaded(i))
            {
                GenTextureMipmaps(&font[i].texture);
                SetTextureFilter(font[i].texture, TEXTURE_FILTER_TRILINEAR);
            }
            else
                spdlog::warn("game font {} missing: falling back to the raylib default font", FACES[i].path);
        }
        return loaded(i) ? font[i] : fallback();
    }

    static const Font &fallback()
    {
        static Font f;
        f = GetFontDefault();
        return f;
    }

    void unload()
    {
        for (int i = 0; i < 3; i++)
        {
            if (loaded(i))
                UnloadFont(font[i]);
            font[i] = Font{};
            tried[i] = false;
        }
    }
};

FontCache &fonts()
{
    static FontCache c;
    return c;
}

float spacingFor(TextFace face, int size)
{
    return fonts().loaded(int(face)) ? FACES[int(face)].spacing * float(size) : float(size) / 10.f;
}
} // namespace

void drawText(const char *s, int x, int y, int size, Color c, TextFace face)
{
    size = effectiveSize(size, face);
    const Font &f = fonts().get(face);
    DrawTextEx(f, s, {float(x), float(y)}, float(size), spacingFor(face, size), c);
}

int measureText(const char *s, int size, TextFace face)
{
    size = effectiveSize(size, face);
    const Font &f = fonts().get(face);
    return int(MeasureTextEx(f, s, float(size), spacingFor(face, size)).x + .5f);
}

int textHeight(int size, TextFace face)
{
    size = effectiveSize(size, face);
    const Font &f = fonts().get(face);
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
