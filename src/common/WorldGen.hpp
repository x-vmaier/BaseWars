/**
 * @file WorldGen.hpp
 * @brief Deterministic world generation, surface features, clearing and economy rules (server + client).
 */
#pragma once
#include "common/Shared.hpp"

// hashing & noise
inline uint32_t hash2(int x, int y, uint32_t s)
{
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

inline float hf(int x, int y, uint32_t s)
{
    return float(hash2(x, y, s) & 0xFFFF) / 65535.f;
}

inline float vnoise(float x, float y, uint32_t s)
{
    int xi = int(std::floor(x)), yi = int(std::floor(y));
    float fx = x - float(xi), fy = y - float(yi);
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = hf(xi, yi, s), b = hf(xi + 1, yi, s), c = hf(xi, yi + 1, s), d = hf(xi + 1, yi + 1, s);
    float top = a + (b - a) * fx, bot = c + (d - c) * fx;
    return top + (bot - top) * fy;
}

inline float fbm(float x, float y, uint32_t s, int oct = 4)
{
    float v = 0, a = 0.5f, t = 0;
    for (int o = 0; o < oct; o++)
    {
        v += a * vnoise(x, y, s + uint32_t(o) * 131u);
        t += a;
        x *= 2;
        y *= 2;
        a *= 0.5f;
    }
    return v / t;
}

inline constexpr float OCEAN_LEVEL = .43f, ISLAND_DEPTH = .06f, ISLAND_GATE = .68f,
                       ISLAND_BLOB = .74f; // below OCEAN_LEVEL: ocean (rare islands excepted)

inline float stretch(float v)
{
    return std::clamp((v - .5f) * 2.1f + .5f, 0.f, 1.f);
}

// world generation (infinite, deterministic)
inline uint8_t genBiome(int x, int y, uint32_t seed)
{
    const ServerConfig &c = cfg();
    float fx = float(x) / c.biomeScale, fy = float(y) / c.biomeScale;
    float e = stretch(fbm(fx * .0105f, fy * .0105f, seed, 4));
    float m = stretch(fbm(fx * .016f + 311.f, fy * .016f - 97.f, seed + 11, 3));
    float t = stretch(fbm(fx * .0085f - 57.f, fy * .0085f + 143.f, seed + 23, 3));
    float d = fbm(fx * .09f, fy * .09f, seed + 37, 3);
    float f = fbm(fx * .045f, fy * .045f, seed + 53, 3);

    e += (d - .5f) * .07f;
    if (e < c.oceanLevel)
    {
        // rare small islands in open (deep) ocean: a regional gate keeps them scattered, a finer noise shapes 1-3 chunk
        // wide blobs
        if (e > c.oceanLevel - ISLAND_DEPTH)
            return B_Ocean;
        float gate = vnoise(fx * .021f + 17.f, fy * .021f - 41.f, seed + 91);
        float blob = vnoise(fx * .033f - 9.f, fy * .033f + 23.f, seed + 93) * .88f +
                     vnoise(fx * .12f, fy * .12f, seed + 95) * .12f;
        if (gate < c.islandGate || blob < ISLAND_BLOB)
            return B_Ocean;
        e = .5f; // classify the island like ordinary lowland
    }
    if (e > c.highlandsHeight)
        return B_Highlands;
    if (t < c.snowCold)
        return B_Snow;
    if (t > c.desertHeat && m < .52f)
        return B_Desert;
    if (m > c.forestMoisture)
        return B_Forest;
    if (f > c.flowerNoise && m > .25f)
        return B_Flowers;
    return B_Plains;
}

inline bool isLand(int b)
{
    return b != B_Ocean && b != INVALID_BIOME;
}

inline bool isCliffTile(int self, int above)
{
    return isLand(self) && self != B_Highlands && above == B_Highlands;
}

inline int floorDiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

inline int chunkOf(int v)
{
    return floorDiv(v, CH);
}

inline int64_t chunkKey(int cx, int cy)
{
    return (int64_t(cx) << 32) | int64_t(uint32_t(cy));
}

inline int keyCx(int64_t k)
{
    return int(k >> 32);
}

inline int keyCy(int64_t k)
{
    return int(uint32_t(k & 0xFFFFFFFF));
}

inline int localIdx(int x, int y)
{
    return (y - chunkOf(y) * CH) * CH + (x - chunkOf(x) * CH);
}

// economy rules
// Production per second of a resource building, by the biome it stands on.
struct Yield
{
    float r[R_Count];
};

inline Yield yieldOf(Kind k, int biome, bool coastal)
{
    Yield y{{0, 0, 0, 0}};
    if (k == K_Lumber)
    {
        switch (biome)
        {
        case B_Forest:
            y.r[R_Wood] = 2.6f;
            break;
        default:
            y.r[R_Wood] = .45f;
            break; // lumberjacks thrive in forests only; elsewhere a trickle (meat comes from hunting)
        }
    }
    else if (k == K_Mine)
    {
        switch (biome)
        {
        case B_Highlands:
            y.r[R_Stone] = 2.4f;
            y.r[R_Gold] = .4f;
            break;
        case B_Desert:
            y.r[R_Gold] = 2.4f;
            y.r[R_Stone] = .3f;
            break;
        default:
            y.r[R_Stone] = .4f;
            y.r[R_Gold] = .1f;
            break;
        }
    }
    else if (k == K_Harbour)
    {
        y.r[R_Meat] = coastal ? 1.2f : 0.f;
        if (biome == B_Plains || biome == B_Flowers)
            y.r[R_Meat] += .4f;
    }
    return y;
}

inline bool preferredBiome(Kind k, int biome)
{
    return (k == K_Lumber && biome == B_Forest) || (k == K_Mine && (biome == B_Highlands || biome == B_Desert));
}

inline constexpr float OFFLINE_PROD = 0.12f;
inline constexpr float COLD_UNIT_DPS = 2.f, COLD_BUILDING_FRAC = .01f, WARMTH_RADIUS = 6.5f;

// [world] surface features, clearing, gold bonus (server + client)
enum Feat : uint8_t
{
    F_None,
    F_Pine,
    F_Oak,
    F_Bush,
    F_Rock,
    F_Gold
};

inline uint8_t featureAt(int x, int y, int b, uint32_t seed)
{
    float dens = b >= 0 && b < 8 ? cfg().featureDensity[b] : 1.f;
    if (dens <= 0.f)
        return F_None;
    uint32_t h = uint32_t(float(hash2(x, y, seed + 777) % 1000) /
                          dens); // density > 1 compresses the roll so more tiles hit the feature thresholds
    switch (b)
    {
    case B_Forest:
        return h < 250 ? (hash2(x, y, 3) % 100 < 30 ? F_Oak : F_Pine)
                       : (h < 290 ? F_Bush : (h < 315 ? F_Rock : F_None));
    case B_Plains:
        return h < 35 ? F_Oak : (h < 80 ? F_Bush : (h < 92 ? F_Rock : F_None));
    case B_Flowers:
        return h < 110 ? F_Bush : (h < 125 ? F_Oak : F_None);
    case B_Desert:
        return h < 30 ? F_Gold : (h < 65 ? F_Rock : F_None);
    case B_Highlands:
        return h < 110 ? F_Rock : (h < 122 ? F_Gold : F_None);
    case B_Snow:
        return h < 140 ? F_Pine : (h < 158 ? F_Rock : F_None);
    default:
        return F_None;
    }
}

inline bool featureBlocks(uint8_t f)
{
    // bushes do not block building (but can be cleared)
    return f == F_Pine || f == F_Oak || f == F_Rock || f == F_Gold;
}

inline const char *featName(uint8_t f)
{
    switch (f)
    {
    case F_Pine:
        return "Pine tree";
    case F_Oak:
        return "Oak tree";
    case F_Bush:
        return "Bush";
    case F_Rock:
        return "Rock";
    case F_Gold:
        return "Gold vein";
    default:
        return "";
    }
}

// seconds of pawn work, scaled by size / hardness (the pawn has to walk there first; the timer only runs while it
// works)
inline float clearBaseSeconds(uint8_t f)
{
    switch (f)
    {
    case F_Bush:
        return 1.5f;
    case F_Pine:
        return 3.f;
    case F_Oak:
        return 4.f;
    case F_Rock:
        return 5.f;
    case F_Gold:
        return 7.f;
    default:
        return 0.f;
    }
}

inline float clearSeconds(uint8_t f)
{
    return clearBaseSeconds(f) * (cfg().instantClear ? 0.f : cfg().clearTimeScale);
}

inline int clearCostWood(uint8_t f)
{
    switch (f)
    {
    case F_Bush:
        return 1;
    case F_Pine:
        return 3;
    case F_Oak:
        return 4;
    case F_Rock:
        return 5;
    case F_Gold:
        return 8;
    default:
        return 0;
    }
} // paid up front

inline Res dropRes(uint8_t f)
{
    // what the cleared feature yields
    return f == F_Gold ? R_Gold : (f == F_Rock ? R_Stone : R_Wood);
}

inline int dropAmount(uint8_t f)
{
    switch (f)
    {
    case F_Bush:
        return 4;
    case F_Pine:
        return 10;
    case F_Oak:
        return 14;
    case F_Rock:
        return 8;
    case F_Gold:
        return 14;
    default:
        return 0;
    }
}

inline constexpr int MAX_CLEAR_TILES = 24; // per C_Clear request
// Gold deposits near a Mine raise its output: +GOLD_BONUS_EACH per deposit within GOLD_RADIUS tiles of its centre,
// capped.
inline constexpr float GOLD_RADIUS = 4.5f, GOLD_BONUS_EACH = .2f, GOLD_BONUS_CAP = 1.f;

inline float goldBonusOf(int deposits)
{
    return std::min(GOLD_BONUS_CAP, GOLD_BONUS_EACH * float(deposits));
}

// W: int biome(x,y); bool cleared(x,y); uint32_t seedValue()
template <class W> int goldNear(W &w, Pos c)
{
    int n = 0, cx = int(std::floor(c.x)), cy = int(std::floor(c.y)), r = int(std::ceil(GOLD_RADIUS));
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++)
        {
            float dx = float(x) + .5f - c.x, dy = float(y) + .5f - c.y;
            if (dx * dx + dy * dy > GOLD_RADIUS * GOLD_RADIUS)
                continue;
            int b = w.biome(x, y);
            if (b != B_Highlands && b != B_Desert)
                continue;
            if (featureAt(x, y, b, w.seedValue()) == F_Gold && !w.cleared(x, y))
                n++;
        }
    return n;
}
