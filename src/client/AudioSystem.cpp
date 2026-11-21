/**
 * @file AudioSystem.cpp
 * @brief Client sound effects: one table of cues (sample, volume, pitch range, cooldown), positional falloff.
 */
#include "client/AudioSystem.hpp"

#include "rendering/Assets.hpp"

#include <raylib.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <random>

namespace audio
{
namespace
{
struct CueDef
{
    const char *name, *file;
    float volume, pitchMin, pitchMax;
    double cooldown; // seconds between two plays of this cue
};

// The one place to add a sound: cue -> sample, volume, pitch variance, cooldown.
constexpr std::array<CueDef, C_Count> TABLE{{
    {"ui_click", "ui_click", .55f, .96f, 1.04f, .03},
    {"ui_hover", "ui_hover", .25f, .97f, 1.03f, .05},
    {"chop", "chop", .55f, .85f, 1.15f, .08},
    {"mine", "mine", .45f, .9f, 1.1f, .08},
    {"build", "build", .7f, .9f, 1.05f, .15},
    {"hit", "hit", .5f, .85f, 1.15f, .06},
    {"horn", "horn", .9f, 1.f, 1.f, 3.0},
    {"victory", "victory", .8f, 1.f, 1.f, 1.0},
    {"coin", "coin", .5f, .95f, 1.12f, .06},
    {"ship_horn", "ship_horn", .7f, .97f, 1.03f, 2.0},
    {"explosion", "explosion", .9f, .9f, 1.1f, .12},
    {"destroy", "destroy", .85f, .9f, 1.1f, .2},
    {"arrow", "arrow", .35f, .9f, 1.2f, .05},
    {"splash", "splash", .45f, .9f, 1.1f, .1},
    {"heal", "heal", .4f, .95f, 1.05f, .25},
}};

constexpr int VOICES = 3;       // aliases per cue so quick repeats overlap instead of cutting off
constexpr float HEARING = 28.f; // tiles; beyond this a world cue is silent

struct Voice
{
    Sound s[VOICES]{};
    int next = 0;
    double lastAt = -100;
    int count = 0;
    bool loaded = false;
};

std::array<Voice, C_Count> voices;
bool deviceOk = false, logCues = false;
float master = 1.f, sfx = 1.f, lx = 0, ly = 0;
std::mt19937 rng{7};

void fire(Cue c, float vol)
{
    if (c < 0 || c >= C_Count) // C_Count = "no cue" for table lookups with gaps
        return;

    Voice &v = voices[c];
    double now = GetTime();
    const CueDef &d = TABLE[c];
    if (vol <= .01f || now - v.lastAt < d.cooldown)
        return;

    v.lastAt = now;
    v.count++;
    if (logCues)
        spdlog::info("audio cue: {} vol={:.2f}", d.name, vol);
    if (!deviceOk || !v.loaded)
        return;

    Sound &s = v.s[v.next];
    v.next = (v.next + 1) % VOICES;
    float pitch = d.pitchMin + (d.pitchMax - d.pitchMin) * float(rng() % 1000) / 1000.f;
    SetSoundVolume(s, std::clamp(vol * d.volume * master * sfx, 0.f, 1.f));
    SetSoundPitch(s, pitch);
    PlaySound(s);
}
} // namespace

void init()
{
    const char *l = std::getenv("BW_AUDIOLOG");
    logCues = l && l[0] == '1';
    const char *off = std::getenv("BW_AUDIO");
    if (off && off[0] == '0')
        return;

    InitAudioDevice();
    deviceOk = IsAudioDeviceReady();
    if (!deviceOk)
    {
        spdlog::warn("no audio device: sound disabled");
        return;
    }

    for (int i = 0; i < C_Count; i++)
    {
        Voice &v = voices[size_t(i)];
        v.s[0] = LoadSound(asset(std::string("sounds/") + TABLE[size_t(i)].file + ".wav").c_str());
        v.loaded = IsSoundValid(v.s[0]);
        if (!v.loaded)
        {
            spdlog::warn("missing sound {}", TABLE[size_t(i)].file);
            continue;
        }
        for (int k = 1; k < VOICES; k++)
            v.s[k] = LoadSoundAlias(v.s[0]);
    }
}

void shutdown()
{
    if (!deviceOk)
        return;
    for (Voice &v : voices)
        if (v.loaded)
        {
            for (int k = 1; k < VOICES; k++)
                UnloadSoundAlias(v.s[k]);
            UnloadSound(v.s[0]);
            v.loaded = false;
        }
    CloseAudioDevice();
    deviceOk = false;
}

void setVolumes(float m, float s)
{
    master = std::clamp(m, 0.f, 1.f);
    sfx = std::clamp(s, 0.f, 1.f);
}

void setListener(float x, float y)
{
    lx = x;
    ly = y;
}

void play(Cue c)
{
    fire(c, 1.f);
}

void playAt(Cue c, float x, float y)
{
    float d = std::hypot(x - lx, y - ly);
    fire(c, std::clamp(1.f - d / HEARING, 0.f, 1.f));
}

bool active()
{
    return deviceOk;
}

int played(Cue c)
{
    return c >= 0 && c < C_Count ? voices[c].count : 0;
}
} // namespace audio
