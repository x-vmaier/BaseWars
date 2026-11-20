/**
 * @file Text.hpp
 * @brief Text drawing and measuring with the game font (Noto Sans), plus small text helpers.
 */
#pragma once
#include <raylib.h>

#include <string>
#include <vector>

// All game text goes through these instead of raylib's DrawText / MeasureText (bitmap default font, 10 px).
// `size` is the pixel height of the em square. Fonts are rasterised per size (crisp at any UI scale) on first use;
// without the font file everything falls back to raylib's default font.
enum class TextWeight
{
    Regular,
    Bold
};

void drawText(const char *s, int x, int y, int size, Color c, TextWeight w = TextWeight::Regular);
int measureText(const char *s, int size, TextWeight w = TextWeight::Regular);
inline void drawText(const std::string &s, int x, int y, int size, Color c, TextWeight w = TextWeight::Regular)
{
    drawText(s.c_str(), x, y, size, c, w);
}
inline int measureText(const std::string &s, int size, TextWeight w = TextWeight::Regular)
{
    return measureText(s.c_str(), size, w);
}
int textHeight(int size); // line height drawText uses for `size`, for vertical centring
void unloadFonts();       // before CloseWindow

void textShadow(const std::string &s, int x, int y, int sz, Color c);      // text with a soft drop shadow
std::vector<std::string> wrapText(const std::string &s, int maxW, int fs); // greedy word wrap to a pixel width
