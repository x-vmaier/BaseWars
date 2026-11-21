/**
 * @file Hints.cpp
 * @brief Hint table and its show-once / cooldown rules.
 */
#include "ui/Hints.hpp"

#include <raylib.h>

namespace
{
enum HintFlag
{
    HF_Always = 1, // status line, not advice: ignores the settings toggle and any cooldown
};

struct HintDef
{
    const char *text;
    int flags;
    double show;     // seconds a started hint stays visible (ignored for HF_Always)
    double cooldown; // seconds until it may show again; < 0 = once per session
};

const HintDef HINTS[H_Count] = {
    {"LMB select/attack  RMB command/menu  M move  A attack  B build  P players  H home  Arrows/MMB pan  Wheel zoom", 0,
     1e9, 0},
    {"Low food! Free pawns hunt sheep; Harbours fish. Food pays for units, buildings, upgrades and repairs.", 0, 1e9,
     0},
    {"MOVE: click a destination", HF_Always, 0, 0},
    {"ATTACK: click a target or ground (attack-move)", HF_Always, 0, 0},
    {"Left-click places the building, Shift keeps placing, Esc cancels. Close to trees, rocks or the coast is best.", 0,
     9, -1},
    {"Your town is burning. Found a new one elsewhere.", HF_Always, 0, 0},
};
} // namespace

const char *Hints::get(HintId id, bool enabled)
{
    const HintDef &h = HINTS[id];
    if (h.flags & HF_Always)
        return h.text;
    if (!enabled)
        return nullptr;

    double now = GetTime();
    if (now < until[id])
        return h.text;
    if (nextAt[id] < 0 || now < nextAt[id]) // spent or cooling down
        return nullptr;
    until[id] = now + h.show;
    nextAt[id] = h.cooldown < 0 ? -1 : now + h.show + h.cooldown;
    return h.text;
}
