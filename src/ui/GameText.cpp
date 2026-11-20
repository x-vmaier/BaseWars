/**
 * @file GameText.cpp
 * @brief Game rules rendered as text for panels (costs are drawn with ui/CostRow): unlocks, next-level previews, troop
 * descriptions.
 */
#include "ui/GameText.hpp"

#include <raylib.h>

#include <cmath>

int mapUpgradeCost(const Info &in, int r)
{
    float c[R_Count];
    upgradeCost(in.kind, in.aux, c);
    return int(c[r]);
}
std::string timeText(float s)
{
    int t = int(std::ceil(s));
    if (t < 60)
        return std::to_string(t) + " s";
    if (t < 3600)
        return std::to_string(t / 60) + " min" + (t % 60 ? " " + std::to_string(t % 60) + " s" : "");
    return std::to_string(t / 3600) + " h " + std::to_string(t / 60 % 60) + " min";
}
std::string unlockText(int level)
{
    std::string t;
    for (int u = K_Pawn; u <= K_Monk; u++)
        if (unitUnlockLevel(Kind(u)) == level)
            t += (t.empty() ? "" : ", ") + std::string(KINFO[u].name);
    return t;
}
void nextLevelInfo(const Info &in, std::vector<std::string> &out, std::vector<Kind> &units)
{
    int L = in.aux, N = L + 1;
    if (L >= MAX_LEVEL || hasTag(in.kind, KT_SingleLevel))
        return;
    out.push_back(TextFormat("Health +%d%%, takes %s of pawn work",
                             int(std::lround((levelHp(N) / levelHp(L) - 1.f) * 100.f)),
                             timeText(upgradeSeconds(in.kind, L)).c_str()));
    if (isStorage(in.kind))
        out.push_back(TextFormat("Capacity %d -> %d", int(storageCap(in.kind, L)), int(storageCap(in.kind, N))));
    if (KINFO[in.kind].dps > 0)
        out.push_back(
            TextFormat("Defence %.0f -> %.0f dps", KINFO[in.kind].dps * levelMul(L), KINFO[in.kind].dps * levelMul(N)));
    if (isResourceBuilding(in.kind))
        out.push_back(TextFormat("Output x%.1f -> x%.1f", levelMul(L), levelMul(N)));
    if (hasTag(in.kind, KT_Trainer))
    {
        std::string u = unlockText(N);
        if (!u.empty())
            out.push_back("Unlocks: " + u);
        for (int k = K_Pawn; k <= K_Monk; k++)
            if (unitUnlockLevel(Kind(k)) == N)
                units.push_back(Kind(k));
        int maxLv = 1;
        for (int t = 2; t <= MAX_UNIT_LEVEL; t++)
            if (troopUpgradeNeedsArchery(t) <= N)
                maxLv = t;
        out.push_back(TextFormat("Troops upgradable to Lv%d", maxLv));
    }
}

void townHallUnlocks(int level, std::vector<std::string> &out)
{
    if (level < 2 || level > MAX_LEVEL)
        return;
    std::string fresh, more;
    for (int k = 0; k < K_Count; k++)
    {
        Kind kk = Kind(k);
        if (!KINDS[k].menuSlot)
            continue;
        if (thUnlock(kk) == level)
            fresh += (fresh.empty() ? "" : ", ") + std::string(KINFO[k].name);
        else if (thUnlock(kk) < level && maxCount(kk, level) > maxCount(kk, level - 1))
            more += (more.empty() ? "" : ", ") + std::string(KINFO[k].name) +
                    TextFormat(" %d->%d", maxCount(kk, level - 1), maxCount(kk, level));
    }
    std::string troops;
    for (int u = K_Pawn; u <= K_Monk; u++)
        if (unitThLevel(Kind(u)) == level)
            troops += (troops.empty() ? "" : ", ") + std::string(KINFO[u].name);
    if (!fresh.empty())
        out.push_back("Unlocks: " + fresh);
    if (!more.empty())
        out.push_back("More allowed: " + more);
    if (!troops.empty())
        out.push_back("Troops: " + troops);
    out.push_back(TextFormat("Other buildings may reach Lv%d", level));
}

TroopText troopText(Kind k)
{
    return {KINDS[k].role, KINDS[k].blurb};
}
void troopCost(Kind k, int toLevel, float *cost)
{
    for (int i = 0; i < R_Count; i++)
        cost[i] =
            std::ceil(KINFO[k].cost[i] * 2.5f * float(toLevel - 1) + (i == R_Gold ? 30.f * float(toLevel - 1) : 0.f));
}
