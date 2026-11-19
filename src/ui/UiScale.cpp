/**
 * @file UiScale.cpp
 * @brief UI scale.
 */
#include "ui/UiScale.hpp"

#include <rlgl.h>

#include <algorithm>

namespace ui
{
namespace
{
const float *scaleSetting = nullptr;
}

void bindScale(const float *setting)
{
    scaleSetting = setting;
}
float scale()
{
    return std::clamp(scaleSetting ? *scaleSetting : 1.f, 0.6f, 2.0f);
}
int screenW()
{
    return int(float(::GetScreenWidth()) / scale());
}
int screenH()
{
    return int(float(::GetScreenHeight()) / scale());
}
Vector2 mouse()
{
    Vector2 m = ::GetMousePosition();
    return {m.x / scale(), m.y / scale()};
}
void begin()
{
    rlPushMatrix();
    rlScalef(scale(), scale(), 1.f);
}
void end()
{
    rlPopMatrix();
}

} // namespace ui
