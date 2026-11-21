/**
 * @file AudioSystem.hpp
 * @brief Client sound effects: one table of cues (sample, volume, pitch range, cooldown), positional falloff.
 */
#pragma once

namespace audio
{
// Append only; every cue has exactly one row in the table in AudioSystem.cpp.
enum Cue
{
    C_UiClick,
    C_UiHover,
    C_Chop,
    C_Mine,
    C_Build,
    C_Hit,
    C_Horn,
    C_Victory,
    C_Coin,
    C_ShipHorn,
    C_Explosion,
    C_Destroy,
    C_Arrow,
    C_Splash,
    C_Heal,
    C_Count
};

// Never fails: without an audio device (or BW_AUDIO=0) cues are only counted / logged (BW_AUDIOLOG=1).
void init();
void shutdown();
void setVolumes(float master, float sfx); // 0..1
void setListener(float tileX, float tileY);
void play(Cue c);                     // not positional (UI)
void playAt(Cue c, float x, float y); // x, y in tiles: fades with distance to the listener, silent when far
bool active();                        // a device is open
int played(Cue c);                    // number of times the cue actually fired (tests / log)
} // namespace audio
