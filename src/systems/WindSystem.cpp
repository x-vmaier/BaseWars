/**
 * @file WindSystem.cpp
 * @brief The world wind: slow drift of its direction, S_Wind replication and the save section.
 */
#include "systems/WindSystem.hpp"

#include "systems/Systems.hpp"

#include <algorithm>

WindSystem::WindSystem(World &world, Systems &s) : world(world), reg(world.reg), s(s)
{
    Wind w;
    w.rng = world.seed * 2654435761u + 77u;
    w.dir = w.target = roll(w, 0.f, WIND_TAU);
    w.shiftIn = cfg().windShiftSeconds;
    reg.ctx().emplace<Wind>(w);
}

float WindSystem::roll(Wind &w, float lo, float hi)
{
    w.rng = w.rng * 1664525u + 1013904223u;
    return lo + (hi - lo) * float(w.rng >> 8) / float(1u << 24);
}

void WindSystem::second()
{
    Wind &w = wind();
    w.shiftIn -= 1.f;
    if (w.shiftIn <= 0.f)
    {
        w.target = roll(w, 0.f, WIND_TAU);
        w.shiftIn = cfg().windShiftSeconds * roll(w, .6f, 1.4f);
    }
    float d = windAngleDelta(w.dir, w.target), step = cfg().windTurnRate;
    w.dir += std::clamp(d, -step, step);
    w.dir = std::fmod(w.dir + WIND_TAU, WIND_TAU);
}

void WindSystem::send(int id)
{
    Writer w;
    w.put<uint8_t>(S_Wind)
        .put<uint8_t>(windToByte(wind().dir))
        .put<uint8_t>(uint8_t(std::lround(std::clamp(cfg().windStrength, 0.f, 1.f) * 100.f)));
    world.net.send(world.pl[id].conn, w);
}

// [f32 dir][f32 target][f32 shiftIn][u32 rng]; absent in older saves (the wind then starts from the seed).
void WindSystem::save(Writer &w) const
{
    const Wind &x = reg.ctx().get<Wind>();
    w.put<float>(x.dir).put<float>(x.target).put<float>(x.shiftIn).put<uint32_t>(x.rng);
}

void WindSystem::load(Reader &r)
{
    if (r.p >= r.e)
        return;
    Wind x;
    x.dir = r.get<float>();
    x.target = r.get<float>();
    x.shiftIn = r.get<float>();
    x.rng = r.get<uint32_t>();
    if (r.ok && std::isfinite(x.dir + x.target + x.shiftIn))
        wind() = x;
}
