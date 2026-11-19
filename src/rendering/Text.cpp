#include "rendering/Text.hpp"

#include "rendering/Assets.hpp"

#include <sstream>

void textShadow(const std::string& s, int x, int y, int sz, Color c) {
    DrawText(s.c_str(), x + 1, y + 1, sz, rgb(0, 0, 0, c.a / 2 + 40));
    DrawText(s.c_str(), x, y, sz, c);
}
std::vector<std::string> wrapText(const std::string& s, int maxW, int fs) {
    std::vector<std::string> out;
    std::string cur, word;
    std::istringstream ss(s);
    while (ss >> word) {
        std::string t = cur.empty() ? word : cur + " " + word;
        if (MeasureText(t.c_str(), fs) > maxW && !cur.empty()) { out.push_back(cur); cur = word; }
        else cur = t;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
