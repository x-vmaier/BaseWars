/**
 * @file Hints.hpp
 * @brief The one hint table: every advice / help line the HUD shows, with show-once / cooldown rules and a settings
 * toggle. UI code asks `Hints::get(id)` instead of owning strings.
 */
#pragma once

enum HintId
{
    H_Controls, // bottom help line
    H_LowFood,
    H_MoveMode,
    H_AttackMode,
    H_Placing, // first time a building is being placed
    H_Defeated,
    H_Count
};

class Hints
{
  public:
    // Text to show now, or nullptr: disabled by the setting, spent (show-once), or cooling down. A hint that was
    // started keeps being returned for its display time. `enabled` is Settings::hints.
    const char *get(HintId id, bool enabled);

  private:
    double until[H_Count]{}, nextAt[H_Count]{};
};
