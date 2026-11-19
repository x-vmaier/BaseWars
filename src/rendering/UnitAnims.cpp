#include "rendering/UnitAnims.hpp"

#include "rendering/Palette.hpp"

#include <cmath>
#include <string>

Anim extinguishAnim(Kind k, int owner) {
    std::string base = std::string("Units/") + COLOR_NAME[colorIdx(owner)] + " Units/";
    switch (k) {
    case K_Warrior: return mkAnim(base + "Warrior/Warrior_Run.png", 192, 192, 14, 96, 135);
    case K_Archer: return mkAnim(base + "Archer/Archer_Run.png", 192, 192, 14, 96, 135);
    case K_Lancer: return mkAnim(base + "Lancer/Lancer_Run.png", 320, 320, 14, 160, 198);
    case K_Monk: return mkAnim(base + "Monk/Run.png", 192, 192, 12, 96, 135);
    default: return mkAnim(base + "Pawn/Pawn_Interact Hammer.png", 192, 192, 8, 96, 135);
    }
}

Anim unitAnim(Kind k, int owner, int state, int aux, int aim, bool moving, int biome, double t) {
    std::string base = std::string("Units/") + COLOR_NAME[colorIdx(owner)] + " Units/";
    float ang = float(aim) / 255.f * 6.28319f - 3.14159f;
    if (state == US_Extinguish) return extinguishAnim(k, owner);
    switch (k) {
    case K_Pawn: {
        if (state == US_Work) return mkAnim(base + (biome == B_Forest ? "Pawn/Pawn_Interact Axe.png" : "Pawn/Pawn_Interact Pickaxe.png"), 192, 192, 9, 96, 135);
        if (state == US_Attack) return mkAnim(base + "Pawn/Pawn_Interact Knife.png", 192, 192, 10, 96, 135);
        static const char* carry[] = {"", " Wood", " Gold", " Gold", " Meat"};
        std::string sfx = aux >= 1 && aux <= 4 ? carry[aux] : "";
        return mkAnim(base + (moving ? "Pawn/Pawn_Run" : "Pawn/Pawn_Idle") + sfx + ".png", 192, 192, moving ? 10 : 7, 96, 135);
    }
    case K_Warrior:
        if (state == US_Attack) return mkAnim(base + (int(t * 2.5) % 2 ? "Warrior/Warrior_Attack2.png" : "Warrior/Warrior_Attack1.png"), 192, 192, 10, 96, 135);
        return mkAnim(base + (moving ? "Warrior/Warrior_Run.png" : "Warrior/Warrior_Idle.png"), 192, 192, moving ? 10 : 7, 96, 135);
    case K_Archer:
        if (state == US_Attack) return mkAnim(base + "Archer/Archer_Shoot.png", 192, 192, 10, 96, 135);
        return mkAnim(base + (moving ? "Archer/Archer_Run.png" : "Archer/Archer_Idle.png"), 192, 192, moving ? 10 : 7, 96, 135);
    case K_Lancer: {
        if (state == US_Attack) {
            float dx = std::cos(ang), dy = std::sin(ang);
            const char* d = std::abs(dy) < .38f ? "Right" : (dy > 0 ? (std::abs(dx) < .38f ? "Down" : "DownRight") : (std::abs(dx) < .38f ? "Up" : "UpRight"));
            return mkAnim(base + "Lancer/Lancer_" + d + "_Attack.png", 320, 320, 8, 160, 198);
        }
        return mkAnim(base + (moving ? "Lancer/Lancer_Run.png" : "Lancer/Lancer_Idle.png"), 320, 320, moving ? 10 : 9, 160, 198);
    }
    case K_Monk:
        if (state == US_Heal) return mkAnim(base + "Monk/Heal.png", 192, 192, 10, 96, 135);
        return mkAnim(base + (moving ? "Monk/Run.png" : "Monk/Idle.png"), 192, 192, moving ? 9 : 7, 96, 135);
    case K_Sheep:
        return mkAnim(std::string("Terrain/Resources/Meat/Sheep/") + (moving ? "Sheep_Move.png" : "Sheep_Idle.png"), 128, 128, 7, 64, 84);
    default: return mkAnim(base + "Warrior/Warrior_Idle.png", 192, 192, 7, 96, 135);
    }
}

BSpr buildingSprite(Kind k, int) {
    switch (k) {
    case K_TownHall: return {"Castle", .8f, 249, 256, 320};
    case K_Outpost: return {"Tower", .8f, 230, 256, 128};
    case K_Storage: return {"Barracks", .66f, 245, 256, 192};
    case K_Archery: return {"Archery", 1.f, 240, 256, 192};
    case K_Harbour: return {"House3", 1.f, 172, 192, 128};
    case K_Lumber: return {"House1", .9f, 173, 192, 128};      // always House1: House2 is a rotated variant and is never used
    default: return {"", 1, 0, 0, 0};
    }
}
