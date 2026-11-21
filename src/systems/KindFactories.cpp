/**
 * @file KindFactories.cpp
 * @brief Table of per-kind creation / training hooks (add a unit's components here).
 */
#include "systems/KindFactories.hpp"

#include "systems/Systems.hpp"

#include <array>

namespace
{
void attachWander(World &w, entt::entity e, bool loaded)
{
    float phase = loaded ? 1.f : float(w.rng() % 300) / 100.f;
    w.reg.emplace<Wander>(e, Wander{phase});
}

void attachHealer(World &w, entt::entity e, bool)
{
    w.reg.emplace<Healer>(e);
}

// Empty boat at upgrade level 1.
void shipTrained(Systems &s, World &w, entt::entity unit, entt::entity, Pos at)
{
    s.naval.initShip(unit);
    w.fx(E_Splash, at.x, at.y);
}
} // namespace

const KindFactory &kindFactory(Kind k)
{
    static const std::array<KindFactory, K_Count> table = []
    {
        std::array<KindFactory, K_Count> t{};

        t[K_Sheep].attach = attachWander;
        t[K_Monk].attach = attachHealer;
        for (int k = 0; k < K_Count; k++)
            if (hasTag(Kind(k), KT_Naval))
                t[k].onTrained = shipTrained;
        return t;
    }();
    return table[k];
}
