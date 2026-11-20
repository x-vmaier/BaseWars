/**
 * @file Text.cpp
 * @brief Game font cache (Noto Sans, OFL, assets/fonts) and text helpers.
 */
#include "rendering/Text.hpp"

#include "rendering/Assets.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace
{
constexpr int MIN_SIZE = 15; // below this glyphs turn to mush
constexpr int MAX_SIZE = 96;
constexpr float SIZE_BOOST =
    1.15f; // the UI was laid out for a 10 px bitmap font; Noto Sans reads smaller at equal size

// drawText and measureText share this, so every layout that measures stays consistent.
int effectiveSize(int size)
{
    return std::clamp(int(float(size) * SIZE_BOOST + .5f), MIN_SIZE, MAX_SIZE);
}

struct FontCache
{
    std::unordered_map<int, Font> byKey; // size * 2 + bold
    unsigned char *file[2] = {nullptr, nullptr};
    int fileSize[2] = {0, 0};
    bool failed[2] = {false, false};
    std::vector<int> glyphs;

    FontCache()
    {
        for (int c = 32; c <= 126; c++)
            glyphs.push_back(c);
        for (int c = 160; c <= 255; c++)
            glyphs.push_back(c); // Latin-1: names typed by players
        for (int c : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2192})
            glyphs.push_back(c);
    }

    // Font for a clamped size and weight, or the raylib default when the file is missing.
    Font get(int size, TextWeight w)
    {
        int wi = w == TextWeight::Bold ? 1 : 0, key = size * 2 + wi;
        auto it = byKey.find(key);
        if (it != byKey.end())
            return it->second;

        if (!file[wi] && !failed[wi])
        {
            file[wi] = LoadFileData(asset(wi ? "fonts/NotoSans-Bold.ttf" : "fonts/NotoSans-Regular.ttf").c_str(),
                                    &fileSize[wi]);
            failed[wi] = file[wi] == nullptr;
            if (failed[wi])
                TraceLog(LOG_WARNING, "game font missing: falling back to the raylib default font");
        }

        Font f = GetFontDefault();
        if (file[wi])
        {
            Font loaded = LoadFontFromMemory(".ttf", file[wi], fileSize[wi], size, glyphs.data(), int(glyphs.size()));
            if (loaded.texture.id != 0)
            {
                SetTextureFilter(loaded.texture, TEXTURE_FILTER_BILINEAR);
                f = loaded;
            }
        }
        byKey[key] = f;
        return f;
    }

    void unload()
    {
        for (auto &kv : byKey)
            if (kv.second.texture.id != GetFontDefault().texture.id)
                UnloadFont(kv.second);
        byKey.clear();
        for (int i = 0; i < 2; i++)
        {
            if (file[i])
                UnloadFileData(file[i]);
            file[i] = nullptr;
            failed[i] = false;
        }
    }
};

FontCache &fonts()
{
    static FontCache c;
    return c;
}

// The default font needs raylib's size / 10 letter spacing, TTF fonts none.
float spacingFor(const Font &f, int size)
{
    return f.texture.id == GetFontDefault().texture.id ? float(size) / 10.f : 0.f;
}
} // namespace

void drawText(const char *s, int x, int y, int size, Color c, TextWeight w)
{
    size = effectiveSize(size);
    Font f = fonts().get(size, w);
    DrawTextEx(f, s, {float(x), float(y)}, float(size), spacingFor(f, size), c);
}

int measureText(const char *s, int size, TextWeight w)
{
    size = effectiveSize(size);
    Font f = fonts().get(size, w);
    return int(MeasureTextEx(f, s, float(size), spacingFor(f, size)).x + .5f);
}

int textHeight(int size)
{
    return effectiveSize(size);
}

void unloadFonts()
{
    fonts().unload();
}

void textShadow(const std::string &s, int x, int y, int sz, Color c)
{
    drawText(s, x + 1, y + 1, sz, rgb(0, 0, 0, c.a / 2 + 40));
    drawText(s, x, y, sz, c);
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
