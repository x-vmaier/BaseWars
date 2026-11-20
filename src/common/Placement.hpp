/**
 * @file Placement.hpp
 * @brief Building placement rules, identical on server and client.
 */
#pragma once
#include "common/Shared.hpp"
#include "common/WorldGen.hpp"

// placement rules (identical on server & client)
enum PlaceErr
{
    P_OK,
    P_Unexplored,
    P_Terrain,
    P_Occupied,
    P_Territory,
    P_Coast,
    P_TooClose,
    P_Enemy,
    P_Spacing,
    P_Radius,
    P_Count
};

enum PlaceMode : int
{
    PM_Normal = 0,
    PM_HomelessHall = 1,
    PM_MoveHall = 2
}; // PM_*: where a Town Hall may stand (see checkPlace)

inline constexpr const char *PLACE_TEXT[P_Count] = {
    "OK",
    "Explore this area first",
    "Cannot build on this terrain",
    "Something is in the way",
    "Outside your territory",
    "Harbours must touch the ocean",
    "Too close to another base",
    "Inside enemy territory",
    "Too close to another campfire",
    "Too far from your clan's towns",
};

inline int anchorRadius(Kind k)
{
    return k == K_TownHall ? 14 : (k == K_Outpost ? 10 : 0);
}

inline Pos footCenter(Kind k, int tx, int ty)
{
    return {float(tx) + float(KINFO[k].fw) * .5f, float(ty) + float(KINFO[k].fh) * .5f};
}

// W: int biome(x,y) [INVALID_BIOME if unknown]; bool explored(x,y); bool occupied(x,y);
//    void anchors(F) calling F(Pos center, Kind kind, bool mine, bool allied) for each FUNCTIONAL Town Hall / Outpost.
//    optional: bool campfireNear(Pos c, float r)  (any campfire closer than r), bool inClanRadius(Pos c) (homeless Town
//    Hall placement)
inline constexpr float CAMPFIRE_SPACING = 6.f, CLAN_TOWN_RADIUS = 120.f;
inline constexpr float OUTPOST_REACH = 34.f;
inline constexpr float TOWN_MOVE_COST[R_Count] = {300, 500, 200, 0}; // wood, gold, stone, meat for moving the Town Hall
inline constexpr float TOWN_MOVE_TIME =
    180.f; // seconds of construction   // an Outpost may stand this far (tiles) from the Town Hall

template <class W> bool wCampfireNear(W &w, Pos c, float r)
{
    if constexpr (requires { w.campfireNear(c, r); })
        return w.campfireNear(c, r);
    else
        return false;
}

template <class W> bool wInClanRadius(W &w, Pos c)
{
    if constexpr (requires { w.inClanRadius(c); })
        return w.inClanRadius(c);
    else
        return true;
}

// Every footprint tile is explored, land and not a cliff face.
template <class W> int placeTerrainCheck(W &w, const KindInfo &ki, int tx, int ty)
{
    for (int y = ty; y < ty + ki.fh; y++)
        for (int x = tx; x < tx + ki.fw; x++)
        {
            if (!w.explored(x, y))
                return P_Unexplored;

            int b = w.biome(x, y);
            if (b == INVALID_BIOME)
                return P_Unexplored;

            if (!isLand(b) || isCliffTile(b, w.biome(x, y - 1)))
                return P_Terrain;
        }
    return P_OK;
}

// A coastal building needs an ocean tile next to its footprint (corners do not count).
template <class W> bool touchesOcean(W &w, const KindInfo &ki, int tx, int ty)
{
    for (int y = ty - 1; y <= ty + ki.fh; y++)
        for (int x = tx - 1; x <= tx + ki.fw; x++)
        {
            bool inside = x >= tx && x < tx + ki.fw && y >= ty && y < ty + ki.fh;
            bool corner = (x < tx || x >= tx + ki.fw) && (y < ty || y >= ty + ki.fh);
            if (!inside && !corner && w.biome(x, y) == B_Ocean)
                return true;
        }
    return false;
}

// Territory rules for everything except campfires: inside own territory (Town Hall / Outpost reach), away from enemies
// and, for anchors, away from other anchors.
template <class W> int placeTerritoryCheck(W &w, Kind k, Pos c, int mode)
{
    bool freeHall = k == K_TownHall && mode != PM_Normal;
    bool inTerritory = false, tooClose = false, enemy = false, anyOwn = false;
    float reach = k == K_TownHall ? 60.f : (k == K_Outpost ? OUTPOST_REACH : 0.f);

    w.anchors([&](Pos a, Kind ak, bool mine, bool ally)
    {
        float d = std::max(std::abs(a.x - c.x), std::abs(a.y - c.y));
        int rad = anchorRadius(ak);
        if (mine)
            anyOwn = true;
        if (mine && (k == K_Outpost ? (ak == K_TownHall && d <= reach) : (reach > 0 ? d <= reach : d <= float(rad))))
            inTerritory = true;
        if (!mine && !ally && d <= float(rad + 2))
            enemy = true;
        if (k == K_Outpost && (mine || ally) && d < 12.f)
            tooClose = true;
        if (k == K_TownHall && d < 26.f)
            tooClose = true;
    });

    if (enemy)
        return P_Enemy;
    if (!freeHall && (!anyOwn || !inTerritory))
        return P_Territory;
    if (tooClose)
        return P_TooClose;
    if (mode == PM_HomelessHall && !wInClanRadius(w, c))
        return P_Radius;
    return P_OK;
}

template <class W> int checkPlace(W &w, Kind k, int tx, int ty, int mode = PM_Normal)
{
    const KindInfo &ki = KINFO[k];

    if (int err = placeTerrainCheck(w, ki, tx, ty))
        return err;

    for (int y = ty; y < ty + ki.fh; y++)
        for (int x = tx; x < tx + ki.fw; x++)
            if (w.occupied(x, y))
                return P_Occupied;

    if (hasTag(k, KT_Coastal) && !touchesOcean(w, ki, tx, ty))
        return P_Coast;

    Pos c = footCenter(k, tx, ty);
    if (hasTag(k, KT_Warm)) // anywhere, but spaced out; claims no territory
        return wCampfireNear(w, c, CAMPFIRE_SPACING) ? P_Spacing : P_OK;

    return placeTerritoryCheck(w, k, c, mode);
}
