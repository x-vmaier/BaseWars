#pragma once
// Text helpers.
#include <raylib.h>

#include <string>
#include <vector>

void textShadow(const std::string& s, int x, int y, int sz, Color c);          // text with a soft drop shadow
std::vector<std::string> wrapText(const std::string& s, int maxW, int fs);      // greedy word wrap to a pixel width
