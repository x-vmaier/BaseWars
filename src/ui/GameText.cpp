#include "ui/GameText.hpp"

#include <raylib.h>

#include <cmath>

std::string costText(const float* c) {
    std::string s;
    static const char* ab[R_Count] = {"W", "G", "S", "M"};
    for (int r = 0; r < R_Count; r++) if (c[r] > 0) s += std::string(ab[r]) + std::to_string(int(c[r])) + " ";
    return s.empty() ? "free" : s;
}
int mapUpgradeCost(const Info& in, int r) { return int(std::ceil(KINFO[in.kind].cost[r] * upgradeCostMul(in.aux) * .7f)); }
std::string unlockText(int level) {
    std::string t;
    for (int u = K_Pawn; u <= K_Monk; u++) if (unitUnlockLevel(Kind(u)) == level) t += (t.empty() ? "" : ", ") + std::string(KINFO[u].name);
    return t;
}
void nextLevelInfo(const Info& in, std::vector<std::string>& out, std::vector<Kind>& units) {
    int L = in.aux, N = L + 1;
    if (L >= MAX_LEVEL || in.kind == K_Campfire) return;
    out.push_back(TextFormat("Health +%d%%", int(std::lround((levelHp(N) / levelHp(L) - 1.f) * 100.f))));
    if (isStorage(in.kind)) out.push_back(TextFormat("Capacity %d -> %d", int(storageCap(in.kind, L)), int(storageCap(in.kind, N))));
    if (KINFO[in.kind].dps > 0) out.push_back(TextFormat("Defence %.0f -> %.0f dps", KINFO[in.kind].dps * levelMul(L), KINFO[in.kind].dps * levelMul(N)));
    if (isResourceBuilding(in.kind)) out.push_back(TextFormat("Output x%.1f -> x%.1f", levelMul(L), levelMul(N)));
    if (in.kind == K_Archery) {
        std::string u = unlockText(N);
        if (!u.empty()) out.push_back("Unlocks: " + u);
        for (int k = K_Pawn; k <= K_Monk; k++) if (unitUnlockLevel(Kind(k)) == N) units.push_back(Kind(k));
        int maxLv = 1;
        for (int t = 2; t <= MAX_UNIT_LEVEL; t++) if (troopUpgradeNeedsArchery(t) <= N) maxLv = t;
        out.push_back(TextFormat("Troops upgradable to Lv%d", maxLv));
    }
}

TroopText troopText(Kind k) {
    switch (k) {
    case K_Pawn: return {"Worker", "Carries farmed resources to the nearest storage and hauls stock between outposts, storage houses and the Town Hall. Weak in a fight, and hauling parties can be raided."};
    case K_Warrior: return {"Frontline infantry", "Sturdy sword and shield fighter. Takes 20% less damage from every attack. The backbone of any army against both units and buildings."};
    case K_Archer: return {"Ranged support", "Shoots arrows from a distance. Deals only 70% damage to buildings and dies quickly in melee, so keep them behind your warriors."};
    case K_Lancer: return {"Fast spear cavalry", "The fastest troop. Deals 30% extra damage to units but only 60% to buildings. Ideal for hunting convoys and raiders."};
    case K_Monk: return {"Healer", "Automatically heals the most wounded friendly unit nearby. Cannot attack, so escort them. Higher levels heal faster."};
    default: return {"", ""};
    }
}
void troopCost(Kind k, int toLevel, float* cost) {
    for (int i = 0; i < R_Count; i++) cost[i] = std::ceil(KINFO[k].cost[i] * 2.5f * float(toLevel - 1) + (i == R_Gold ? 30.f * float(toLevel - 1) : 0.f));
}
