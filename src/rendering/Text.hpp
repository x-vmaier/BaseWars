/**
 * @file Text.hpp
 * @brief Text drawing and measuring with the game font (Noto Sans), plus small text helpers.
 */
#pragma once
#include <raylib.h>

#include <string>
#include <vector>

// All game text goes through these instead of raylib's DrawText / MeasureText (bitmap default font, 10 px).
// `size` is the nominal pixel size (the layouts were written for a 10 px font; see effectiveSize in Text.cpp).
// Faces: Body (Noto Sans) for running text, Bold for emphasis, Pixel (Pixelify Sans) for titles and coordinates.
// Each face is sampled once at high resolution (LoadFontEx) and scaled with trilinear filtering; without the font
// files everything falls back to raylib's default font.
enum class TextFace
{
    Body,
    Bold,
    Pixel
};

void drawText(const char *s, int x, int y, int size, Color c, TextFace w = TextFace::Body);
int measureText(const char *s, int size, TextFace w = TextFace::Body);
inline void drawText(const std::string &s, int x, int y, int size, Color c, TextFace w = TextFace::Body)
{
    drawText(s.c_str(), x, y, size, c, w);
}
inline int measureText(const std::string &s, int size, TextFace w = TextFace::Body)
{
    return measureText(s.c_str(), size, w);
}
int textHeight(int size, TextFace f = TextFace::Body); // exact rendered line height (MeasureTextEx), for centring
void unloadFonts();                                    // before CloseWindow

void textShadow(const std::string &s, int x, int y, int sz, Color c,
                TextFace f = TextFace::Body);                              // text with a soft drop shadow
std::vector<std::string> wrapText(const std::string &s, int maxW, int fs); // greedy word wrap to a pixel width
