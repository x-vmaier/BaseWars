/**
 * @file Settings.hpp
 * @brief Persistent client settings (settings.txt next to the executable): last server, UI scale, window
 * mode.
 */
#pragma once
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

struct Settings
{
    std::string lastServer = "127.0.0.1:7777";
    float uiScale = 1.f;      // 0.6 .. 2.0, scales every menu and HUD element
    bool hints = true;        // show advice hints (ui/Hints)
    float masterVolume = 1.f; // 0..1, all sound
    float sfxVolume = 1.f;    // 0..1, effects on top of master
    int windowMode = 0;       // 0 = windowed, 1 = fullscreen, 2 = borderless windowed
    static constexpr int WINDOW_MODES = 3;
    static const char *modeName(int m) { return m == 1 ? "Fullscreen" : (m == 2 ? "Borderless windowed" : "Windowed"); }

    static std::string path(const std::string &dir) { return dir + "settings.txt"; }
    void load(const std::string &dir)
    {
        std::ifstream f(path(dir));
        std::string ln;
        while (f && std::getline(f, ln))
        {
            auto p = ln.find('=');
            if (p == std::string::npos)
                continue;

            std::string k = ln.substr(0, p), v = ln.substr(p + 1);
            while (!v.empty() && (v.back() == '\r' || v.back() == ' '))
                v.pop_back();

            if (k == "last_server")
                lastServer = v;
            else if (k == "ui_scale")
                uiScale = std::clamp(float(std::atof(v.c_str())), 0.6f, 2.0f);
            else if (k == "hints")
                hints = v != "0";
            else if (k == "master_volume")
                masterVolume = std::clamp(float(std::atof(v.c_str())), 0.f, 1.f);
            else if (k == "sfx_volume")
                sfxVolume = std::clamp(float(std::atof(v.c_str())), 0.f, 1.f);
            else if (k == "window_mode")
                windowMode = std::clamp(std::atoi(v.c_str()), 0, WINDOW_MODES - 1);
        }
    }
    bool save(const std::string &dir) const
    {
        std::ofstream f(path(dir));
        if (!f)
            return false;
        f << "last_server=" << lastServer << "\nui_scale=" << uiScale << "\nwindow_mode=" << windowMode
          << "\nhints=" << (hints ? 1 : 0) << "\n";
        return true;
    }
};
